// SPDX-License-Identifier: GPL-3.0-or-later
#include "map_input.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>
#include <string_view>
#include <tuple>

#include "order_questions.hpp"

#include "o1602/core/file.hpp"
#include "o1602/render/island_renderer.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/growth.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

constexpr double kScrollSpeed = 900.0;  // pixels per second

/// What an action does, for the key overview: keys.action.<its name in keys.cfg>.
const std::string& action_label(ui::KeyAction a) {
    return ui::tr("keys.action." + std::string(ui::KeyBindings::name(a)));
}

/// A key as the overview shows it: the usual keys by the names of the language, a key bound by
/// its place by what the keyboard's layout puts there.
std::string key_label(const ui::KeyChord& c) {
    static constexpr std::pair<SDL_Keycode, std::string_view> kNames[] = {
        {SDLK_SPACE, "space"},
        {SDLK_DELETE, "delete"},
        {SDLK_ESCAPE, "escape"},
        {SDLK_PAGEUP, "page_up"},
        {SDLK_PAGEDOWN, "page_down"},
        {SDLK_HOME, "home"},
        {SDLK_UP, "up"},
        {SDLK_DOWN, "down"},
        {SDLK_LEFT, "left"},
        {SDLK_RIGHT, "right"},
        {SDLK_RETURN, "return"},
        {SDLK_PRINTSCREEN, "print_screen"},
        {SDLK_KP_PLUS, "keypad_plus"},
        {SDLK_KP_MINUS, "keypad_minus"},
        {SDLK_TAB, "tab"},
        {SDLK_PAUSE, "pause"},
    };
    const SDL_Keycode keycode = c.physical
                                    ? SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(c.key), SDL_KMOD_NONE, true)
                                    : static_cast<SDL_Keycode>(c.key);
    std::string name;
    for (const auto& [key, label] : kNames) {
        if (key == keycode) {
            name = ui::tr("keys.name." + std::string(label));
        }
    }
    if (name.empty()) {
        const char* n = SDL_GetKeyName(keycode);
        name = n != nullptr ? n : "?";
    }
    return (c.ctrl ? ui::tr("keys.modifier.ctrl") : std::string()) +
           (c.shift ? ui::tr("keys.modifier.shift") : std::string()) +
           (c.alt ? ui::tr("keys.modifier.alt") : std::string()) + name;
}

ui::KeyChord chord(SDL_Keycode key, bool ctrl = false, bool shift = false) {
    return ui::KeyChord{static_cast<std::int32_t>(key), ctrl, shift, false};
}

/// A key by its place on the keyboard: the same on QWERTY, QWERTZ and AZERTY.
ui::KeyChord place(SDL_Scancode key, bool ctrl = false, bool shift = false) {
    return ui::KeyChord{static_cast<std::int32_t>(key), ctrl, shift, false, true};
}

std::string key_name(std::int32_t key, bool physical) {
    const char* name =
        physical ? SDL_GetScancodeName(static_cast<SDL_Scancode>(key)) : SDL_GetKeyName(static_cast<SDL_Keycode>(key));
    return std::string(name != nullptr ? name : "");
}

std::optional<std::int32_t> key_from_name(std::string_view name, bool physical) {
    const std::string text(name);
    if (physical) {
        const SDL_Scancode sc = SDL_GetScancodeFromName(text.c_str());
        return sc == SDL_SCANCODE_UNKNOWN ? std::nullopt : std::optional<std::int32_t>(static_cast<std::int32_t>(sc));
    }
    const SDL_Keycode key = SDL_GetKeyFromName(text.c_str());
    return key == SDLK_UNKNOWN ? std::nullopt : std::optional<std::int32_t>(static_cast<std::int32_t>(key));
}

ui::KeyPress key_press(int key, int scancode, std::uint16_t mod) {
    return ui::KeyPress{key, scancode, (mod & SDL_KMOD_CTRL) != 0, (mod & SDL_KMOD_SHIFT) != 0,
                        (mod & SDL_KMOD_ALT) != 0};
}

}  // namespace

MapInput::MapInput(const Parts& parts)
    : m_host(parts.host),
      m_buildings(parts.buildings),
      m_figures(parts.figures),
      m_state(parts.state),
      m_local_player(parts.local_player),
      m_camera(parts.camera),
      m_tool(parts.tool),
      m_commands(parts.commands),
      m_sound(parts.sound),
      m_window(parts.window),
      m_hud(parts.hud),
      m_original_mouse(parts.original_mouse),
      m_confirm(parts.confirm),
      m_keys_file(parts.keys_file) {
    setup_keys();
}

