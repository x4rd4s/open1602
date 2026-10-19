// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

#include "o1602/audio/game_audio.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/ui/localization.hpp"
#include "o1602/ui/text_format.hpp"

namespace o1602::app {

namespace {

// Gadgets of CTRL.GAD and LEIST.GAD.
constexpr std::int32_t kRotateLeft = 30001;
constexpr std::int32_t kRotateRight = 30002;
constexpr std::int32_t kZoomIn = 30003;
constexpr std::int32_t kZoomOut = 30004;
constexpr std::int32_t kMinimap = 30005;
constexpr std::int32_t kCompass = 30006;
constexpr std::int32_t kFirstMode = 30011;  // build, military, info, options
constexpr std::int32_t kStatusLine = 100001;
constexpr std::int32_t kFirstEvent = 100011;
constexpr std::size_t kEvents = 3;
// TOOLS.INC: the compass (GFXMAP + 1 + rotation) and the message icons (GFXLEISTE).
constexpr std::int32_t kGfxParchment = 17;
constexpr std::int32_t kGfxCompass = 18;
constexpr std::int32_t kGfxLeiste = 253;
constexpr std::int32_t kLeisteLastBanner = 4;  // its message icons have colours for banners 0..4, 12 apart
constexpr std::int32_t kLeistArt = 101001;     // the status bar's art (drawn widened instead)
constexpr std::int32_t kLeistTextX = 750;      // its line ends here, 18 before the art's right edge
constexpr std::int32_t kTutorArt = 111001;     // the mission window's art (drawn widened instead)
constexpr std::int32_t kGfxTutor = 645;        // GFXTUTOR of TOOLS.INC
constexpr std::int32_t kTutorText = 110002;    // its flowing text
constexpr std::int32_t kDesignViewW = 768;     // game view, status bar and mission window at 1024 x 768
constexpr std::uint64_t kEventMs = 45'000;     // an icon stays this long (449 time units of 0x00430F20)
constexpr std::uint64_t kMinimapMs = 500;      // the minimap texture is uploaded at most this often

/// Help text line of [LEISTE] for the gadgets of the panel's head (in the order of the texts).
std::optional<std::size_t> help_line(std::int32_t id) {
    switch (id) {
        case kRotateLeft:
            return 2;
        case kRotateRight:
            return 3;
        case kZoomIn:
            return 4;
        case kZoomOut:
            return 5;
        default:
            break;
    }
    if (id >= kFirstMode && id < kFirstMode + 4) {
        return static_cast<std::size_t>(6 + id - kFirstMode);
    }
    return std::nullopt;
}

/// The icon of a message (Event_UpdateStatusIcons 0x0043AFA0): types 0..3 by colour (! event,
/// scroll treaty or trade, swords attack, lips other players), 4..7 plague, fire, drought, volcano.
/// Which message has which type is not documented; this follows the icons.
std::uint8_t event_type(std::uint16_t message) {
    switch (message) {
        case 0x01:
            return 4;
        case 0x02:
            return 5;
        case 0x03:
            return 6;
        case 0x5E:
            return 7;
        default:
            break;
    }
    const audio::MessageAudio* m = audio::message_audio(message);
    const std::uint8_t kind = m != nullptr ? m->kind : 0;
    return kind == 5 ? 2 : kind == 4 || kind == 7 ? 1 : kind == 3 ? 3 : 0;
}

}  // namespace

std::unique_ptr<Hud> Hud::create(SDL_Renderer* renderer, const assets::GameDirectory& game,
                                 const assets::Palette& palette, const assets::TextTable* texts) {
    auto ctrl = assets::load_gad(game, "CTRL.GAD");
    auto leist = assets::load_gad(game, "LEIST.GAD");
    if (!ctrl || !leist || !game.resolve(ui::sheet_file(0, ui::ArtSize::Large))) {
        return nullptr;
    }
    return std::unique_ptr<Hud>(
        new Hud(renderer, game, palette, std::move(ctrl).value(), std::move(leist).value(), texts));
}

Hud::Hud(SDL_Renderer* renderer, const assets::GameDirectory& game, const assets::Palette& palette,
         assets::GadgetLayout ctrl, assets::GadgetLayout leist, const assets::TextTable* texts)
    : m_sdl(renderer),
      m_palette(palette),
      m_renderer(std::make_unique<render::SdlUiRenderer>(renderer, game, palette, ui::ArtSize::Large)),
      m_ctrl(std::move(ctrl), 1.0),
      m_leist(std::move(leist), 1.0),
      m_end([&]() -> std::optional<assets::GadgetLayout> {
          auto ende = assets::load_gad(game, "ENDE.GAD");
          return ende ? std::optional<assets::GadgetLayout>(std::move(ende).value()) : std::nullopt;
      }()) {
    if (texts != nullptr) {
        m_texts = *texts;
        m_leiste = texts->section("LEISTE");
        m_meldung = texts->section("MELDUNG");
    }
    const assets::TextTable* own = m_texts ? &*m_texts : nullptr;
    for (std::size_t i = 0; i < kEvents; ++i) {
        m_leist.set_hidden(kFirstEvent + static_cast<std::int32_t>(i), true);
    }
    m_leist.set_hidden(kLeistArt, true);
    // The panels of the modes; without their layouts the head stays alone.
    if (auto bau = assets::load_gad(game, "BAU.GAD")) {
        m_build = std::make_unique<BuildPanel>(std::move(bau).value(), ui::original_build_menu(), own);
    }
    if (auto info = assets::load_gad(game, "INFO.GAD")) {
        m_info = std::make_unique<InfoPanel>(std::move(info).value(), own);
    }
    m_selection_panels = std::make_unique<SelectionPanels>(game);
    {
        auto option = assets::load_gad(game, "OPTION.GAD");
        auto disk = assets::load_gad(game, "DISK.GAD");
        auto musik = assets::load_gad(game, "MUSIK.GAD");
        auto anno = assets::load_gad(game, "ANNO.GAD");
        if (option && disk && musik && anno) {
            m_options = std::make_unique<OptionsPanel>(std::move(option).value(), std::move(disk).value(),
                                                       std::move(musik).value(), std::move(anno).value(), own);
        }
    }
    m_menu = std::make_unique<MainMenu>(game, own);
    if (!m_menu->usable()) {
        m_menu.reset();
    } else {
        // Open1602's own windows in the colours of its others (gold frame, dark ground).
        ui::FormStyle style;
        style.field = ui::nearest_colour(palette, 12, 8, 4);
        style.hover = ui::nearest_colour(palette, 72, 50, 24);
        style.selected = ui::nearest_colour(palette, 112, 80, 30);
        m_multiplayer = std::make_unique<MultiplayerMenu>(style);
        // The banners of the slots in the palette's nearest colours, grey for any further one.
        render::BannerTable banners = render::BannerTable::original();
        banners.add_more_players();
        std::vector<std::uint8_t> colours;
        for (const render::Banner& b : banners.banners()) {
            colours.push_back(ui::nearest_colour(palette, b.color.r, b.color.g, b.color.b));
        }
        m_multiplayer->set_banner_colours(std::move(colours), ui::nearest_colour(palette, 128, 128, 128));
    }
    if (auto tutor = assets::load_gad(game, "TUTOR.GAD")) {
        m_tutor.emplace(std::move(tutor).value(), 1.0);
        m_tutor->set_hidden(kTutorArt, true);
        // The gadgets at the window's right end (the scroll arrows) move with it on wide screens.
        for (const assets::Gadget& g : m_tutor->layout().gadgets) {
            if (g.kind != assets::GadgetKind::Unused && g.id != kTutorArt && g.pos[0] >= kDesignViewW / 2) {
                m_tutor_right.emplace_back(g.id, g.pos);
            }
        }
        if (const assets::Gadget* text = m_tutor->layout().find(kTutorText)) {
            m_tutor_text_size = text->size;
        }
    }
    m_ctrl.set_selected(m_mode, true);
    m_gfx_baumat = m_ctrl.layout().constant("GFXBAUMAT").value_or(m_gfx_baumat);
}

void Hud::draw_resources() {
    // Design units from the top left of the map, icons at the top, text cells ending at 30.
    const double s = m_regions.scale;
    const ui::Rect& v = m_regions.game_view;
    const std::int32_t x0 = v.x + static_cast<std::int32_t>(5.5 * s);
    std::vector<ui::DrawItem> items;
    const auto icon = [&](std::int32_t gfx, std::int32_t x) {
        ui::DrawItem d;
        d.kind = ui::DrawItem::Kind::Sprite;
        d.gfx = gfx;
        d.x = ui::to_pixels(x, s);
        d.y = 0;
        items.push_back(d);
    };
    const auto text = [&](std::string t, std::int32_t x) {
        ui::DrawItem d;
        d.kind = ui::DrawItem::Kind::Text;
        d.block = 0;   // ZEINR
        d.colour = 4;  // light grey (240, 240, 240)
        d.x = ui::to_pixels(x, s);
        d.y = ui::to_pixels(30, s);
        d.text = std::move(t);
        items.push_back(d);
    };
    const auto with_cost = [](std::int64_t value, const std::optional<std::int64_t>& cost) {
        return cost ? std::to_string(value) + " (" + std::to_string(*cost) + ")" : std::to_string(value);
    };
    const Resources& r = m_resources;
    icon(m_gfx_baumat + 11, 16);
    text(r.money ? with_cost(*r.money, r.money_cost) : std::string("???"), 46);
    if (r.goods) {
        // Werkzeug, Holz, Ziegel: GFXBAUMAT + 13, 14, 12.
        const std::array<std::int32_t, 3> icons{m_gfx_baumat + 13, m_gfx_baumat + 14, m_gfx_baumat + 12};
        for (std::size_t i = 0; i < 3; ++i) {
            const auto x = static_cast<std::int32_t>(296 + 110 * i);
            icon(icons[i], x);
            const auto cost = r.goods_cost ? std::optional<std::int64_t>((*r.goods_cost)[i]) : std::nullopt;
            text(with_cost((*r.goods)[i], cost), x + 30);
        }
    }
    if (r.inhabitants) {
        if (r.shield >= 0) {
            icon(m_gfx_baumat + 17 + std::min(r.shield, 3), 666);
        } else {
            icon(m_gfx_baumat + 15, 646);
        }
        text(std::to_string(*r.inhabitants), 696);
    }
    m_renderer->draw(items, x0, v.y, s);
}

SelectionPanel* Hud::selection_panel(std::optional<PanelContext>& context) {
    if (m_simulation == nullptr || m_buildings == nullptr || m_figure_table == nullptr || !m_selection_panels) {
        return nullptr;
    }
    if (m_mode == kModeMilitary) {
        context.emplace(PanelContext{*m_simulation, *m_buildings, *m_figure_table, m_texts ? &*m_texts : nullptr,
                                     m_local, m_units});
        return m_selection_panels->military_panel_for(*context);
    }
    if (m_mode != kModeInfo || m_selection.kind == Selection::Kind::None) {
        return nullptr;
    }
    context.emplace(
        PanelContext{*m_simulation, *m_buildings, *m_figure_table, m_texts ? &*m_texts : nullptr, m_local, m_units});
    return m_selection_panels->panel_for(m_selection, *context);
}

bool Hud::picking() {
    if (m_mode != kModeInfo && m_mode != kModeMilitary) {
        return false;
    }
    std::optional<PanelContext> context;
    SelectionPanel* p = selection_panel(context);
    return p != nullptr && p->picking();
}

void Hud::map_click(std::uint32_t x, std::uint32_t y) {
    std::optional<PanelContext> context;
    if (SelectionPanel* p = selection_panel(context); p != nullptr && p->picking()) {
        p->picked(x, y, m_selection, *context, m_commands);
    }
}

std::optional<Hud::Action> Hud::toggle_window(Selection::Kind kind) {
    if (m_mode == kModeInfo && m_selection.kind == kind) {
        m_selection = Selection{};
        return std::nullopt;
    }
    std::optional<Action> action;
    if (m_mode != kModeInfo) {
        action = set_mode(kModeInfo);
    }
    m_selection = Selection{};
    m_selection.kind = kind;
    std::optional<PanelContext> context;
    if (SelectionPanel* p = selection_panel(context)) {
        p->opened();
    }
    return action;
}

bool Hud::back() {
    if (m_end_active) {
        if (const auto choice = m_end.back()) {
            m_end_choice = choice;
        }
        return true;  // no keys on the final screen of a lost game
    }
    if (multiplayer_open()) {
        if (auto e = m_multiplayer->back()) {
            m_multiplayer_events.push_back(std::move(*e));
        }
        return true;
    }
    if (m_menu_active) {
        return m_menu && m_menu->back();
    }
    if (m_mode == kModeOptions) {
        return m_options && m_options->back();
    }
    if (m_mode != kModeInfo && m_mode != kModeMilitary) {
        return false;
    }
    if (m_mode == kModeInfo &&
        (m_selection.kind == Selection::Kind::ShipList || m_selection.kind == Selection::Kind::CityList ||
         m_selection.kind == Selection::Kind::Diplomacy)) {
        // A page back (tribute -> treaties), else back to the player status (0x0040A0D6).
        std::optional<PanelContext> context;
        if (SelectionPanel* p = selection_panel(context); p != nullptr && p->back()) {
            return true;
        }
        m_selection = Selection{};
        return true;
    }
    std::optional<PanelContext> context;
    SelectionPanel* p = selection_panel(context);
    return p != nullptr && p->back();
}

std::optional<Hud::Action> Hud::build_tool_action() {
    if (!m_build) {
        return std::nullopt;
    }
    if (m_build->demolish()) {
        return Action{Action::Kind::Demolish};
    }
    const BuildPanel::Available available =
        m_available ? m_available : BuildPanel::Available([](std::uint32_t) { return true; });
    if (const auto id = m_build->current(available)) {
        Action a{Action::Kind::Build};
        a.building = *id;
        return a;
    }
    return Action{Action::Kind::ToolOff};
}

std::optional<Hud::Action> Hud::set_mode(std::int32_t mode) {
    if (mode < kModeBuild || mode > kModeOptions) {
        return std::nullopt;
    }
    m_ctrl.set_selected(m_mode, false);
    if (mode == kModeOptions && m_mode != kModeOptions && m_options) {
        m_options->opened();
    }
    m_mode = mode;
    m_ctrl.set_selected(m_mode, true);
    if (m_build) {
        m_build->close_popup();
    }
    // Hud_SetMode 0x0043B660: the build mode arms the remembered building again, the others put
    // the tool away.
    return mode == kModeBuild ? build_tool_action() : std::optional<Action>(Action{Action::Kind::ToolOff});
}

ui::Rect Hud::layout(std::int32_t width, std::int32_t height) {
    // The whole panel stays on screen: the smaller of the two scales (the original: width / 1024).
    const double scale =
        std::min(static_cast<double>(width) / ui::kDesignWidth, static_cast<double>(height) / ui::kDesignHeight);
    m_regions = ui::screen_regions(width, height, scale);
    m_ctrl.set_scale(scale);
    m_leist.set_scale(scale);
    if (m_build) {
        m_build->set_scale(scale);
    }
    if (m_info) {
        m_info->set_scale(scale);
    }
    if (m_options) {
        m_options->set_scale(scale);
    }
    if (m_menu) {
        m_menu->set_scale(scale);
    }
    if (m_multiplayer) {
        m_multiplayer->set_scale(scale);
    }
    m_end.set_scale(scale);
    if (m_selection_panels) {
        m_selection_panels->set_scale(scale);
    }
    return m_regions.game_view;
}

bool Hud::covers(std::int32_t x, std::int32_t y) const noexcept {
    // The mission's window takes the clicks inside it (0x0042D1C0); the menu all of them.
    return m_menu_active || m_regions.side_panel.contains(x, y) || m_regions.status_bar.contains(x, y) ||
           (m_options_model.mission_window && m_tutor && m_regions.tutor.contains(x, y));
}

ui::Rect Hud::minimap_rect() const {
    const assets::Gadget* g = m_ctrl.layout().find(kMinimap);
    if (g == nullptr) {
        return {};
    }
    const double s = m_regions.scale;
    return {m_regions.side_panel.x + ui::to_pixels(g->pos[0], s), m_regions.side_panel.y + ui::to_pixels(g->pos[1], s),
            static_cast<std::int32_t>(g->size[0] * s), static_cast<std::int32_t>(g->size[1] * s)};
}

std::optional<std::pair<double, double>> Hud::minimap_tile(std::int32_t x, std::int32_t y) const {
    const ui::Rect map = minimap_rect();
    if (!map.contains(x, y) || m_world_w == 0 || m_world_h == 0) {
        return std::nullopt;
    }
    const double fit = std::min(static_cast<double>(map.w) / m_world_w, static_cast<double>(map.h) / m_world_h);
    const double ox = map.x + (map.w - m_world_w * fit) / 2.0;
    const double oy = map.y + (map.h - m_world_h * fit) / 2.0;
    const double tx = (x - ox) / fit;
    const double ty = (y - oy) / fit;
    if (tx < 0.0 || ty < 0.0 || tx >= m_world_w || ty >= m_world_h) {
        return std::nullopt;
    }
    return std::make_pair(tx, ty);
}

std::optional<std::pair<double, double>> Hud::last_message_tile() const {
    for (const Event& e : m_events) {
        if (e.tile) {
            return e.tile;
        }
    }
    return std::nullopt;
}

void Hud::draw_question() {
    if (!m_question) {
        return;
    }
    const double s = m_regions.scale;
    // Centred on the main menu's window, in the game on the map.
    const ui::Rect& v = m_menu_active ? m_regions.full : m_regions.game_view;
    const auto px = [&](double d) { return static_cast<std::int32_t>(d * s); };
    const ui::Rect box{v.x + (v.w - px(520)) / 2, v.y + (v.h - px(130)) / 2, px(520), px(130)};
    constexpr std::uint8_t kGold = 215;  // (202, 172, 52) in STADTFLD.COL
    constexpr std::uint8_t kDark = 28;   // (35, 23, 12)
    m_renderer->fill(ui::Rect{box.x - px(3), box.y - px(3), box.w + px(6), box.h + px(6)}, kGold);
    m_renderer->fill(box, kDark);
    m_yes = ui::Rect{box.x + px(90), box.y + px(80), px(140), px(34)};
    m_no = ui::Rect{box.x + box.w - px(230), box.y + px(80), px(140), px(34)};
    std::vector<ui::DrawItem> items;
    const auto text = [&](const std::string& t, std::int32_t x, std::int32_t y) {
        ui::DrawItem d;
        d.kind = ui::DrawItem::Kind::Text;
        d.block = 0;
        d.colour = 4;
        d.align = ui::Align::Centre;
        d.x = x;
        d.y = y;
        d.text = t;
        items.push_back(d);
    };
    // Up to two lines, broken at '\n'.
    const std::string& q = *m_question;
    const std::size_t nl = q.find('\n');
    text(q.substr(0, nl), box.w / 2, px(36));
    if (nl != std::string::npos) {
        text(q.substr(nl + 1), box.w / 2, px(62));
    }
    for (const ui::Rect* b : {&m_yes, &m_no}) {
        m_renderer->fill(*b, kGold);
        m_renderer->fill(ui::Rect{b->x + px(2), b->y + px(2), b->w - px(4), b->h - px(4)}, kDark);
    }
    text(ui::tr("dialog.yes"), m_yes.x - box.x + m_yes.w / 2, m_yes.y - box.y + px(28));
    text(ui::tr("dialog.no"), m_no.x - box.x + m_no.w / 2, m_no.y - box.y + px(28));
    m_renderer->draw(items, box.x, box.y, s);
}

void Hud::menu_origin(std::int32_t& x, std::int32_t& y) const {
    const double s = m_regions.scale;
    x = std::max(0, (m_regions.full.w - static_cast<std::int32_t>(ui::kDesignWidth * s)) / 2);
    y = std::max(0, (m_regions.full.h - static_cast<std::int32_t>(ui::kDesignHeight * s)) / 2);
}

ui::Rect Hud::menu_video_rect() const {
    std::int32_t ox = 0;
    std::int32_t oy = 0;
    menu_origin(ox, oy);
    const double s = m_regions.scale;
    const ui::Rect f = MainMenu::video_frame();
    return {ox + ui::to_pixels(f.x, s), oy + ui::to_pixels(f.y, s), ui::to_pixels(f.w, s), ui::to_pixels(f.h, s)};
}

void Hud::draw_menu() {
    m_renderer->fill(m_regions.full, 0);
    std::int32_t ox = 0;
    std::int32_t oy = 0;
    menu_origin(ox, oy);
    if (m_end_active) {
        m_end.draw(*m_renderer, ox, oy);
    } else if (multiplayer_open()) {
        m_renderer->draw(m_multiplayer->draw(m_multiplayer_model, m_renderer->fonts()), ox, oy, m_regions.scale);
    } else if (m_menu) {
        m_renderer->draw(m_menu->draw(m_renderer->fonts()), ox, oy, m_regions.scale);
    }
    draw_question();
    draw_help();
}

void Hud::open_end_screen(sim::EndScreenSummary summary, bool view_only) {
    m_end.open(std::move(summary), view_only);
    m_end_choice.reset();
    m_end_active = true;
    m_menu_active = true;
}

void Hud::close_end_screen(EndScreen::Choice choice) {
    m_end_active = false;
    m_end_choice.reset();
    if (choice != EndScreen::Choice::Resume && m_menu) {
        open_menu();
    } else {
        m_menu_active = false;
    }
}

void Hud::draw_mission(const sim::World& world) {
    if (!m_options_model.mission_window || !m_tutor || m_simulation == nullptr) {
        return;
    }
    const sim::Player* p = world.player(m_local);
    const sim::Mission* mission = p != nullptr ? world.mission_of(*p) : nullptr;
    if (mission == nullptr && !world.missions().empty()) {
        mission = &world.missions().front();  // the endless game: one mission without owner, only its text
    }
    // The window of the original (region above the status bar, 0x0043B53F): its art, the text
    // flowing in 110002; the lines and buttons of the tutorial stay hidden.
    m_tutor->set_scale(m_regions.scale);
    // As wide as the game view (the original's art is 768 wide): the art widened, the text wider,
    // the arrows at the right end.
    const ui::Rect& window = m_regions.tutor;
    const std::int32_t extra = std::max(0, static_cast<std::int32_t>(window.w / m_regions.scale) - kDesignViewW);
    for (const auto& [id, pos] : m_tutor_right) {
        m_tutor->set_position(id, pos[0] + extra, pos[1]);
    }
    m_tutor->set_size(kTutorText, m_tutor_text_size[0] + extra, m_tutor_text_size[1]);
    m_renderer->draw_widened(0, kGfxTutor, window, m_regions.scale);
    m_tutor->set_text(110002, mission != nullptr && !mission->text.empty() ? mission->text : std::string("-"));
    for (std::int32_t id = 110011; id <= 110016; ++id) {
        m_tutor->set_visible(id, false);
    }
    m_tutor->set_visible(110031, false);  // the arrows pressed
    // Clamped here, where the text and the window's width are known: "down" past the end would
    // blank the window and take as many clicks back up.
    m_tutor_line = std::min(m_tutor_line, m_tutor->max_scroll(110002, m_renderer->fonts()));
    m_tutor->set_scroll(110002, m_tutor_line);
    m_renderer->draw(m_tutor->draw(m_renderer->fonts()), m_regions.tutor.x, m_regions.tutor.y, m_regions.scale);
}

void Hud::reset_game() {
    m_events.clear();
    m_selection = Selection{};
    m_question.reset();
    m_help.reset();
    m_modal_press = ModalPress::None;
    m_commands.clear();
    m_minimap_dirty = true;
    m_tutor_line = 0;
}

std::string Hud::crops_line(const std::array<std::pair<std::uint8_t, std::uint32_t>, 3>& crops) const {
    constexpr std::size_t kGameSuited = 22;
    constexpr std::uint8_t kFirstRaw = sim::ware::kCornField;  // [ROHST] starts with grain
    if (!m_texts) {
        return {};
    }
    const std::vector<std::string>& game = m_texts->section("GAME");
    const std::vector<std::string>& raw = m_texts->section("ROHST");
    const auto name = [&](std::uint8_t ware) -> std::string_view {
        const std::size_t i = ware >= kFirstRaw ? std::size_t{ware} - kFirstRaw : raw.size();
        return i < raw.size() ? std::string_view(raw[i]) : std::string_view();
    };
    if (game.size() <= kGameSuited) {
        return {};
    }
    return ui::format_text(game[kGameSuited],
                           {name(crops[0].first), std::int64_t{crops[0].second}, name(crops[1].first),
                            std::int64_t{crops[1].second}, name(crops[2].first), std::int64_t{crops[2].second}});
}

void Hud::draw_help() {
    if (!m_help) {
        return;
    }
    const double s = m_regions.scale;
    const ui::Rect& v = m_regions.game_view;
    const auto px = [&](double d) { return static_cast<std::int32_t>(d * s); };
    const std::size_t rows = (m_help->size() + 1) / 2;
    const std::int32_t line = px(14);
    const std::int32_t w = std::min(v.w - px(16), px(740));
    const std::int32_t h = std::min(v.h - px(16), px(56) + static_cast<std::int32_t>(rows) * line);
    const ui::Rect box{v.x + (v.w - w) / 2, v.y + (v.h - h) / 2, w, h};
    constexpr std::uint8_t kGold = 215;
    constexpr std::uint8_t kDark = 28;
    m_renderer->fill(ui::Rect{box.x - px(3), box.y - px(3), box.w + px(6), box.h + px(6)}, kGold);
    m_renderer->fill(box, kDark);
    std::vector<ui::DrawItem> items;
    const auto text = [&](const std::string& t, std::int32_t x, std::int32_t y, ui::Align align,
                          std::int32_t font = 1) {
        ui::DrawItem d;
        d.kind = ui::DrawItem::Kind::Text;
        d.block = font;  // the small font (ZEILITTLE), headings in the normal one
        d.colour = 4;
        d.align = align;
        d.x = x;
        d.y = y;
        d.text = t;
        items.push_back(d);
    };
    text(ui::tr("keys.title"), box.w / 2, px(28), ui::Align::Centre, 0);
    const std::int32_t column = box.w / 2;
    for (std::size_t i = 0; i < m_help->size(); ++i) {
        const HelpLine& l = (*m_help)[i];
        const std::int32_t x = px(14) + static_cast<std::int32_t>(i / rows) * column;
        const std::int32_t y = px(52) + static_cast<std::int32_t>(i % rows) * line;
        if (y > box.h - px(4)) {
            continue;  // a small window: what does not fit is left out
        }
        if (l.keys.empty()) {
            text(l.text, x, y + px(1), ui::Align::Left, 0);  // a heading (or a blank line)
            continue;
        }
        text(l.keys, x + px(8), y, ui::Align::Left);
        text(l.text, x + px(160), y, ui::Align::Left);
    }
    m_renderer->draw(items, box.x, box.y, s);
}

std::optional<Hud::Action> Hud::mouse(std::int32_t x, std::int32_t y, bool down) {
    std::optional<Action> action;
    // Mouse motion also comes here with down == false: only a release after a press is a click.
    if (m_help) {
        // The key overview takes the click that closes it.
        if (down) {
            m_modal_press = ModalPress::Elsewhere;
        } else if (m_modal_press != ModalPress::None) {
            m_help.reset();
            m_modal_press = ModalPress::None;
        }
        return std::nullopt;
    }
    if (!m_menu_active && m_options_model.mission_window && m_tutor && m_regions.tutor.contains(x, y)) {
        // The mission's window: X closes it, the arrows scroll its text.
        for (const ui::PanelEvent& e : m_tutor->mouse(x - m_regions.tutor.x, y - m_regions.tutor.y, down)) {
            if (e.kind != ui::PanelEvent::Kind::Clicked) {
                continue;
            }
            if (e.id == 110001) {
                m_options_model.mission_window = false;
                m_option_events.push_back(OptionsEvent{OptionsEvent::Kind::MissionWindow});
            } else if (e.id >= 110022 && e.id <= 110026) {
                m_tutor_line = std::max(0, m_tutor_line - 1);
            } else if (e.id >= 110032 && e.id <= 110036) {
                ++m_tutor_line;
            }
        }
        return std::nullopt;
    }
    if (m_question) {
        // The question takes every click until it is answered: pressed and released on the same
        // button (as the original's buttons), so passing over "Ja" answers nothing.
        const ModalPress at = m_yes.contains(x, y)  ? ModalPress::Yes
                              : m_no.contains(x, y) ? ModalPress::No
                                                    : ModalPress::Elsewhere;
        if (down) {
            if (m_modal_press == ModalPress::None) {
                m_modal_press = at;
            }
            return std::nullopt;
        }
        const ModalPress pressed = std::exchange(m_modal_press, ModalPress::None);
        if (pressed == ModalPress::None) {
            return std::nullopt;  // the pointer only moved
        }
        if (at == pressed && at != ModalPress::Elsewhere) {
            Action a{Action::Kind::Answer};
            a.step = at == ModalPress::Yes ? 1 : 0;
            m_question.reset();
            return a;
        }
        return std::nullopt;
    }
    if (m_end_active) {
        std::int32_t ox = 0;
        std::int32_t oy = 0;
        menu_origin(ox, oy);
        if (const auto choice = m_end.mouse(x - ox, y - oy, down)) {
            m_end_choice = choice;
        }
        return std::nullopt;
    }
    if (multiplayer_open()) {
        std::int32_t ox = 0;
        std::int32_t oy = 0;
        menu_origin(ox, oy);
        for (MultiplayerEvent& e : m_multiplayer->mouse(m_multiplayer_model, x - ox, y - oy, down)) {
            m_multiplayer_events.push_back(std::move(e));
        }
        return std::nullopt;
    }
    if (m_menu_active && m_menu) {
        std::int32_t ox = 0;
        std::int32_t oy = 0;
        menu_origin(ox, oy);
        for (MenuEvent& e : m_menu->mouse(x - ox, y - oy, down)) {
            m_menu_events.push_back(std::move(e));
        }
        return std::nullopt;
    }
    const ui::Rect& panel = m_regions.side_panel;
    const ui::Rect& bar = m_regions.status_bar;
    // The mode's panel first: an open pop-up takes every press on the side panel.
    if (m_mode == kModeBuild && m_build) {
        const BuildPanel::Available available =
            m_available ? m_available : BuildPanel::Available([](std::uint32_t) { return true; });
        const bool popup = m_build->popup_open();
        bool consumed = false;
        if (const auto a = m_build->mouse(x - panel.x, y - panel.y, down, available, consumed)) {
            switch (a->kind) {
                case BuildPanel::Action::Kind::Pick:
                    action = Action{Action::Kind::Build};
                    action->building = a->building;
                    break;
                case BuildPanel::Action::Kind::Demolish:
                    action = Action{Action::Kind::Demolish};
                    break;
                case BuildPanel::Action::Kind::Rotate:
                    action = Action{Action::Kind::TurnBuilding};
                    action->step = a->step;
                    break;
                case BuildPanel::Action::Kind::OrientToRoad:
                    action = Action{Action::Kind::OrientToRoad};
                    action->step = a->step;
                    break;
            }
        }
        if (popup && consumed) {
            return action;
        }
    } else if (m_mode == kModeInfo || m_mode == kModeMilitary) {
        std::optional<PanelContext> context;
        if (SelectionPanel* p = selection_panel(context)) {
            for (const ui::PanelEvent& e : p->panel().mouse(x - panel.x, y - panel.y, down)) {
                p->clicked(e, m_selection, *context, m_commands);
            }
            if (const auto r = p->take_request()) {
                switch (r->kind) {
                    case SelectionPanel::Request::Kind::FoundKontor:
                        action = Action{Action::Kind::FoundKontor};
                        action->ship = r->ship;
                        break;
                    case SelectionPanel::Request::Kind::SelectShip:
                        action = Action{Action::Kind::SelectShip, r->x, r->y};
                        action->ship = r->ship;
                        break;
                    case SelectionPanel::Request::Kind::Centre:
                        action = Action{Action::Kind::Message, r->x, r->y};
                        break;
                }
            }
        } else if (m_info && m_mode == kModeInfo) {
            if (const auto list = m_info->mouse(x - panel.x, y - panel.y, down)) {
                action = toggle_window(*list);
            } else if (m_info->take_rating()) {
                action = Action{Action::Kind::Rating};
            }
        }
    } else if (m_mode == kModeOptions && m_options) {
        for (OptionsEvent& e : m_options->mouse(x - panel.x, y - panel.y, down, m_options_model)) {
            m_option_events.push_back(std::move(e));
        }
    }
    const auto ctrl = m_ctrl.mouse(x - panel.x, y - panel.y, down);
    const auto leist = m_leist.mouse(x - bar.x, y - bar.y, down);
    for (const ui::PanelEvent& e : ctrl) {
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            continue;
        }
        switch (e.id) {
            case kRotateLeft:
                action = Action{Action::Kind::RotateLeft};
                break;
            case kRotateRight:
                action = Action{Action::Kind::RotateRight};
                break;
            case kZoomIn:
                action = Action{Action::Kind::ZoomIn};
                break;
            case kZoomOut:
                action = Action{Action::Kind::ZoomOut};
                break;
            default:
                if (e.id >= kModeBuild && e.id <= kModeOptions) {
                    action = set_mode(e.id);
                }
                break;
        }
    }
    for (const ui::PanelEvent& e : leist) {
        const auto k = static_cast<std::size_t>(e.id - kFirstEvent);
        if (e.kind == ui::PanelEvent::Kind::Clicked && k < m_events.size() && m_events[k].tile) {
            action = Action{Action::Kind::Message, m_events[k].tile->first, m_events[k].tile->second};
        }
    }
    // The minimap: pressing or dragging on it moves the view there.
    if (down) {
        if (const auto t = minimap_tile(x, y)) {
            action = Action{Action::Kind::Minimap, t->first, t->second};
        }
    }
    return action;
}

