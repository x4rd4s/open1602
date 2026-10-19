// SPDX-License-Identifier: GPL-3.0-or-later
//
// Input of the map view (ADR 0010): the key table, selecting ships and land units with the left
// button (click, rectangle, Shift, Ctrl, double click), orders with the right button (Shift: queued;
// also on the minimap), picks for attack and patrol, and the questions before what cannot be taken
// back. The original's mouse (orders with the left button) is an option.
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>
#include <string_view>
#include <tuple>

#include "map_viewer.hpp"
#include "order_questions.hpp"

#include "o1602/core/file.hpp"
#include "o1602/render/island_renderer.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

constexpr std::uint64_t kDoubleGroupMs = 500;  // a group key twice in this time goes there
constexpr double kScrollSpeed = 900.0;         // pixels per second

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

void MapViewer::setup_keys() {
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

void MapViewer::load_key_settings() {
    const std::filesystem::path path = m_options.keys_file.empty() ? data_folder() / "keys.cfg" : m_options.keys_file;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (!m_options.keys_file.empty()) {
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

void MapViewer::show_key_help() {
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

ui::KeyContext MapViewer::key_context() const {
    if (m_tool.active() || (m_hud && m_hud->mode() == Hud::kModeBuild)) {
        return ui::KeyContext::Building;
    }
    if (!m_units.empty() && m_units.owner(m_state->world) == m_local_player) {
        return ui::KeyContext::Units;
    }
    return ui::KeyContext::General;
}

void MapViewer::handle_key(int key, int scancode, std::uint16_t mod) {
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
            apply_hud(a);
        }
        return;
    }
    if (const auto b = m_keys.find_pressed(key_context(), press)) {
        run_action(*b);
    }
}

void MapViewer::scroll_with_keys(std::uint32_t delta_ms) {
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

void MapViewer::run_action(const ui::KeyBinding& b) {
    using ui::KeyAction;
    std::int32_t w = 0;
    std::int32_t h = 0;
    m_window->output_size(w, h);
    const double cx = w / 2.0;
    const double cy = h / 2.0;
    const auto set_mode = [&](std::int32_t mode) {
        if (m_hud) {
            if (const auto a = m_hud->set_mode(mode)) {
                apply_hud(*a);
            }
        }
    };
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
            zoom_by(-1, cx, cy);
            break;
        case KeyAction::ZoomOut:
            zoom_by(1, cx, cy);
            break;
        case KeyAction::JumpToMessage:
            if (const auto t = m_hud ? m_hud->last_message_tile() : std::nullopt) {
                m_camera.center_on(render::TilePos{static_cast<std::uint32_t>(std::max(0.0, t->first)),
                                                   static_cast<std::uint32_t>(std::max(0.0, t->second))});
            }
            break;
        case KeyAction::CentreOnSelection:
            if (const auto c = m_units.centre(m_state->world)) {
                m_camera.center_on(render::TilePos{static_cast<std::uint32_t>(std::max(0.0, c->first)),
                                                   static_cast<std::uint32_t>(std::max(0.0, c->second))});
            } else {
                m_camera.center_on(home_tile());
            }
            break;
        case KeyAction::Pause:
            submit(sim::GameSpeedOrder{
                m_local_player,
                m_state->simulation.paused() ? sim::GameSpeedOrder::Op::Resume : sim::GameSpeedOrder::Op::Pause,
                m_state->simulation.speed()});
            break;
        case KeyAction::SpeedUp:
            submit(sim::GameSpeedOrder{
                m_local_player, sim::GameSpeedOrder::Op::SetSpeed,
                std::min(m_state->simulation.rules().max_game_speed, m_state->simulation.speed() * 2)});
            break;
        case KeyAction::SpeedDown:
            submit(sim::GameSpeedOrder{m_local_player, sim::GameSpeedOrder::Op::SetSpeed,
                                       std::max(1U, m_state->simulation.speed() / 2)});
            break;
        case KeyAction::QuickSave: {
            const std::filesystem::path path = save_folder() / ("quicksave" + std::string(sim::kSaveExtension));
            const auto status = save_game(path);
            notify(status ? ui::tr("notice.game_saved", {utf8_to_cp1252(core::path_to_utf8(path.filename()))})
                          : ui::tr("notice.save_failed", {ui::error_text(status.error(), "save")}));
            break;
        }
        case KeyAction::QuickLoad: {
            const std::filesystem::path path = save_folder() / ("quicksave" + std::string(sim::kSaveExtension));
            const auto status = load_game(path);
            if (!status) {
                notify(ui::tr("notice.load_failed", {ui::error_text(status.error(), "load")}));
            } else if (m_load_problems == 0) {  // else the note says what went wrong while loading
                notify(ui::tr("notice.game_loaded", {utf8_to_cp1252(core::path_to_utf8(path.filename()))}));
            }
            break;
        }
        case KeyAction::GameMenu:
            not_yet("game menu");
            break;
        case KeyAction::Screenshot:  // U-002: the original's screenshot key does not work
            // Read in the next present(), when the frame is complete.
            m_screenshot = ScreenshotRequest{next_screenshot_path(), true};
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
                    apply_hud(*a);
                }
            }
            break;
        case KeyAction::Diplomacy:
            if (m_hud) {
                if (const auto a = m_hud->toggle_window(Selection::Kind::Diplomacy)) {
                    apply_hud(*a);
                }
            }
            break;
        case KeyAction::RotateBuilding:
            m_tool.rotate(1);
            break;
        case KeyAction::PickBuilding:
            // The pipette: the building under the mouse becomes the order.
            if (const auto spot = spot_at_mouse()) {
                if (const auto at = sim::building_at(m_state->world, m_buildings, spot->island, {spot->x, spot->y})) {
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
            m_units.add_to_group(m_state->world, b.param);
            break;
        case KeyAction::GroupRecall: {
            const std::uint64_t now = platform::ticks_ms();
            if (m_units.recall_group(m_state->world, b.param)) {
                selection_changed(false);
                if (m_last_group == b.param && now - m_last_group_ms < kDoubleGroupMs) {
                    run_action(ui::KeyBinding{ui::KeyContext::General, {}, KeyAction::CentreOnSelection, 0});
                }
            }
            m_last_group = b.param;
            m_last_group_ms = now;
            break;
        }
        case KeyAction::NextIdleShip:
        case KeyAction::NextIdleUnit: {
            // The next of the player's ships (soldiers) without an order, after the selected one.
            const bool ships = b.action == KeyAction::NextIdleShip;
            std::vector<ui::UnitRef> idle;
            if (ships) {
                for (const sim::Ship& s : m_state->world.ships()) {
                    if (s.owner == m_local_player && !s.sinking && s.target.empty() && !s.route.active) {
                        idle.push_back({ui::UnitRef::Kind::Ship, s.id});
                    }
                }
            } else {
                for (const sim::Soldier& s : m_state->world.soldiers()) {
                    if (s.owner == m_local_player && !s.away() && s.target.empty() && !s.patrol.active) {
                        idle.push_back({ui::UnitRef::Kind::Soldier, s.id});
                    }
                }
            }
            if (idle.empty()) {
                break;
            }
            std::size_t next = 0;
            if (m_units.size() == 1) {
                const auto it = std::find(idle.begin(), idle.end(), m_units.units().front());
                next = it == idle.end() ? 0 : static_cast<std::size_t>(it - idle.begin() + 1) % idle.size();
            }
            m_units.select(m_state->world, idle[next]);
            selection_changed(false);
            run_action(ui::KeyBinding{ui::KeyContext::General, {}, KeyAction::CentreOnSelection, 0});
            break;
        }
        case KeyAction::NextKindInSelection:
            if (m_units.cycle_types(m_state->world)) {
                selection_changed(false);
            }
            break;
        case KeyAction::AttackMove:
            m_pick = Pick::Attack;
            break;
        case KeyAction::Stop:
            submit_orders(m_units.stop_orders(m_state->world, m_local_player), false);
            break;
        case KeyAction::HoldPosition:
            submit_orders(m_units.hold_orders(m_state->world, m_local_player), false);
            break;
        case KeyAction::Patrol:
            m_pick = Pick::Patrol;
            break;
        case KeyAction::Formation:
        case KeyAction::Scatter: {
            if (b.action == KeyAction::Formation) {
                m_units.formation(m_state->world, m_figures, m_formation);
                m_formation = (m_formation + 1) % 3;
            } else {
                m_units.scatter(m_state->world);
            }
            // The group takes its places around where it stands (0x0044C6D0).
            if (const auto c = m_units.centre(m_state->world)) {
                submit_orders(m_units.move_orders(m_state->world, m_local_player, c->first, c->second, false));
            }
            break;
        }
        case KeyAction::WhiteFlag: {
            std::vector<sim::Command> orders;
            const bool on = !m_units.any_white_flag(m_state->world);
            for (const ui::UnitRef& r : m_units.units()) {
                if (r.kind == ui::UnitRef::Kind::Ship) {
                    sim::ShipCommand o;
                    o.player = m_local_player;
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
                submit(sim::ExploreOrder{m_local_player, m_units.units().front().id});
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
                    o.player = m_local_player;
                    o.ship = r.id;
                    o.op = sim::ShipCommand::Op::Scuttle;
                    orders.emplace_back(o);
                } else {
                    sim::SoldierCommand o;
                    o.player = m_local_player;
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
            m_show_owners = !m_show_owners;
            break;
        case KeyAction::Quit:
            m_running = false;
            break;
    }
}

std::pair<double, double> MapViewer::screen_of(double tx, double ty) const {
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

std::optional<render::FigureSprites> MapViewer::unit_sprites(const ui::UnitRef& ref) const {
    const auto& graphics = m_graphics[static_cast<std::size_t>(m_camera.view().zoom)];
    if (!graphics) {
        return std::nullopt;
    }
    const render::WorldLayers layers = world_layers(*graphics);
    render::FigureSprites out;
    if (ref.kind == ui::UnitRef::Kind::Ship) {
        const sim::Ship* ship = m_state->world.ship(ref.id);
        render::ShipSprites s;
        if (ship == nullptr ||
            !render::ship_sprites(*ship, m_state->world, m_camera.view(), layers, m_clock.elapsed_ms(), s)) {
            return std::nullopt;
        }
        out.row = s.row;
        out.depth = s.depth;
        out.parts[out.count++] = s.hull;
        if (s.has_flag) {
            out.parts[out.count++] = s.flag;
        }
        return out;
    }
    const render::FigureInstance* figure = m_soldiers.find(ref.id);
    if (figure == nullptr || !render::land_figure_sprites(*figure, m_state->world, m_camera.view(), layers, out)) {
        return std::nullopt;
    }
    // The shadow is not part of the unit.
    std::uint8_t kept = 0;
    for (std::uint8_t i = 0; i < out.count; ++i) {
        if (out.parts[i].mode != render::DrawCommand::Mode::Shadow) {
            out.parts[kept++] = out.parts[i];
        }
    }
    out.count = kept;
    return out.count > 0 ? std::optional<render::FigureSprites>(out) : std::nullopt;
}

std::optional<std::array<double, 4>> MapViewer::unit_box(const ui::UnitRef& ref) const {
    const auto& graphics = m_graphics[static_cast<std::size_t>(m_camera.view().zoom)];
    if (const auto sprites = unit_sprites(ref); sprites && graphics) {
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
    const auto pos = ui::unit_position(m_state->world, ref);
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

std::vector<ui::UnitCandidate> MapViewer::units_in(double x0, double y0, double x1, double y1) const {
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
    for (const sim::Ship& s : m_state->world.ships()) {
        if (!s.sinking) {
            test({ui::UnitRef::Kind::Ship, s.id});
        }
    }
    for (const sim::Soldier& s : m_state->world.soldiers()) {
        if (!s.away()) {
            test({ui::UnitRef::Kind::Soldier, s.id});
        }
    }
    return out;
}

std::optional<ui::UnitRef> MapViewer::unit_at(double sx, double sy) const {
    // Pixel exact (Map_PickAtScreen 0x00487940): the unit whose visible pixel is under the mouse,
    // the one drawn last if several overlap.
    const auto& graphics = m_graphics[static_cast<std::size_t>(m_camera.view().zoom)];
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
            const auto sprites = unit_sprites(c.ref);
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
        const bool own = ui::unit_owner(m_state->world, c.ref) == m_local_player;
        const double rank = d - (c.ref.kind == ui::UnitRef::Kind::Soldier ? 1e6 : 0.0);
        if (!best || (own && !best_own) || (own == best_own && rank < best_d)) {
            best = c.ref;
            best_d = rank;
            best_own = own;
        }
    }
    return best;
}

void MapViewer::map_left_up(double x, double y, std::uint16_t mod, int clicks) {
    const bool shift = (mod & SDL_KMOD_SHIFT) != 0;
    const bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
    if (m_pick != Pick::None) {
        map_order(x, y, shift);
        return;
    }
    if (m_rubber_band && m_press) {
        m_units.box(m_state->world, units_in(m_press->first, m_press->second, x, y), m_local_player, shift);
        selection_changed(true);
        return;
    }
    if (const auto unit = unit_at(x, y)) {
        if (ctrl || clicks >= 2) {
            // All of this kind on screen.
            const ui::Rect v = m_hud ? m_hud->game_view() : ui::Rect{0, 0, 1 << 15, 1 << 15};
            m_units.select_alike(m_state->world, units_in(v.x, v.y, v.x + v.w, v.y + v.h), *unit);
        } else if (shift) {
            m_units.toggle(m_state->world, *unit);
        } else {
            m_units.select(m_state->world, *unit);
        }
        selection_changed(false);
        return;
    }
    if (m_options.original_mouse && !m_units.empty() && m_units.owner(m_state->world) == m_local_player) {
        map_order(x, y, shift);
        return;
    }
    m_units.clear();
    selection_changed(false);
    select_building_at_mouse();
}

void MapViewer::map_order(double x, double y, bool queue) {
    const Pick pick = std::exchange(m_pick, Pick::None);
    if (m_units.empty() || m_units.owner(m_state->world) != m_local_player) {
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
                o.player = m_local_player;
                o.ship = r.id;
                o.op = sim::ShipCommand::Op::Patrol;
                o.x = static_cast<std::uint16_t>(tile->x);
                o.y = static_cast<std::uint16_t>(tile->y);
                orders.emplace_back(o);
            } else if (const sim::Soldier* s = m_state->world.soldier(r.id)) {
                sim::SoldierCommand o;
                o.player = m_local_player;
                o.soldier = r.id;
                o.op = sim::SoldierCommand::Op::SetWaypoint;
                o.index = 0;
                o.point =
                    sim::Target::spot(static_cast<std::uint32_t>(s->cell_x()), static_cast<std::uint32_t>(s->cell_y()));
                orders.emplace_back(o);
                o.index = 1;
                o.point = sim::Target::spot(2 * tile->x, 2 * tile->y);
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
        const bool foreign = ui::unit_owner(m_state->world, *unit) != m_local_player;
        submit_orders(m_units.target_orders(m_state->world, m_local_player, t, queue, foreign || attack_pick));
        return;
    }
    if (!tile) {
        return;
    }
    const sim::World::Cell& cell = m_state->world.cell(tile->x, tile->y);
    if (cell.island != sim::World::kSea) {
        if (m_units.kind() == ui::UnitRef::Kind::Ship) {
            // A ship to a Kontor: its city.
            for (const sim::City& c : m_state->world.cities()) {
                if (c.island != cell.island || !c.warehouse_x || !c.warehouse_y) {
                    continue;
                }
                const auto b = sim::building_at(
                    m_state->world, m_buildings, c.island,
                    {static_cast<std::int32_t>(*c.warehouse_x), static_cast<std::int32_t>(*c.warehouse_y)});
                const auto here = sim::building_at(
                    m_state->world, m_buildings, cell.island,
                    {static_cast<std::int32_t>(cell.local_x), static_cast<std::int32_t>(cell.local_y)});
                if (b && here && b->origin.x == here->origin.x && b->origin.y == here->origin.y) {
                    const sim::Target t = sim::Target::city_of(c.island, c.index);
                    submit_orders(m_units.target_orders(m_state->world, m_local_player, t, queue,
                                                        attack_pick && c.owner != m_local_player));
                    return;
                }
            }
        } else if (const auto b = sim::building_at(
                       m_state->world, m_buildings, cell.island,
                       {static_cast<std::int32_t>(cell.local_x), static_cast<std::int32_t>(cell.local_y)});
                   b && b->type->kind != sim::building_kind::kGround && b->type->kind != sim::building_kind::kRoad &&
                   !sim::soldiers_walk_on(*b->type)) {
            // Land units onto a building: attack another's, walk to (or heal at) one's own.
            const sim::Target t = sim::Target::building(cell.island, static_cast<std::uint32_t>(b->origin.x),
                                                        static_cast<std::uint32_t>(b->origin.y));
            const bool foreign =
                target_owner(m_state->world, t) != m_local_player && target_owner(m_state->world, t) != sim::kNoPlayer;
            submit_orders(m_units.target_orders(m_state->world, m_local_player, t, queue, foreign || attack_pick));
            return;
        }
    }
    // With F first: an attack move (fights on the way, ADR 0010).
    submit_orders(
        m_units.move_orders(m_state->world, m_local_player, tile->x + 0.5, tile->y + 0.5, queue, attack_pick));
}

void MapViewer::order_to_tile(double tx, double ty, bool queue) {
    if (m_units.empty() || m_units.owner(m_state->world) != m_local_player) {
        return;
    }
    submit_orders(m_units.move_orders(m_state->world, m_local_player, tx, ty, queue));
}

void MapViewer::cancel_action() {
    if (m_pick != Pick::None) {
        m_pick = Pick::None;
        return;
    }
    if (m_hud && m_hud->back()) {
        return;  // a window went back a page or stopped picking
    }
    if (m_hud && m_hud->mode() == Hud::kModeBuild) {
        if (const auto a = m_hud->set_mode(Hud::kModeInfo)) {
            apply_hud(*a);
        }
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
        if (const auto a = m_hud->set_mode(Hud::kModeInfo)) {
            apply_hud(*a);
        }
    }
}

void MapViewer::selection_changed(bool box) {
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
        if (const auto a = m_hud->set_mode(Hud::kModeMilitary)) {
            apply_hud(*a);
        }
    }
}

std::optional<std::string> MapViewer::question_for(const sim::Command& command) const {
    return order_question(m_state->world, m_figures, m_local_player, command);
}

void MapViewer::submit_orders(std::vector<sim::Command> orders, bool sound) {
    if (orders.empty()) {
        return;
    }
    if (m_options.confirm && m_hud) {
        for (const sim::Command& c : orders) {
            if (const auto q = question_for(c)) {
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
        submit(std::move(c));
    }
    // One acknowledgement of the first unit (0x0044D540).
    if (sound && !m_units.empty()) {
        if (const auto pos = ui::unit_position(m_state->world, m_units.units().front())) {
            m_sound->order(m_units.kind() == ui::UnitRef::Kind::Ship, attack, pos->first, pos->second);
        }
    }
}

void MapViewer::draw_selection_overlay() {
    SDL_Renderer* r = m_window->sdl_renderer();
    const auto& graphics = m_graphics[static_cast<std::size_t>(m_camera.view().zoom)];
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
        const auto health = ui::unit_health(m_state->world, m_figures, ref);
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
        if (const auto sprites = unit_sprites(ref); sprites && graphics) {
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
    if (m_rubber_band && m_press) {
        const SDL_FRect band{static_cast<float>(std::min(m_press->first, m_mouse_x)),
                             static_cast<float>(std::min(m_press->second, m_mouse_y)),
                             static_cast<float>(std::abs(m_mouse_x - m_press->first)),
                             static_cast<float>(std::abs(m_mouse_y - m_press->second))};
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderRect(r, &band);
    }
}

}  // namespace o1602::app