void MapInput::setup_keys() {
    using ui::KeyAction;
    using ui::KeyContext;
    ui::KeyBindings& k = m_keys;
    const auto general = [&](ui::KeyChord c, KeyAction a, std::uint8_t p = 0) { k.bind(KeyContext::General, c, a, p); };
    const auto building = [&](ui::KeyChord c, KeyAction a, std::uint8_t p = 0) {
        k.bind(KeyContext::Building, c, a, p);
    };
    const auto units = [&](ui::KeyChord c, KeyAction a) { k.bind(KeyContext::Units, c, a); };
    // Camera and game. W A S D, Q and E by their place, as the digits of the groups: on AZERTY they
    // lie where Z Q S D, A and E are. Keys that stand for a word (B for building) go by meaning.
    general(place(SDL_SCANCODE_W), KeyAction::ScrollUp);
    general(chord(SDLK_UP), KeyAction::ScrollUp);
    general(place(SDL_SCANCODE_S), KeyAction::ScrollDown);
    general(chord(SDLK_DOWN), KeyAction::ScrollDown);
    general(place(SDL_SCANCODE_A), KeyAction::ScrollLeft);
    general(chord(SDLK_LEFT), KeyAction::ScrollLeft);
    general(place(SDL_SCANCODE_D), KeyAction::ScrollRight);
    general(chord(SDLK_RIGHT), KeyAction::ScrollRight);
    general(place(SDL_SCANCODE_Q), KeyAction::RotateViewLeft);
    general(place(SDL_SCANCODE_E), KeyAction::RotateViewRight);
    general(chord(SDLK_PAGEUP), KeyAction::ZoomIn);
    general(chord(SDLK_PAGEDOWN), KeyAction::ZoomOut);
    general(chord(SDLK_SPACE), KeyAction::JumpToMessage);
    general(chord(SDLK_HOME), KeyAction::CentreOnSelection);
    general(chord(SDLK_F3), KeyAction::Pause);
    general(chord(SDLK_PAUSE), KeyAction::Pause);
    general(chord(SDLK_KP_PLUS), KeyAction::SpeedUp);
    general(chord(SDLK_KP_MINUS), KeyAction::SpeedDown);
    general(chord(SDLK_F5), KeyAction::QuickSave);
    general(chord(SDLK_F9), KeyAction::QuickLoad);
    general(chord(SDLK_F10), KeyAction::GameMenu);
    general(chord(SDLK_F12), KeyAction::Screenshot);
    general(chord(SDLK_PRINTSCREEN), KeyAction::Screenshot);
    general(chord(SDLK_F1), KeyAction::Help);
    general(chord(SDLK_ESCAPE), KeyAction::Cancel);
    // Windows
    general(chord(SDLK_B), KeyAction::ModeBuild);
    general(chord(SDLK_K), KeyAction::ModeMilitary);
    general(chord(SDLK_I), KeyAction::ModeInfo);
    general(chord(SDLK_O), KeyAction::ModeOptions);
    general(chord(SDLK_L), KeyAction::ShipList);
    general(chord(SDLK_L, false, true), KeyAction::CityList);
    general(chord(SDLK_V), KeyAction::Diplomacy);
    general(chord(SDLK_DELETE), KeyAction::DemolishTool);
    // Groups: keys 1 .. 9, 0 are groups 0 .. 9.
    const std::array<SDL_Scancode, 10> digits{SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
                                              SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8,
                                              SDL_SCANCODE_9, SDL_SCANCODE_0};
    for (std::size_t g = 0; g < digits.size(); ++g) {
        const auto p = static_cast<std::uint8_t>(g);
        general(place(digits[g], true), KeyAction::GroupStore, p);
        general(place(digits[g], false, true), KeyAction::GroupAdd, p);
        general(place(digits[g]), KeyAction::GroupRecall, p);
    }
    general(chord(SDLK_PERIOD), KeyAction::NextIdleShip);
    general(chord(SDLK_COMMA), KeyAction::NextIdleUnit);
    // Viewer
    general(chord(SDLK_PLUS, true), KeyAction::PixelScaleUp);
    general(chord(SDLK_EQUALS, true), KeyAction::PixelScaleUp);
    general(chord(SDLK_MINUS, true), KeyAction::PixelScaleDown);
    general(chord(SDLK_O, true), KeyAction::ShowOwners);
    general(chord(SDLK_Q, true), KeyAction::Quit);
    // Building
    building(chord(SDLK_R), KeyAction::RotateBuilding);
    building(chord(SDLK_C), KeyAction::PickBuilding);
    building(chord(SDLK_DELETE), KeyAction::DemolishTool);
    building(chord(SDLK_TAB), KeyAction::NextBuilding);
    building(chord(SDLK_TAB, false, true), KeyAction::NextBuilding, 1);
    // Orders to the selected units
    // None of these means anything in general (no clash with W A S D).
    units(chord(SDLK_F), KeyAction::AttackMove);  // "Feuer"
    units(chord(SDLK_H), KeyAction::Stop);        // "Halt"
    units(chord(SDLK_G), KeyAction::HoldPosition);
    units(chord(SDLK_P), KeyAction::Patrol);
    units(chord(SDLK_C), KeyAction::Formation);
    units(chord(SDLK_C, false, true), KeyAction::Scatter);
    units(chord(SDLK_Y), KeyAction::WhiteFlag);
    units(chord(SDLK_X), KeyAction::Explore);
    units(chord(SDLK_T), KeyAction::Trade);
    units(chord(SDLK_R), KeyAction::EditRoute);
    units(chord(SDLK_DELETE), KeyAction::Delete);
    units(chord(SDLK_TAB), KeyAction::NextKindInSelection);
}

void MapInput::load_key_settings(const std::filesystem::path& default_file) {
    const std::filesystem::path path = m_keys_file.empty() ? default_file : m_keys_file;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (!m_keys_file.empty()) {
            std::fprintf(stderr, "keys: %s not found, defaults used\n", core::path_to_utf8(path).c_str());
            return;
        }
        // The defaults as a file to edit (in the user's folder, never in the installation).
        const std::string text = m_keys.write_text(key_name, true);
        std::filesystem::create_directories(path.parent_path(), ec);
        const auto* data = reinterpret_cast<const std::uint8_t*>(text.data());
        if (auto status = core::write_file_atomic(path, std::span<const std::uint8_t>(data, text.size())); !status) {
            std::fprintf(stderr, "keys: %s\n", status.error().message.c_str());
        }
        return;
    }
    auto bytes = core::read_file(path);
    if (!bytes) {
        std::fprintf(stderr, "keys: %s\n", bytes.error().message.c_str());
        return;
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    for (const std::string& w : m_keys.read_text(text, key_from_name)) {
        std::fprintf(stderr, "keys: %s: %s\n", core::path_to_utf8(path).c_str(), w.c_str());
    }
}

void MapInput::show_key_help() {
    if (!m_hud) {
        return;
    }
    using ui::KeyAction;
    using ui::KeyContext;
    std::vector<Hud::HelpLine> lines;
    for (const auto& [context, heading] :
         {std::pair<KeyContext, const char*>{KeyContext::General, "keys.heading.general"},
          {KeyContext::Building, "keys.heading.building"},
          {KeyContext::Units, "keys.heading.units"}}) {
        if (!lines.empty()) {
            lines.push_back(Hud::HelpLine{});  // a blank line before the next heading
        }
        lines.push_back(Hud::HelpLine{"", ui::tr(heading)});
        std::vector<std::pair<KeyAction, std::string>> seen;  // one line per action, its keys joined
        for (const ui::KeyBinding& b : m_keys.all()) {
            if (b.context != context) {
                continue;
            }
            std::string key = key_label(b.chord);
            const bool group = b.action == KeyAction::GroupStore || b.action == KeyAction::GroupAdd ||
                               b.action == KeyAction::GroupRecall;
            if (group) {
                if (b.param != 0) {
                    continue;
                }
                key = key.substr(0, key.size() - 1) + ui::tr("keys.group_digits");  // the digits, 1 for group 1
            }
            const auto it = std::find_if(seen.begin(), seen.end(), [&](const auto& s) { return s.first == b.action; });
            if (it == seen.end()) {
                seen.emplace_back(b.action, key);
            } else if (it->second.find(key) == std::string::npos) {
                it->second += ", " + key;
            }
        }
        std::stable_sort(seen.begin(), seen.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [action, keys] : seen) {
            lines.push_back(Hud::HelpLine{keys, action_label(action)});
        }
    }
    m_hud->show_help(std::move(lines));
}

ui::KeyContext MapInput::key_context() const {
    if (m_tool.active() || (m_hud && m_hud->mode() == Hud::kModeBuild)) {
        return ui::KeyContext::Building;
    }
    if (!m_units.empty() && m_units.owner(world()) == local()) {
        return ui::KeyContext::Units;
    }
    return ui::KeyContext::General;
}

void MapInput::handle_key(int key, int scancode, std::uint16_t mod) {
    const ui::KeyPress press = key_press(key, scancode, mod);
    if (m_hud && m_hud->menu_active() && !m_hud->modal()) {
        // The menu knows Escape (a screen back) and the program's keys, nothing of the game.
        if (key == SDLK_ESCAPE) {
            (void)m_hud->back();
            return;
        }
        if (const auto b = m_keys.find_pressed(ui::KeyContext::General, press);
            b && (b->action == ui::KeyAction::Screenshot || b->action == ui::KeyAction::Quit ||
                  b->action == ui::KeyAction::Help)) {
            run_action(*b);
        }
        return;
    }
    if (m_hud && m_hud->end_screen_active() && (key == SDLK_LEFT || key == SDLK_RIGHT)) {
        m_hud->end_screen_step(key == SDLK_LEFT ? -1 : 1);
        return;
    }
    if (m_hud && m_hud->helping()) {
        m_hud->close_help();  // any key closes the key overview
        return;
    }
    if (m_hud && m_hud->asking()) {
        // The question: Enter answers yes, Escape no.
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_ESCAPE) {
            m_hud->close_question();
            Hud::Action a{Hud::Action::Kind::Answer};
            a.step = key == SDLK_ESCAPE ? 0 : 1;
            m_host.apply_hud(a);
        }
        return;
    }
    if (const auto b = m_keys.find_pressed(key_context(), press)) {
        run_action(*b);
    }
}