void Hud::message(std::uint16_t id, sim::BannerId banner, std::optional<std::pair<double, double>> tile,
                  std::uint64_t now_ms) {
    // Event_Post 0x00430D50: the same message at the same place only refreshes its time; a new one
    // pushes the oldest out.
    for (Event& e : m_events) {
        if (e.id == id && e.tile == tile) {
            e.time_ms = now_ms;
            return;
        }
    }
    m_events.push_front(Event{id, banner, tile, now_ms});
    if (m_events.size() > kEvents) {
        m_events.pop_back();
    }
}

namespace {

/// The colour of a world tile on the minimap.
std::array<std::uint8_t, 3> minimap_colour(const sim::World& world, const assets::BuildingTable& buildings,
                                           const render::BannerTable& banners, const sim::World::Cell& c) {
    std::array<std::uint8_t, 3> rgb{38, 70, 128};  // sea
    if (c.island == sim::World::kSea) {
        return rgb;
    }
    const sim::MapCell& top = world.islands()[c.island].map.top(c.local_x, c.local_y);
    const assets::BuildingType* b = top.empty() ? nullptr : buildings.by_sprite(top.sprite);
    const std::int32_t kind = b != nullptr ? b->kind : sim::building_kind::kSea;
    namespace bk = sim::building_kind;
    const bool water = kind == bk::kSea || kind == bk::kSurf || kind == bk::kSurfCorner;
    rgb = water                 ? std::array<std::uint8_t, 3>{38, 70, 128}
          : kind == bk::kForest ? std::array<std::uint8_t, 3>{60, 96, 40}
                                : std::array<std::uint8_t, 3>{112, 132, 66};
    const bool scenery = water || kind == bk::kForest || kind == bk::kGround || kind == bk::kRiverMouth;
    if (!scenery && top.owner != sim::kNoPlayer) {
        const sim::Player* p = world.player(top.owner);
        const render::Banner* banner = p != nullptr ? banners.get(p->banner) : nullptr;
        if (banner != nullptr) {
            rgb = {banner->color.r, banner->color.g, banner->color.b};
        }
    }
    return rgb;
}

}  // namespace