void MapInput::scroll_with_keys(std::uint32_t delta_ms) {
    if (m_hud && (m_hud->modal() || m_hud->typing())) {
        return;
    }
    const bool* state = SDL_GetKeyboardState(nullptr);
    const SDL_Keymod mod = SDL_GetModState();
    const bool fast = (mod & SDL_KMOD_SHIFT) != 0;
    const ui::KeyContext context = key_context();
    const auto held = [&](ui::KeyAction action) {
        for (const ui::KeyChord& k : m_keys.keys(action)) {
            // A key that means something else here does not scroll (none does with the defaults).
            const SDL_Scancode sc = k.physical ? static_cast<SDL_Scancode>(k.key)
                                               : SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(k.key), nullptr);
            const auto b = m_keys.find_pressed(
                context,
                key_press(static_cast<int>(SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, true)), static_cast<int>(sc), 0));
            if (b && b->action != action) {
                continue;
            }
            if (sc != SDL_SCANCODE_UNKNOWN && state[sc] && (mod & SDL_KMOD_CTRL) == 0) {
                return true;
            }
        }
        return false;
    };
    double dx = 0.0;
    double dy = 0.0;
    dx -= held(ui::KeyAction::ScrollLeft) ? 1.0 : 0.0;
    dx += held(ui::KeyAction::ScrollRight) ? 1.0 : 0.0;
    dy -= held(ui::KeyAction::ScrollUp) ? 1.0 : 0.0;
    dy += held(ui::KeyAction::ScrollDown) ? 1.0 : 0.0;
    if (dx != 0.0 || dy != 0.0) {
        const double distance = kScrollSpeed * (fast ? 3.0 : 1.0) * delta_ms / 1000.0;
        m_camera.scroll(dx * distance, dy * distance);
    }
}

void MapInput::run_action(const ui::KeyBinding& b) {
    using ui::KeyAction;
    std::int32_t w = 0;
    std::int32_t h = 0;
    m_window.output_size(w, h);
    const double cx = w / 2.0;
    const double cy = h / 2.0;
    const auto not_yet = [&](const char* what) { std::fprintf(stderr, "%s: comes with the interface\n", what); };
    switch (b.action) {
        case KeyAction::ScrollUp:
        case KeyAction::ScrollDown:
        case KeyAction::ScrollLeft:
        case KeyAction::ScrollRight:
            break;  // while held (scroll_with_keys)
        case KeyAction::RotateViewLeft:
        case KeyAction::RotateViewRight:
            m_camera.set_rotation(
                static_cast<std::uint8_t>(m_camera.view().rotation + (b.action == KeyAction::RotateViewLeft ? 3 : 1)),
                cx, cy);
            break;
        case KeyAction::ZoomIn:
            m_host.zoom_by(-1, cx, cy);
            break;
        case KeyAction::ZoomOut:
            m_host.zoom_by(1, cx, cy);
            break;
        case KeyAction::JumpToMessage:
            if (const auto t = m_hud ? m_hud->last_message_tile() : std::nullopt) {
                m_camera.center_on(render::TilePos{static_cast<std::uint32_t>(std::max(0.0, t->first)),
                                                   static_cast<std::uint32_t>(std::max(0.0, t->second))});
            }
            break;
        case KeyAction::CentreOnSelection:
            if (const auto c = m_units.centre(world())) {
                m_camera.center_on(render::TilePos{static_cast<std::uint32_t>(std::max(0.0, c->first)),
                                                   static_cast<std::uint32_t>(std::max(0.0, c->second))});
            } else {
                m_camera.center_on(m_host.home_tile());
            }
            break;
        case KeyAction::Pause:
            m_commands.submit(sim::GameSpeedOrder{
                local(), simulation().paused() ? sim::GameSpeedOrder::Op::Resume : sim::GameSpeedOrder::Op::Pause,
                simulation().speed()});
            break;
        case KeyAction::SpeedUp:
            m_commands.submit(
                sim::GameSpeedOrder{local(), sim::GameSpeedOrder::Op::SetSpeed,
                                    std::min(simulation().rules().max_game_speed, simulation().speed() * 2)});
            break;
        case KeyAction::SpeedDown:
            m_commands.submit(sim::GameSpeedOrder{local(), sim::GameSpeedOrder::Op::SetSpeed,
                                                  std::max(1U, simulation().speed() / 2)});
            break;
        case KeyAction::QuickSave:
            m_host.quick_save();
            break;
        case KeyAction::QuickLoad:
            m_host.quick_load();
            break;
        case KeyAction::GameMenu:
            not_yet("game menu");
            break;
        case KeyAction::Screenshot:  // U-002: the original's screenshot key does not work
            m_host.request_screenshot();
            break;
        case KeyAction::Help:
            show_key_help();
            break;
        case KeyAction::Cancel:
            cancel_action();
            break;
        case KeyAction::ModeBuild:
            set_mode(m_hud && m_hud->mode() == Hud::kModeBuild ? Hud::kModeInfo : Hud::kModeBuild);
            if (!m_hud) {
                m_tool.set_mode(m_tool.mode() == BuildTool::Mode::Build ? BuildTool::Mode::Off
                                                                        : BuildTool::Mode::Build);
            }
            break;
        case KeyAction::ModeMilitary:
            set_mode(Hud::kModeMilitary);
            break;
        case KeyAction::ModeInfo:
            set_mode(Hud::kModeInfo);
            break;
        case KeyAction::ModeOptions:
            set_mode(Hud::kModeOptions);
            break;
        case KeyAction::ShipList:
        case KeyAction::CityList:
            if (m_hud) {
                if (const auto a = m_hud->toggle_window(b.action == KeyAction::ShipList ? Selection::Kind::ShipList
                                                                                        : Selection::Kind::CityList)) {
                    m_host.apply_hud(*a);
                }
            }
            break;
        case KeyAction::Diplomacy:
            if (m_hud) {
                if (const auto a = m_hud->toggle_window(Selection::Kind::Diplomacy)) {
                    m_host.apply_hud(*a);
                }
            }
            break;
        case KeyAction::RotateBuilding:
            m_tool.rotate(1);
            break;
        case KeyAction::PickBuilding:
            // The pipette: the building under the mouse becomes the order.
            if (const auto spot = spot_at_mouse()) {
                if (const auto at = sim::building_at(world(), m_buildings, spot->island, {spot->x, spot->y})) {
                    set_mode(Hud::kModeBuild);
                    if (m_tool.select(at->type->id)) {
                        m_tool.set_mode(BuildTool::Mode::Build);
                        drop_units();
                    }
                }
            }
            break;
        case KeyAction::DemolishTool:
            if (m_hud && m_hud->mode() != Hud::kModeBuild) {
                set_mode(Hud::kModeBuild);
            }
            m_tool.set_mode(m_tool.mode() == BuildTool::Mode::Demolish ? BuildTool::Mode::Off
                                                                       : BuildTool::Mode::Demolish);
            if (m_tool.active()) {
                drop_units();
            }
            break;
        case KeyAction::NextBuilding:
            m_tool.cycle(b.param != 0 ? -1 : 1);
            break;
        case KeyAction::GroupStore:
            m_units.store_group(b.param);
            break;
        case KeyAction::GroupAdd:
            m_units.add_to_group(world(), b.param);
            break;
        case KeyAction::GroupRecall: {
            const bool twice = m_group_key.press(b.param, platform::ticks_ms());
            if (m_units.recall_group(world(), b.param)) {
                selection_changed(false);
                if (twice) {
                    run_action(ui::KeyBinding{ui::KeyContext::General, {}, KeyAction::CentreOnSelection, 0});
                }
            }
            break;
        }
        case KeyAction::NextIdleShip:
        case KeyAction::NextIdleUnit: {
            // The next of the player's ships (soldiers) without an order, after the selected one.
            const bool ships = b.action == KeyAction::NextIdleShip;
            std::vector<ui::UnitRef> idle;
            if (ships) {
                for (const sim::Ship& s : world().ships()) {
                    if (s.owner == local() && !s.sinking && s.target.empty() && !s.route.active) {
                        idle.push_back({ui::UnitRef::Kind::Ship, s.id});
                    }
                }
            } else {
                for (const sim::Soldier& s : world().soldiers()) {
                    if (s.owner == local() && !s.away() && s.target.empty() && !s.patrol.active) {
                        idle.push_back({ui::UnitRef::Kind::Soldier, s.id});
                    }
                }
            }
            const auto next = next_idle(
                idle, m_units.size() == 1 ? std::optional<ui::UnitRef>(m_units.units().front()) : std::nullopt);
            if (!next) {
                break;
            }
            m_units.select(world(), idle[*next]);
            selection_changed(false);
            run_action(ui::KeyBinding{ui::KeyContext::General, {}, KeyAction::CentreOnSelection, 0});
            break;
        }
        case KeyAction::NextKindInSelection:
            if (m_units.cycle_types(world())) {
                selection_changed(false);
            }
            break;
        case KeyAction::AttackMove:
            m_pick = Pick::Attack;
            break;
        case KeyAction::Stop:
            submit_orders(m_units.stop_orders(world(), local()), false);
            break;
        case KeyAction::HoldPosition:
            submit_orders(m_units.hold_orders(world(), local()), false);
            break;
        case KeyAction::Patrol:
            m_pick = Pick::Patrol;
            break;
        case KeyAction::Formation:
        case KeyAction::Scatter: {
            if (b.action == KeyAction::Formation) {
                m_units.formation(world(), m_figures, m_formation);
                m_formation = (m_formation + 1) % 3;
            } else {
                m_units.scatter(world());
            }
            // The group takes its places around where it stands (0x0044C6D0).
            if (const auto c = m_units.centre(world())) {
                submit_orders(m_units.move_orders(world(), local(), c->first, c->second, false));
            }
            break;
        }
        case KeyAction::WhiteFlag: {
            std::vector<sim::Command> orders;
            const bool on = !m_units.any_white_flag(world());
            for (const ui::UnitRef& r : m_units.units()) {
                if (r.kind == ui::UnitRef::Kind::Ship) {
                    sim::ShipCommand o;
                    o.player = local();
                    o.ship = r.id;
                    o.op = sim::ShipCommand::Op::WhiteFlag;
                    o.on = on;
                    orders.emplace_back(o);
                }
            }
            submit_orders(std::move(orders), false);
            break;
        }
        case KeyAction::Explore:
            if (m_units.size() == 1 && m_units.kind() == ui::UnitRef::Kind::Ship) {
                m_commands.submit(sim::ExploreOrder{local(), m_units.units().front().id});
            }
            break;
        case KeyAction::Trade:
        case KeyAction::EditRoute:
            set_mode(Hud::kModeInfo);  // the ship's window has trade and route
            break;
        case KeyAction::Delete: {
            std::vector<sim::Command> orders;
            for (const ui::UnitRef& r : m_units.units()) {
                if (r.kind == ui::UnitRef::Kind::Ship) {
                    sim::ShipCommand o;
                    o.player = local();
                    o.ship = r.id;
                    o.op = sim::ShipCommand::Op::Scuttle;
                    orders.emplace_back(o);
                } else {
                    sim::SoldierCommand o;
                    o.player = local();
                    o.soldier = r.id;
                    o.op = sim::SoldierCommand::Op::Disband;
                    orders.emplace_back(o);
                }
            }
            submit_orders(std::move(orders), false);
            break;
        }
        case KeyAction::PixelScaleUp:
            m_camera.set_pixel_scale(std::min(m_camera.pixel_scale() + 1, kMaxPixelScale), cx, cy);
            break;
        case KeyAction::PixelScaleDown:
            m_camera.set_pixel_scale(std::max(m_camera.pixel_scale() - 1, 1), cx, cy);
            break;
        case KeyAction::ShowOwners:
            m_host.toggle_owners();
            break;
        case KeyAction::Quit:
            m_host.quit();
            break;
    }
}

void MapInput::handle_event(const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
            if (m_hud && m_hud->typing()) {
                // A savegame's name is typed: the keys go there, not to the key table.
                if (event.key.key == SDLK_BACKSPACE) {
                    m_hud->type_key(8);
                } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
                    m_hud->type_key(13);
                } else if (event.key.key == SDLK_ESCAPE) {
                    m_hud->type_key(27);
                } else if (event.key.key == SDLK_TAB) {
                    m_hud->type_key(9);
                }
                break;
            }
            handle_key(static_cast<int>(event.key.key), static_cast<int>(event.key.scancode),
                       static_cast<std::uint16_t>(event.key.mod));
            break;
        case SDL_EVENT_TEXT_INPUT:
            if (m_hud && m_hud->typing() && event.text.text != nullptr) {
                m_hud->type_text(utf8_to_cp1252(event.text.text));
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            m_mouse_x = static_cast<double>(event.motion.x);
            m_mouse_y = static_cast<double>(event.motion.y);
            if (m_hud) {
                if (const auto a = m_hud->mouse(static_cast<std::int32_t>(event.motion.x),
                                                static_cast<std::int32_t>(event.motion.y), m_left_down)) {
                    m_host.apply_hud(*a);
                }
            }
            if (m_left_down) {
                m_press.move(static_cast<double>(event.motion.x), static_cast<double>(event.motion.y));
            }
            if (m_drag.move(static_cast<double>(event.motion.xrel), static_cast<double>(event.motion.yrel))) {
                m_camera.scroll(-static_cast<double>(event.motion.xrel), -static_cast<double>(event.motion.yrel));
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button == SDL_BUTTON_LEFT) {
                m_left_down = true;
            }
            if (m_hud && m_hud->modal()) {
                // The question (or the key overview) takes every click.
                if (event.button.button == SDL_BUTTON_LEFT) {
                    (void)m_hud->mouse(static_cast<std::int32_t>(event.button.x),
                                       static_cast<std::int32_t>(event.button.y), true);
                }
                break;
            }
            if (m_hud &&
                m_hud->covers(static_cast<std::int32_t>(event.button.x), static_cast<std::int32_t>(event.button.y))) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    m_sound.click();
                    if (const auto a = m_hud->mouse(static_cast<std::int32_t>(event.button.x),
                                                    static_cast<std::int32_t>(event.button.y), true)) {
                        m_host.apply_hud(*a);
                    }
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    // A right click on the minimap sends the selection there (ADR 0010), elsewhere on
                    // the panel it goes back to the player status.
                    if (const auto t = m_hud->minimap_tile(static_cast<std::int32_t>(event.button.x),
                                                           static_cast<std::int32_t>(event.button.y))) {
                        order_to_tile(t->first, t->second, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
                    } else {
                        (void)back_to_status();
                    }
                }
                break;  // the map does not see clicks on the frame
            }
            if (event.button.button == SDL_BUTTON_LEFT && m_hud && m_hud->picking()) {
                // A window waits for a place on the map (a unit's destination).
                if (const auto tile = m_camera.tile_at_screen(m_mouse_x, m_mouse_y)) {
                    m_hud->map_click(static_cast<std::uint32_t>(tile->x), static_cast<std::uint32_t>(tile->y));
                }
                break;
            }
            if (event.button.button == SDL_BUTTON_RIGHT || event.button.button == SDL_BUTTON_MIDDLE) {
                m_drag.press();
            } else if (event.button.button == SDL_BUTTON_LEFT && m_tool.active()) {
                if (const auto spot = spot_at_mouse()) {
                    m_tool.press(*spot);
                }
            } else if (event.button.button == SDL_BUTTON_LEFT) {
                m_press.press(static_cast<double>(event.button.x), static_cast<double>(event.button.y));
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                m_left_down = false;
                if (m_hud) {
                    if (const auto a = m_hud->mouse(static_cast<std::int32_t>(event.button.x),
                                                    static_cast<std::int32_t>(event.button.y), false)) {
                        m_host.apply_hud(*a);
                    }
                }
                if (m_press.at()) {
                    map_left_up(static_cast<double>(event.button.x), static_cast<double>(event.button.y),
                                static_cast<std::uint16_t>(SDL_GetModState()), event.button.clicks);
                    m_press.reset();
                }
            }
            if (event.button.button == SDL_BUTTON_RIGHT || event.button.button == SDL_BUTTON_MIDDLE) {
                // A right click (not a drag): an order (ADR 0010), or back with the original's mouse.
                if (m_drag.release() && event.button.button == SDL_BUTTON_RIGHT && !(m_hud && m_hud->modal())) {
                    if (m_original_mouse) {
                        cancel_action();
                    } else if (!m_units.empty() && cancel_build_step()) {
                        // A building in hand goes first, even with a ship selected (the Kontor to
                        // found from it); the ship stays selected and does not sail off.
                        while (cancel_build_step()) {
                        }
                    } else if (!m_units.empty() || !back_to_status()) {
                        map_order(static_cast<double>(event.button.x), static_cast<double>(event.button.y),
                                  (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
                    }
                }
            } else if (event.button.button == SDL_BUTTON_LEFT && m_tool.dragging()) {
                if (const auto spot = spot_at_mouse()) {
                    if (auto order = m_tool.build_order(world(), local(), *spot)) {
                        // A Kontor founded from a ship is the only one there: the tool is put away
                        // once it can be built (the ship's window gave it).
                        const bool founded =
                            order->ship && std::ranges::all_of(sim::plan_build(world(), m_buildings, *order).items,
                                                               [](const sim::PlannedBuild& i) { return i.ok(); });
                        m_commands.submit(std::move(*order));
                        if (founded) {
                            m_tool.set_mode(BuildTool::Mode::Off);
                        }
                    } else if (auto demolish = m_tool.demolish_order(local(), *spot)) {
                        m_commands.submit(*demolish);
                    }
                }
                m_tool.release();
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL: {  // U-001: the original has no wheel zoom
            const float y = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
            if (y != 0.0F) {
                m_host.zoom_by(y > 0.0F ? -1 : 1, static_cast<double>(event.wheel.mouse_x),
                               static_cast<double>(event.wheel.mouse_y));
            }
            break;
        }
        default:
            break;
    }
}

void MapInput::sync_text_input() {
    // SDL sends no text (SDL_EVENT_TEXT_INPUT) without it.
    const bool typing = m_hud && m_hud->typing();
    if (typing == m_text_input) {
        return;
    }
    m_text_input = typing;
    if (typing) {
        SDL_StartTextInput(m_window.sdl_window());
    } else {
        SDL_StopTextInput(m_window.sdl_window());
    }
}

void MapInput::follow_selection() {
    // A window changed the selection (filters, formations): the windows follow.
    if (m_units.take_changed()) {
        selection_changed(false);
    }
    // Units that died, sank or changed hands leave the selection.
    if (m_units.prune(world())) {
        selection_changed(false);
    }
}

void MapInput::reset_game() {
    m_units.reset();
    m_pending_orders.clear();
    m_pick = Pick::None;
    m_press.reset();
    m_group_key.reset();
    m_formation = 0;
    m_field_source.reset();
}

void MapInput::set_mode(std::int32_t mode) {
    if (m_hud) {
        if (const auto a = m_hud->set_mode(mode)) {
            m_host.apply_hud(*a);
        }
    }
}

std::optional<BuildTool::Spot> MapInput::spot_at_mouse() const {
    const auto tile = m_camera.tile_at_screen(m_mouse_x, m_mouse_y);
    if (!tile) {
        return std::nullopt;
    }
    const auto& cell = world().cell(tile->x, tile->y);
    if (cell.island == sim::World::kSea) {
        return std::nullopt;
    }
    return BuildTool::Spot{cell.island, cell.local_x, cell.local_y};
}

void MapInput::select_building_at_mouse() {
    if (!m_hud) {
        return;
    }
    const sim::World& w = world();
    const assets::BuildingTable& buildings = m_buildings;
    Selection sel;
    {
        // A building: its origin, so that every tile of it selects the same. Buildings stand above
        // their tiles, so a click on the picture over ground looks a little further down the
        // screen for the tiles they stand on.
        const auto selectable = [](const assets::BuildingType& b) {
            const std::int32_t k = b.prod_kind;
            return (k >= sim::prod::kWorkshop && k <= sim::prod::kKontor) ||
                   (k >= sim::prod::kHouse && k <= sim::prod::kPublicLast) || k == sim::prod::kHaupt;
        };
        const double step = render::tile_height(m_camera.view().zoom) * m_camera.pixel_scale() / 2.0;
        for (int i = 0; i <= 4 && sel.kind == Selection::Kind::None; ++i) {
            const auto tile = m_camera.tile_at_screen(m_mouse_x, m_mouse_y + step * i);
            if (!tile || tile->x >= w.width() || tile->y >= w.height()) {
                continue;
            }
            const sim::World::Cell& cell = w.cell(tile->x, tile->y);
            if (cell.island == sim::World::kSea) {
                continue;
            }
            const sim::IslandMap& map = w.islands()[cell.island].map;
            const sim::SubTile t = map.sub_tile(buildings, cell.local_x, cell.local_y);
            if (t.building == nullptr || !map.contains(t.origin_x, t.origin_y) || !selectable(*t.building)) {
                continue;  // ground, fields, roads: nothing to show, look further down
            }
            sel.kind = Selection::Kind::Building;
            sel.island = cell.island;
            sel.x = static_cast<std::uint16_t>(t.origin_x);
            sel.y = static_cast<std::uint16_t>(t.origin_y);
        }
    }
    const Selection& old = m_hud->selection();
    sel.again = sel.kind == Selection::Kind::Building && old.kind == Selection::Kind::Building &&
                old.island == sel.island && old.x == sel.x && old.y == sel.y;
    m_hud->set_selection(sel);
    m_field_source.reset();
    if (sel.kind == Selection::Kind::Building) {
        // Looking at a farm, a plantation or a forester picks its field in the build menu
        // (BuildMenu_SelectFarmByWare 0x004331B0): the field whose grown stage gives the raw material.
        const sim::SubTile t = w.islands()[sel.island].map.sub_tile(buildings, sel.x, sel.y);
        if (t.building != nullptr && t.building->raw_ware != 0) {
            for (const assets::BuildingType& field : buildings.types()) {
                const assets::BuildingType* ripe =
                    field.prod_kind == sim::prod::kGrowing ? sim::ripe_record(buildings, field) : nullptr;
                if (ripe != nullptr && ripe->ware == t.building->raw_ware && m_hud->show_in_build_menu(field.id)) {
                    m_field_source = FieldSource{sel.island, sel.x, sel.y, field.id};
                    break;
                }
            }
        }
    }
}

void MapInput::drop_units() {
    if (!m_units.empty()) {
        m_units.clear();
        selection_changed(false);
    }
}

bool MapInput::cancel_build_step() {
    if (m_hud && m_hud->close_build_popup()) {
        return true;
    }
    if (m_tool.active()) {
        m_tool.set_mode(BuildTool::Mode::Off);
        return true;
    }
    return false;
}

bool MapInput::back_to_status() {
    if (!m_hud || m_hud->menu_active()) {
        return false;
    }
    // Everything at once, so one click is enough: the build menu's pop-up and the building in
    // hand, the pages of a window (tribute -> treaties, a ship's trade page) or of the settings,
    // another mode, the selection.
    bool changed = false;
    while (cancel_build_step()) {
        changed = true;
    }
    for (int page = 0; page < kMaxBackSteps && m_hud->back(); ++page) {
        changed = true;
    }
    if (m_hud->mode() != Hud::kModeInfo) {
        set_mode(Hud::kModeInfo);
        changed = true;
    }
    if (!m_units.empty()) {
        m_units.clear();
        selection_changed(false);
        changed = true;
    }
    if (m_hud->selection().kind != Selection::Kind::None) {
        m_hud->set_selection(Selection{});
        changed = true;
    }
    return changed;
}

std::pair<double, double> MapInput::screen_of(double tx, double ty) const {
    // Tile centres lie on a lattice: interpolate between three of them.
    const render::WorldView& view = m_camera.view();
    std::int32_t x0 = 0;
    std::int32_t y0 = 0;
    std::int32_t x1 = 0;
    std::int32_t y1 = 0;
    std::int32_t x2 = 0;
    std::int32_t y2 = 0;
    render::world_tile_center(view, 0, 0, x0, y0);
    render::world_tile_center(view, 1, 0, x1, y1);
    render::world_tile_center(view, 0, 1, x2, y2);
    const double fx = tx - 0.5;
    const double fy = ty - 0.5;
    const double px = x0 + fx * (x1 - x0) + fy * (x2 - x0);
    const double py = y0 + fx * (y1 - y0) + fy * (y2 - y0);
    double sx = 0.0;
    double sy = 0.0;
    m_camera.world_to_screen(px, py, sx, sy);
    return {sx, sy};
}

std::optional<std::array<double, 4>> MapInput::unit_box(const ui::UnitRef& ref) const {
    const ZoomGraphics* graphics = m_host.graphics();
    if (const auto sprites = m_host.unit_sprites(ref); sprites && graphics) {
        // The pictures as drawn: world pixels * scale - origin.
        std::int32_t ox = 0;
        std::int32_t oy = 0;
        m_camera.screen_origin(ox, oy);
        const double k = m_camera.pixel_scale();
        std::optional<std::array<std::int32_t, 4>> u;
        for (std::uint8_t i = 0; i < sprites->count; ++i) {
            const auto b = render::command_bounds(sprites->parts[i], graphics->graphics.atlas);
            if (!b) {
                continue;
            }
            if (!u) {
                u = b;
                continue;
            }
            const std::int32_t x0 = std::min((*u)[0], (*b)[0]);
            const std::int32_t y0 = std::min((*u)[1], (*b)[1]);
            const std::int32_t x1 = std::max((*u)[0] + (*u)[2], (*b)[0] + (*b)[2]);
            const std::int32_t y1 = std::max((*u)[1] + (*u)[3], (*b)[1] + (*b)[3]);
            u = std::array<std::int32_t, 4>{x0, y0, x1 - x0, y1 - y0};
        }
        if (u) {
            return std::array<double, 4>{(*u)[0] * k - ox, (*u)[1] * k - oy, (*u)[2] * k, (*u)[3] * k};
        }
    }
    // No graphics yet: an estimate around the unit's position.
    const auto pos = ui::unit_position(world(), ref);
    if (!pos) {
        return std::nullopt;
    }
    const auto [sx, sy] = screen_of(pos->first, pos->second);
    const double tw = render::tile_width(m_camera.view().zoom) * m_camera.pixel_scale();
    if (ref.kind == ui::UnitRef::Kind::Ship) {
        const double w = 1.4 * tw;
        const double h = 1.5 * tw;
        return std::array<double, 4>{sx - w / 2, sy - h * 0.85, w, h};
    }
    const double w = 0.45 * tw;
    const double h = 0.75 * tw;
    return std::array<double, 4>{sx - w / 2, sy - h * 0.9, w, h};
}

std::vector<ui::UnitCandidate> MapInput::units_in(double x0, double y0, double x1, double y1) const {
    const double left = std::min(x0, x1);
    const double right = std::max(x0, x1);
    const double top = std::min(y0, y1);
    const double bottom = std::max(y0, y1);
    std::vector<ui::UnitCandidate> out;
    const auto test = [&](const ui::UnitRef& ref) {
        const auto b = unit_box(ref);
        if (!b) {
            return;
        }
        const double w = std::min(right, (*b)[0] + (*b)[2]) - std::max(left, (*b)[0]);
        const double h = std::min(bottom, (*b)[1] + (*b)[3]) - std::max(top, (*b)[1]);
        if (w > 0.0 && h > 0.0) {
            out.push_back({ref, static_cast<std::int64_t>(w * h)});
        }
    };
    for (const sim::Ship& s : world().ships()) {
        if (!s.sinking) {
            test({ui::UnitRef::Kind::Ship, s.id});
        }
    }
    for (const sim::Soldier& s : world().soldiers()) {
        if (!s.away()) {
            test({ui::UnitRef::Kind::Soldier, s.id});
        }
    }
    return out;
}

std::optional<ui::UnitRef> MapInput::unit_at(double sx, double sy) const {
    // Pixel exact (Map_PickAtScreen 0x00487940): the unit whose visible pixel is under the mouse,
    // the one drawn last if several overlap.
    const ZoomGraphics* graphics = m_host.graphics();
    if (graphics) {
        std::int32_t ox = 0;
        std::int32_t oy = 0;
        m_camera.screen_origin(ox, oy);
        const double k = m_camera.pixel_scale();
        const auto wx = static_cast<std::int32_t>(std::floor((sx + ox) / k));
        const auto wy = static_cast<std::int32_t>(std::floor((sy + oy) / k));
        std::optional<ui::UnitRef> best;
        std::tuple<std::int32_t, std::int32_t, int> best_order{};
        for (const ui::UnitCandidate& c : units_in(sx, sy, sx + 1.0, sy + 1.0)) {
            const auto sprites = m_host.unit_sprites(c.ref);
            if (!sprites) {
                continue;
            }
            bool hit = false;
            for (std::uint8_t i = 0; i < sprites->count && !hit; ++i) {
                hit = render::command_hit(sprites->parts[i], graphics->graphics.atlas, graphics->hit_mask, wx, wy);
            }
            // Drawn by row and depth; land units after ships on the same place.
            const std::tuple<std::int32_t, std::int32_t, int> order{sprites->row, sprites->depth,
                                                                    c.ref.kind == ui::UnitRef::Kind::Soldier ? 1 : 0};
            if (hit && (!best || order > best_order)) {
                best = c.ref;
                best_order = order;
            }
        }
        if (best) {
            return best;
        }
    }
    // Small figures (zoomed out) are hard to hit: then the nearest whose picture box is within a
    // few pixels, land units and the player's own first.
    const double slack = 3.0 * m_camera.pixel_scale();
    std::optional<ui::UnitRef> best;
    double best_d = 0.0;
    bool best_own = false;
    for (const ui::UnitCandidate& c : units_in(sx - slack, sy - slack, sx + slack, sy + slack)) {
        const auto b = unit_box(c.ref);
        if (!b || (*b)[2] * (*b)[3] > 400.0 * m_camera.pixel_scale() * m_camera.pixel_scale()) {
            continue;  // only small pictures; a large one is hit by its pixels
        }
        const double dx = (*b)[0] + (*b)[2] / 2 - sx;
        const double dy = (*b)[1] + (*b)[3] / 2 - sy;
        const double d = dx * dx + dy * dy;
        const bool own = ui::unit_owner(world(), c.ref) == local();
        const double rank = d - (c.ref.kind == ui::UnitRef::Kind::Soldier ? 1e6 : 0.0);
        if (!best || (own && !best_own) || (own == best_own && rank < best_d)) {
            best = c.ref;
            best_d = rank;
            best_own = own;
        }
    }
    return best;
}

void MapInput::map_left_up(double x, double y, std::uint16_t mod, int clicks) {
    const bool shift = (mod & SDL_KMOD_SHIFT) != 0;
    const bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
    if (m_pick != Pick::None) {
        map_order(x, y, shift);
        return;
    }
    if (m_press.rubber_band() && m_press.at()) {
        m_units.box(world(), units_in(m_press.at()->first, m_press.at()->second, x, y), local(), shift);
        selection_changed(true);
        return;
    }
    if (const auto unit = unit_at(x, y)) {
        if (ctrl || clicks >= 2) {
            // All of this kind on screen.
            const ui::Rect v = m_hud ? m_hud->game_view() : ui::Rect{0, 0, 1 << 15, 1 << 15};
            m_units.select_alike(world(), units_in(v.x, v.y, v.x + v.w, v.y + v.h), *unit);
        } else if (shift) {
            m_units.toggle(world(), *unit);
        } else {
            m_units.select(world(), *unit);
        }
        selection_changed(false);
        return;
    }
    if (m_original_mouse && !m_units.empty() && m_units.owner(world()) == local()) {
        map_order(x, y, shift);
        return;
    }
    m_units.clear();
    selection_changed(false);
    select_building_at_mouse();
}

void MapInput::map_order(double x, double y, bool queue) {
    const Pick pick = std::exchange(m_pick, Pick::None);
    if (m_units.empty() || m_units.owner(world()) != local()) {
        return;
    }
    const auto tile = m_camera.tile_at_screen(x, y);
    if (pick == Pick::Patrol) {
        if (!tile) {
            return;
        }
        // Back and forth between where the units are and the click (0x00440BD0).
        std::vector<sim::Command> orders;
        for (const ui::UnitRef& r : m_units.units()) {
            if (r.kind == ui::UnitRef::Kind::Ship) {
                sim::ShipCommand o;
                o.player = local();
                o.ship = r.id;
                o.op = sim::ShipCommand::Op::Patrol;
                o.x = static_cast<std::uint16_t>(tile->x);
                o.y = static_cast<std::uint16_t>(tile->y);
                orders.emplace_back(o);
            } else if (const sim::Soldier* s = world().soldier(r.id)) {
                sim::SoldierCommand o;
                o.player = local();
                o.soldier = r.id;
                o.op = sim::SoldierCommand::Op::SetWaypoint;
                o.index = 0;
                o.point =
                    sim::Target::spot(static_cast<std::uint32_t>(s->cell_x()), static_cast<std::uint32_t>(s->cell_y()));
                orders.emplace_back(o);
                o.index = 1;
                o.point = sim::Target::tile_spot(tile->x, tile->y);
                orders.emplace_back(o);
                o.op = sim::SoldierCommand::Op::Patrol;
                o.on = true;
                orders.emplace_back(o);
            }
        }
        submit_orders(std::move(orders));
        return;
    }
    const bool attack_pick = pick == Pick::Attack;
    // Onto a unit: attack somebody else's, follow or board one's own.
    if (const auto unit = unit_at(x, y); unit && !m_units.contains(*unit)) {
        const sim::Target t =
            unit->kind == ui::UnitRef::Kind::Ship ? sim::Target::other_ship(unit->id) : sim::Target::soldier(unit->id);
        const bool foreign = ui::unit_owner(world(), *unit) != local();
        submit_orders(m_units.target_orders(world(), local(), t, queue, foreign || attack_pick));
        return;
    }
    if (!tile) {
        return;
    }
    const sim::World::Cell& cell = world().cell(tile->x, tile->y);
    if (cell.island != sim::World::kSea) {
        if (m_units.kind() == ui::UnitRef::Kind::Ship) {
            // A ship to a Kontor: its city.
            for (const sim::City& c : world().cities()) {
                if (c.island != cell.island || !c.warehouse_x || !c.warehouse_y) {
                    continue;
                }
                const auto b = sim::building_at(
                    world(), m_buildings, c.island,
                    {static_cast<std::int32_t>(*c.warehouse_x), static_cast<std::int32_t>(*c.warehouse_y)});
                const auto here = sim::building_at(
                    world(), m_buildings, cell.island,
                    {static_cast<std::int32_t>(cell.local_x), static_cast<std::int32_t>(cell.local_y)});
                if (b && here && b->origin.x == here->origin.x && b->origin.y == here->origin.y) {
                    const sim::Target t = sim::Target::city_of(c.island, c.index);
                    submit_orders(m_units.target_orders(world(), local(), t, queue, attack_pick && c.owner != local()));
                    return;
                }
            }
        } else if (const auto b = sim::building_at(
                       world(), m_buildings, cell.island,
                       {static_cast<std::int32_t>(cell.local_x), static_cast<std::int32_t>(cell.local_y)});
                   b && b->type->kind != sim::building_kind::kGround && b->type->kind != sim::building_kind::kRoad &&
                   !sim::soldiers_walk_on(*b->type)) {
            // Land units onto a building: attack another's, walk to (or heal at) one's own.
            const sim::Target t = sim::Target::building(cell.island, static_cast<std::uint32_t>(b->origin.x),
                                                        static_cast<std::uint32_t>(b->origin.y));
            const bool foreign = target_owner(world(), t) != local() && target_owner(world(), t) != sim::kNoPlayer;
            submit_orders(m_units.target_orders(world(), local(), t, queue, foreign || attack_pick));
            return;
        }
    }
    // With F first: an attack move (fights on the way, ADR 0010).
    submit_orders(m_units.move_orders(world(), local(), tile->x + 0.5, tile->y + 0.5, queue, attack_pick));
}

void MapInput::order_to_tile(double tx, double ty, bool queue) {
    if (m_units.empty() || m_units.owner(world()) != local()) {
        return;
    }
    submit_orders(m_units.move_orders(world(), local(), tx, ty, queue));
}

void MapInput::cancel_action() {
    if (m_pick != Pick::None) {
        m_pick = Pick::None;
        return;
    }
    if (m_hud && m_hud->back()) {
        return;  // a window went back a page or stopped picking
    }
    if (m_hud && m_hud->mode() == Hud::kModeBuild) {
        set_mode(Hud::kModeInfo);
        return;
    }
    if (m_tool.active()) {
        m_tool.set_mode(BuildTool::Mode::Off);
        return;
    }
    if (!m_units.empty()) {
        m_units.clear();
        selection_changed(false);
        return;
    }
    if (m_hud && m_hud->selection().kind != Selection::Kind::None) {
        m_hud->set_selection(Selection{});
        return;
    }
    if (m_hud && m_hud->mode() == Hud::kModeMilitary) {
        set_mode(Hud::kModeInfo);
    }
}

void MapInput::selection_changed(bool box) {
    if (!m_hud) {
        return;
    }
    Selection sel;
    if (m_units.size() == 1 && m_units.kind() == ui::UnitRef::Kind::Ship) {
        sel.kind = Selection::Kind::Ship;
        sel.ship = m_units.units().front().id;
    }
    m_hud->set_selection(sel);
    // 0x0040AFF0: in the info mode a group (by rectangle) or a land unit goes to the military mode.
    if (!m_units.empty() && m_hud->mode() != Hud::kModeMilitary && m_hud->mode() != Hud::kModeBuild &&
        ((box && m_units.size() >= 2) || m_units.kind() == ui::UnitRef::Kind::Soldier)) {
        set_mode(Hud::kModeMilitary);
    }
}

void MapInput::submit_orders(std::vector<sim::Command> orders, bool sound) {
    submit(std::move(orders), sound, m_confirm);
}

void MapInput::answer(bool yes) {
    if (yes) {
        submit(std::move(m_pending_orders), true, false);
    }
    m_pending_orders.clear();
}

void MapInput::submit(std::vector<sim::Command> orders, bool sound, bool ask) {
    if (orders.empty()) {
        return;
    }
    if (ask && m_hud) {
        for (const sim::Command& c : orders) {
            if (const auto q = order_question(world(), m_figures, local(), c)) {
                m_pending_orders = std::move(orders);
                m_hud->ask(*q);
                return;
            }
        }
    }
    bool attack = false;
    for (const sim::Command& c : orders) {
        if (const auto* o = std::get_if<sim::ShipOrder>(&c)) {
            attack = attack || o->attack;
        } else if (const auto* so = std::get_if<sim::SoldierOrder>(&c)) {
            attack = attack || so->attack;
        }
    }
    for (sim::Command& c : orders) {
        m_commands.submit(std::move(c));
    }
    // One acknowledgement of the first unit (0x0044D540).
    if (sound && !m_units.empty()) {
        if (const auto pos = ui::unit_position(world(), m_units.units().front())) {
            m_sound.order(m_units.kind() == ui::UnitRef::Kind::Ship, attack, pos->first, pos->second);
        }
    }
}

void MapInput::draw_overlay() {
    SDL_Renderer* r = m_window.sdl_renderer();
    const ZoomGraphics* graphics = m_host.graphics();
    const std::int32_t k = m_camera.pixel_scale();
    std::int32_t ox = 0;
    std::int32_t oy = 0;
    m_camera.screen_origin(ox, oy);
    const std::int32_t tw = render::tile_width(m_camera.view().zoom);
    const std::optional<std::uint32_t> numbers =
        graphics ? graphics->graphics.sheet_base(assets::FigureSheet::Numbers) : std::nullopt;
    std::vector<render::DrawCommand> digits;
    for (const ui::UnitRef& ref : m_units.units()) {
        const auto b = unit_box(ref);
        const auto health = ui::unit_health(world(), m_figures, ref);
        if (!b || !health) {
            continue;
        }
        // Figure_Draw 0x0044FA90, in world pixels: a green and red bar right above the picture
        // (ship: flag or hull), centred on the unit; its group's number (NUMBERS.BSH, sprite
        // group + 1) 2 pixels right of the bar, half the bar's height higher.
        const bool ship = ref.kind == ui::UnitRef::Kind::Ship;
        const std::int32_t w = (ship ? 76 : 51) * tw / 128;
        const std::int32_t h = std::max(1, (ship ? 15 : 10) * tw / 128);
        double centre = (*b)[0] + (*b)[2] / 2;
        if (const auto sprites = m_host.unit_sprites(ref); sprites && graphics) {
            if (const auto body = render::command_bounds(sprites->parts[0], graphics->graphics.atlas)) {
                centre = ((*body)[0] + (*body)[2] / 2.0) * k - ox;
            }
        }
        const auto left = static_cast<std::int32_t>(std::floor((centre + ox) / k)) - w / 2;
        const auto top = static_cast<std::int32_t>(std::floor(((*b)[1] + oy) / k)) - h;
        const auto filled = static_cast<std::int32_t>(w * *health);
        const auto rect = [&](std::int32_t x, std::int32_t width) {
            return SDL_FRect{static_cast<float>(x * k - ox), static_cast<float>(top * k - oy),
                             static_cast<float>(width * k), static_cast<float>(h * k)};
        };
        const SDL_FRect green = rect(left, filled);
        const SDL_FRect red = rect(left + filled, w - filled);
        SDL_SetRenderDrawColor(r, 0, 255, 0, 255);
        SDL_RenderFillRect(r, &green);
        SDL_SetRenderDrawColor(r, 255, 0, 0, 255);
        SDL_RenderFillRect(r, &red);
        if (const auto g = m_units.group_of(ref)) {
            const auto sprite = static_cast<std::uint32_t>((*g + 1) % 10);
            if (numbers && graphics && !graphics->graphics.atlas.rect(*numbers + sprite).empty()) {
                render::DrawCommand c;
                c.sprite = *numbers + sprite;
                c.x = left + w + 2;
                c.y = top - h / 2 + graphics->graphics.atlas.rect(c.sprite).height;  // left / bottom anchor
                digits.push_back(c);
            } else {
                const char digit[2] = {static_cast<char>('0' + sprite), 0};
                SDL_SetRenderDrawColor(r, 0, 255, 255, 255);
                SDL_RenderDebugText(r, static_cast<float>((left + w + 2) * k - ox),
                                    static_cast<float>((top - h / 2) * k - oy), digit);
            }
        }
    }
    if (!digits.empty()) {
        graphics->renderer->draw(digits, ox, oy, k);
    }
    // The selection rectangle: a thin white outline (0x0040CAC0).
    if (const auto& at = m_press.at(); m_press.rubber_band() && at) {
        const SDL_FRect band{
            static_cast<float>(std::min(at->first, m_mouse_x)), static_cast<float>(std::min(at->second, m_mouse_y)),
            static_cast<float>(std::abs(m_mouse_x - at->first)), static_cast<float>(std::abs(m_mouse_y - at->second))};
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderRect(r, &band);
    }
}

}  // namespace o1602::app