void Hud::build_minimap(const sim::World& world, const assets::BuildingTable& buildings,
                        const render::BannerTable& banners) {
    m_world_w = world.width();
    m_world_h = world.height();
    m_minimap_islands = world.islands().size();
    m_minimap_next = 0;
    m_minimap.assign(std::size_t{m_world_w} * m_world_h * 4, 0);
    for (std::uint32_t y = 0; y < m_world_h; ++y) {
        for (std::uint32_t x = 0; x < m_world_w; ++x) {
            const std::array<std::uint8_t, 3> rgb = minimap_colour(world, buildings, banners, world.cell(x, y));
            const std::size_t i = (std::size_t{y} * m_world_w + x) * 4;
            m_minimap[i] = rgb[0];
            m_minimap[i + 1] = rgb[1];
            m_minimap[i + 2] = rgb[2];
            m_minimap[i + 3] = 255;
        }
    }
}

bool Hud::refresh_minimap_island(const sim::World& world, const assets::BuildingTable& buildings,
                                 const render::BannerTable& banners, std::size_t island) {
    // Only islands change on the map: the sea between them stays as the full build drew it.
    const sim::WorldIsland& is = world.islands()[island];
    const std::uint32_t x1 = std::min(m_world_w, is.x + is.map.width());
    const std::uint32_t y1 = std::min(m_world_h, is.y + is.map.height());
    bool changed = false;
    for (std::uint32_t y = is.y; y < y1; ++y) {
        for (std::uint32_t x = is.x; x < x1; ++x) {
            const sim::World::Cell& c = world.cell(x, y);
            if (c.island != island) {
                continue;
            }
            const std::array<std::uint8_t, 3> rgb = minimap_colour(world, buildings, banners, c);
            const std::size_t i = (std::size_t{y} * m_world_w + x) * 4;
            if (m_minimap[i] != rgb[0] || m_minimap[i + 1] != rgb[1] || m_minimap[i + 2] != rgb[2]) {
                m_minimap[i] = rgb[0];
                m_minimap[i + 1] = rgb[1];
                m_minimap[i + 2] = rgb[2];
                changed = true;
            }
        }
    }
    return changed;
}

void Hud::draw(const sim::World& world, const assets::BuildingTable& buildings, const render::BannerTable& banners,
               const std::array<std::pair<double, double>, 4>& view, std::uint64_t now_ms,
               const sim::PlayerFigures& figures, std::uint8_t colour) {
    const double s = m_regions.scale;
    draw_resources();
    // Black under the art where the window is larger than it.
    m_renderer->fill(m_regions.side_panel, 0);
    m_renderer->fill(m_regions.status_bar, 0);
    // Help text: the gadget under the mouse, else the status of the game.
    std::string status = m_status;
    if (const auto id = m_ctrl.hovered()) {
        if (const auto line = help_line(*id); line && *line < m_leiste.size()) {
            status = m_leiste[*line];
        }
    }
    // Message icons: expired ones go, the others show their type in the player's colour.
    std::erase_if(m_events, [&](const Event& e) { return now_ms > e.time_ms + kEventMs; });
    for (std::size_t i = 0; i < kEvents; ++i) {
        const auto id = kFirstEvent + static_cast<std::int32_t>(i);
        if (i >= m_events.size()) {
            m_leist.set_hidden(id, true);
            continue;
        }
        const Event& e = m_events[i];
        const std::uint8_t type = event_type(e.id);
        if (type < 4) {
            m_leist.set_graphic(id, kGfxLeiste + 1 + std::min<std::int32_t>(e.banner, kLeisteLastBanner) + type * 12,
                                6);
        } else {
            m_leist.set_graphic(id, kGfxLeiste + 1 + 0x28 + type * 2, 1);
        }
        m_leist.set_hidden(id, false);
        if (m_leist.hovered() == id && e.id < m_meldung.size()) {
            status = m_meldung[e.id];
        }
    }
    m_ctrl.set_graphic(kCompass, kGfxCompass + m_rotation, 0);
    m_renderer->draw(m_ctrl.draw(m_renderer->fonts()), m_regions.side_panel.x, m_regions.side_panel.y, s);
    // The mode's panel below the head, and its help texts.
    const BuildPanel::Available available =
        m_available ? m_available : BuildPanel::Available([](std::uint32_t) { return true; });
    if (m_mode == kModeBuild && m_build) {
        m_build->draw(*m_renderer, buildings, available,
                      static_cast<std::uint8_t>((m_rotation + m_building_rotation) & 3U), m_regions.side_panel, s);
        if (const auto text = m_build->help(m_leiste)) {
            status = *text;
        }
    } else if (m_mode == kModeInfo || m_mode == kModeMilitary) {
        std::optional<PanelContext> context;
        if (SelectionPanel* p = selection_panel(context)) {
            p->refresh(m_selection, *context);
            m_renderer->draw(p->panel().draw(m_renderer->fonts()), m_regions.side_panel.x, m_regions.side_panel.y, s);
            if (const auto id = p->panel().hovered()) {
                if (const auto text = p->help(*id, m_selection, *context)) {
                    status = *text;
                }
            }
        } else if (m_info && m_mode == kModeInfo) {
            m_info->draw(*m_renderer, figures, colour, m_regions.side_panel, s);
            if (const auto text = m_info->help(m_leiste)) {
                status = *text;
            }
        }
    } else if (m_mode == kModeOptions && m_options) {
        m_options->refresh(m_options_model);
        m_renderer->draw(m_options->panel().draw(m_renderer->fonts()), m_regions.side_panel.x, m_regions.side_panel.y,
                         s);
        if (const auto text = m_options->help(m_leiste, m_options_model)) {
            status = *text;
        }
    }
    draw_mission(world);
    // The minimap on the parchment.
    // Built in full once per game; then one island per frame is brought up to date and the texture
    // is uploaded again only when a pixel changed, at most every kMinimapMs.
    if (m_minimap_dirty || world.width() != m_world_w || world.height() != m_world_h ||
        world.islands().size() != m_minimap_islands) {
        build_minimap(world, buildings, banners);
        m_minimap_ms = now_ms;
        m_minimap_dirty = true;
        m_minimap_changed = false;
    } else if (m_minimap_islands > 0) {
        m_minimap_next %= m_minimap_islands;
        m_minimap_changed |= refresh_minimap_island(world, buildings, banners, m_minimap_next++);
        if (m_minimap_changed && now_ms >= m_minimap_ms + kMinimapMs) {
            m_minimap_ms = now_ms;
            m_minimap_dirty = true;
            m_minimap_changed = false;
        }
    }
    const ui::Rect map = minimap_rect();
    if (map.w > 0) {
        // The parchment under the map (GFXMAP, drawn by the code under the GAD_NIX area).
        ui::DrawItem parchment;
        parchment.kind = ui::DrawItem::Kind::Sprite;
        parchment.gfx = kGfxParchment;
        m_renderer->draw({parchment}, map.x, map.y, s);
    }
    if (map.w > 0 && m_world_w > 0) {
        const double fit = std::min(static_cast<double>(map.w) / m_world_w, static_cast<double>(map.h) / m_world_h);
        const ui::Rect at{map.x + static_cast<std::int32_t>((map.w - m_world_w * fit) / 2.0),
                          map.y + static_cast<std::int32_t>((map.h - m_world_h * fit) / 2.0),
                          static_cast<std::int32_t>(m_world_w * fit), static_cast<std::int32_t>(m_world_h * fit)};
        m_renderer->image(1, m_minimap, static_cast<std::int32_t>(m_world_w), static_cast<std::int32_t>(m_world_h), at,
                          m_minimap_dirty);
        m_minimap_dirty = false;
        // The visible part of the map as a white outline.
        std::array<SDL_FPoint, 5> points{};
        for (std::size_t i = 0; i < 4; ++i) {
            points[i] = SDL_FPoint{static_cast<float>(at.x + view[i].first * fit),
                                   static_cast<float>(at.y + view[i].second * fit)};
        }
        points[4] = points[0];
        const SDL_Rect clip{at.x, at.y, at.w, at.h};
        SDL_SetRenderClipRect(m_sdl, &clip);
        SDL_SetRenderDrawColor(m_sdl, 255, 255, 255, 255);
        SDL_RenderLines(m_sdl, points.data(), static_cast<int>(points.size()));
        SDL_SetRenderClipRect(m_sdl, nullptr);
    }
    // A status bar wider than its art (wide windows): the art widened, the line at its right end.
    m_renderer->draw_widened(0, kGfxLeiste, m_regions.status_bar, s);
    const std::int32_t extra = std::max(0, static_cast<std::int32_t>(m_regions.status_bar.w / s) - kDesignViewW);
    if (const assets::Gadget* line = m_leist.layout().find(kStatusLine)) {
        m_leist.set_offset(kStatusLine, kLeistTextX + extra, line->offset[1]);
    }
    m_leist.set_text(kStatusLine, status);
    m_renderer->draw(m_leist.draw(m_renderer->fonts()), m_regions.status_bar.x, m_regions.status_bar.y, s);
    draw_question();
    draw_help();
}

}  // namespace o1602::app
