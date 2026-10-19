// SPDX-License-Identifier: GPL-3.0-or-later
//
// The original's frame around the map (docs/re/ui.md): the panel on the right with the minimap,
// compass, rotate and zoom buttons and the four modes (CTRL.GAD), and the status bar below the map
// with the status line and the icons of the last three messages (LEIST.GAD). Below the head the
// mode's panel: the build menu or the player status (side_panels.hpp); the windows of the
// selections come later.
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "end_screen.hpp"
#include "main_menu.hpp"
#include "multiplayer_menu.hpp"
#include "options_panel.hpp"
#include "selection_panels.hpp"
#include "side_panels.hpp"

#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/palette.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/render/banner.hpp"
#include "o1602/render/sdl_ui_renderer.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/ui/panel.hpp"

struct SDL_Renderer;

namespace o1602::app {

class Hud {
public:
    /// What a click on the frame asks for.
    struct Action {
        enum class Kind : std::uint8_t {
            RotateLeft,
            RotateRight,
            ZoomIn,
            ZoomOut,
            Minimap,
            Message,
            Build,         ///< build mode with `building`
            Demolish,      ///< build mode, the demolish tool
            TurnBuilding,  ///< the building to place by `step` quarters
            ToolOff,       ///< another mode: no building tool
            OrientToRoad,  ///< buildings with a door turn to the road (`step` 1) or not (0)
            FoundKontor,   ///< the building tool places a Kontor paid from `ship`
            Answer,        ///< the question was answered: `step` 1 yes, 0 no
            SelectShip,    ///< select `ship` and look at it (the ship list)
            Rating,        ///< the score sheets of the running game ("Bewertung")
        };
        Kind kind = Kind::RotateLeft;
        double tile_x = 0.0;  ///< Minimap and Message: the world tile to look at
        double tile_y = 0.0;
        std::uint32_t building = 0;
        int step = 0;
        std::uint32_t ship = 0;
    };
    /// The modes of the panel (CTRL.GAD).
    static constexpr std::int32_t kModeBuild = 30011;
    static constexpr std::int32_t kModeMilitary = 30012;
    static constexpr std::int32_t kModeInfo = 30013;
    static constexpr std::int32_t kModeOptions = 30014;

    /// Nothing if the layouts or the panel's art are missing (the viewer then runs without frame).
    static std::unique_ptr<Hud> create(SDL_Renderer* renderer, const assets::GameDirectory& game,
                                       const assets::Palette& palette, const assets::TextTable* texts);

    /// Lays the frame out for the window; returns the rectangle left for the map.
    ui::Rect layout(std::int32_t width, std::int32_t height);
    [[nodiscard]] const ui::Rect& game_view() const noexcept { return m_regions.game_view; }
    /// Whether a point belongs to the frame (the map does not see the mouse there).
    [[nodiscard]] bool covers(std::int32_t x, std::int32_t y) const noexcept;

    /// The mouse in window pixels; returns what a click asked for.
    std::optional<Action> mouse(std::int32_t x, std::int32_t y, bool down);

    /// A message for the local player (MELDUNG number) at a world tile, in the colour of `banner`.
    void message(std::uint16_t id, sim::BannerId banner, std::optional<std::pair<double, double>> tile,
                 std::uint64_t now_ms);
    /// What the resource bar at the top of the map shows (Hud_DrawResourceBar 0x0040D160): the
    /// player's money, and of the city under the mouse its tools, wood and bricks (own city) and
    /// inhabitants with the owner's shield. Costs of a pending build order in brackets.
    struct Resources {
        std::optional<std::int64_t> money;  ///< nothing: "???" (a player who is not human)
        std::optional<std::int64_t> money_cost;
        std::optional<std::array<std::int64_t, 3>> goods;  ///< tools, wood, bricks in t (own city)
        std::optional<std::array<std::int64_t, 3>> goods_cost;
        std::optional<std::int64_t> inhabitants;  ///< the city under the mouse (a discovered island)
        std::int32_t shield = -1;                 ///< colour of a human owner, -1 the plain shield
    };
    void set_resources(Resources r) { m_resources = std::move(r); }
    /// The local player's points for the info panel, and whether its "Bewertung" may be pressed.
    void set_points(std::int64_t points, bool rating) {
        if (m_info) {
            m_info->set_points(points, rating);
        }
    }
    /// Text of the status line when the mouse is on no gadget (the build tool's report).
    void set_status(std::string text) { m_status = std::move(text); }
    /// The status line over grass and trees of an explored island (0x0040A3D0, Text.cod GAME 22):
    /// "Geeignet für: Tabak: 100% - ...", for the climate's three crops (ware, fertility in percent).
    [[nodiscard]] std::string crops_line(const std::array<std::pair<std::uint8_t, std::uint32_t>, 3>& crops) const;
    /// The view rotation for the compass, 0..3.
    void set_rotation(std::uint8_t r) noexcept { m_rotation = r; }
    /// The rotation of the building to place (the preview turns with it).
    void set_building_rotation(std::uint8_t r) noexcept { m_building_rotation = r; }
    /// Which buildings the local player may pick (the build menu).
    void set_available(BuildPanel::Available available) { m_available = std::move(available); }
    [[nodiscard]] std::int32_t mode() const noexcept { return m_mode; }
    /// The game the windows of the selections read, and who plays here.
    void set_game(const sim::Simulation& simulation, const assets::BuildingTable& buildings,
                  const assets::FigureTable& figures, sim::PlayerId local) {
        m_simulation = &simulation;
        m_buildings = &buildings;
        m_figure_table = &figures;
        m_local = local;
    }
    /// The selected ships and land units: their windows show in the military mode.
    void set_units(ui::UnitSelection* units) noexcept { m_units = units; }
    /// What the player selected on the map; its window shows in the info mode.
    void set_selection(const Selection& selection) { m_selection = selection; }
    /// The build menu opens next with this building picked in its category (a farm's field after
    /// looking at the farm, 0x004331B0). False if the menu has no such building.
    bool show_in_build_menu(std::uint32_t id) { return m_build && m_build->show(id); }
    /// Opens the ship or city list or the diplomacy window in the info mode (the buttons of the
    /// player status, L, Shift+L, V), or closes it if it is open; returns what the tool should do.
    std::optional<Action> toggle_window(Selection::Kind kind);
    [[nodiscard]] const Selection& selection() const noexcept { return m_selection; }
    /// Whether a window waits for a click on the map (the info mode).
    [[nodiscard]] bool picking();
    /// That click, world tile.
    void map_click(std::uint32_t x, std::uint32_t y);
    /// Escape: a window goes back a page or stops picking; false if nothing did.
    bool back();
    /// Commands the windows gave since the last call.
    [[nodiscard]] std::vector<sim::Command> take_commands() { return std::exchange(m_commands, {}); }
    /// Switches the mode (also from the keys); returns what the tool should do.
    std::optional<Action> set_mode(std::int32_t mode);

    /// The minimap's window on the screen (videos of messages play there, ADR 0010).
    [[nodiscard]] ui::Rect minimap_area() const { return minimap_rect(); }
    /// The world tile of a point on the minimap, nothing elsewhere.
    [[nodiscard]] std::optional<std::pair<double, double>> minimap_tile(std::int32_t x, std::int32_t y) const;
    /// The place of the newest message that has one (Space jumps there).
    [[nodiscard]] std::optional<std::pair<double, double>> last_message_tile() const;
    /// A question over the map with yes and no (ADR 0010: before scuttling, disbanding, war); the
    /// answer comes as Action::Answer from mouse() or answer().
    void ask(std::string question) {
        m_question = std::move(question);
        m_modal_press = ModalPress::None;
    }
    [[nodiscard]] bool asking() const noexcept { return m_question.has_value(); }
    void close_question() noexcept {
        m_question.reset();
        m_modal_press = ModalPress::None;
    }
    /// The key overview over the map (F1, ADR 0010): lines of keys and what they do; a heading has
    /// no keys. Any click closes it.
    struct HelpLine {
        std::string keys;
        std::string text;
    };
    void show_help(std::vector<HelpLine> lines) { m_help = std::move(lines); }
    [[nodiscard]] bool helping() const noexcept { return m_help.has_value(); }
    /// A question or the key overview is open: it takes every click.
    [[nodiscard]] bool modal() const noexcept { return m_question.has_value() || m_help.has_value(); }
    void close_help() noexcept { m_help.reset(); }
    /// Closes the build menu's pop-up of a category (public buildings ...); false if none was open.
    bool close_build_popup() noexcept {
        if (m_build && m_build->popup_open()) {
            m_build->close_popup();
            return true;
        }
        return false;
    }
    /// Another game was loaded: messages, selection, questions and the minimap of the old one go.
    void reset_game();
    /// The settings (mode 30014): what they show, and what they asked for since the last call.
    [[nodiscard]] OptionsModel& options_model() noexcept { return m_options_model; }
    [[nodiscard]] std::vector<OptionsEvent> take_option_events() { return std::exchange(m_option_events, {}); }
    /// A name is being typed (a savegame's, the player's in the menu): the keys go there
    /// (Windows-1252 text, Backspace 8, Enter 13).
    [[nodiscard]] bool typing() const noexcept {
        if (m_menu_active) {
            return multiplayer_open() ? m_multiplayer->typing() : m_menu && m_menu->typing();
        }
        return m_options && m_mode == kModeOptions && m_options->editing();
    }
    void type_text(std::string_view text) {
        if (multiplayer_open()) {
            m_multiplayer->type(text);
        } else if (m_menu_active && m_menu) {
            m_menu->type(text);
        } else if (m_options) {
            m_options->type(text);
        }
    }
    void type_key(int key) {
        if (multiplayer_open()) {
            for (MultiplayerEvent& e : m_multiplayer->key(m_multiplayer_model, key)) {
                m_multiplayer_events.push_back(std::move(e));
            }
        } else if (m_menu_active && m_menu) {
            if (auto e = m_menu->key(key)) {
                m_menu_events.push_back(std::move(*e));
            }
        } else if (m_options) {
            if (auto e = m_options->key(key)) {
                m_option_events.push_back(std::move(*e));
            }
        }
    }

    /// The main menu over the whole window (no game shows meanwhile); nothing if its layouts are
    /// missing.
    [[nodiscard]] MainMenu* menu() noexcept { return m_menu.get(); }
    [[nodiscard]] bool menu_active() const noexcept { return m_menu_active; }
    void open_menu() {
        if (m_menu) {
            m_menu_active = true;
            m_multiplayer_shown = false;
            m_menu->open();
        }
    }
    void close_menu() noexcept {
        m_menu_active = false;
        m_multiplayer_shown = false;
    }
    /// The multiplayer menu over the whole window, in place of the main menu (`notice`: why the
    /// last session ended). Its screens show m_multiplayer_model, which the viewer keeps up to date.
    [[nodiscard]] MultiplayerMenu* multiplayer() noexcept { return m_multiplayer.get(); }
    [[nodiscard]] bool multiplayer_open() const noexcept { return m_menu_active && m_multiplayer_shown; }
    void open_multiplayer(std::string notice = {}) {
        if (m_menu) {
            m_menu_active = true;
            m_multiplayer_shown = true;
            m_multiplayer->open(std::move(notice));
        }
    }
    /// Back to the main menu.
    void close_multiplayer() noexcept { m_multiplayer_shown = false; }
    [[nodiscard]] MultiplayerModel& multiplayer_model() noexcept { return m_multiplayer_model; }
    [[nodiscard]] std::vector<MultiplayerEvent> take_multiplayer_events() {
        return std::exchange(m_multiplayer_events, {});
    }
    [[nodiscard]] std::vector<MenuEvent> take_menu_events() { return std::exchange(m_menu_events, {}); }
    /// The score sheets over the whole window, like the menu (menu_active() holds meanwhile, so
    /// the game stands still behind them, as in the original). The player's choice comes back
    /// through take_end_choice().
    void open_end_screen(sim::EndScreenSummary summary, bool view_only);
    [[nodiscard]] bool end_screen_active() const noexcept { return m_end_active; }
    void end_screen_step(int direction) { m_end.step(direction); }
    [[nodiscard]] std::optional<EndScreen::Choice> take_end_choice() { return std::exchange(m_end_choice, {}); }
    /// Leaves the end screen: the main menu (Resume, or no menu: the game).
    void close_end_screen(EndScreen::Choice choice);
    /// The menu's picture frame on the screen (the credits play there).
    [[nodiscard]] ui::Rect menu_video_rect() const;
    void draw_menu();

    /// Draws the frame. `view` are the corners of the visible map in world tiles (the minimap's
    /// frame), `world` the game for the minimap (kept up to date one island per frame).
    void draw(const sim::World& world, const assets::BuildingTable& buildings, const render::BannerTable& banners,
              const std::array<std::pair<double, double>, 4>& view, std::uint64_t now_ms,
              const sim::PlayerFigures& figures, std::uint8_t colour);
    /// After the frame is shown (the text cache keeps what the frame drew).
    void next_frame() noexcept { m_renderer->next_frame(); }

private:
    Hud(SDL_Renderer* renderer, const assets::GameDirectory& game, const assets::Palette& palette,
        assets::GadgetLayout ctrl, assets::GadgetLayout leist, const assets::TextTable* texts);
    /// What the build tool should do in the build mode now.
    [[nodiscard]] std::optional<Action> build_tool_action();
    void draw_resources();
    void build_minimap(const sim::World& world, const assets::BuildingTable& buildings,
                       const render::BannerTable& banners);
    /// Redraws one island's tiles on the minimap; true if a pixel changed.
    bool refresh_minimap_island(const sim::World& world, const assets::BuildingTable& buildings,
                                const render::BannerTable& banners, std::size_t island);
    [[nodiscard]] ui::Rect minimap_rect() const;

    struct Event {
        std::uint16_t id = 0;
        sim::BannerId banner = 0;
        std::optional<std::pair<double, double>> tile;
        std::uint64_t time_ms = 0;
    };

    SDL_Renderer* m_sdl;
    assets::Palette m_palette;
    std::unique_ptr<render::SdlUiRenderer> m_renderer;
    ui::Panel m_ctrl;
    ui::Panel m_leist;
    ui::ScreenRegions m_regions;
    std::optional<assets::TextTable> m_texts;
    std::vector<std::string> m_leiste;   ///< Text.cod [LEISTE]: help texts of the status line
    std::vector<std::string> m_meldung;  ///< Text.cod [MELDUNG]
    std::string m_status;
    std::uint8_t m_rotation = 0;
    std::uint8_t m_building_rotation = 0;
    std::int32_t m_mode = kModeInfo;  ///< the original starts in the info mode (0x0042BEF7)
    std::unique_ptr<BuildPanel> m_build;
    std::unique_ptr<InfoPanel> m_info;
    BuildPanel::Available m_available;
    Resources m_resources;
    std::unique_ptr<SelectionPanels> m_selection_panels;
    Selection m_selection;
    ui::UnitSelection* m_units = nullptr;
    const sim::Simulation* m_simulation = nullptr;
    const assets::BuildingTable* m_buildings = nullptr;
    const assets::FigureTable* m_figure_table = nullptr;
    sim::PlayerId m_local = sim::kNoPlayer;
    std::vector<sim::Command> m_commands;
    /// The window of the current selection, nothing without one or without the game.
    [[nodiscard]] SelectionPanel* selection_panel(std::optional<PanelContext>& context);
    std::int32_t m_gfx_baumat = 46;  ///< TOOLS.INC GFXBAUMAT
    std::deque<Event> m_events;      ///< newest first, at most three
    std::vector<std::uint8_t> m_minimap;
    std::uint64_t m_minimap_ms = 0;
    bool m_minimap_dirty = true;     ///< the texture must be uploaded again
    bool m_minimap_changed = false;  ///< pixels changed since the last upload
    std::size_t m_minimap_islands = 0;
    std::size_t m_minimap_next = 0;  ///< the island refreshed next
    std::uint32_t m_world_w = 0;
    std::uint32_t m_world_h = 0;
    /// Top left of the menu's 1024 x 768 design on the screen (centred).
    void menu_origin(std::int32_t& x, std::int32_t& y) const;
    std::unique_ptr<MainMenu> m_menu;
    std::unique_ptr<MultiplayerMenu> m_multiplayer;  ///< with the main menu
    MultiplayerModel m_multiplayer_model;
    bool m_multiplayer_shown = false;
    std::vector<MultiplayerEvent> m_multiplayer_events;
    bool m_menu_active = false;
    std::vector<MenuEvent> m_menu_events;
    EndScreen m_end;
    bool m_end_active = false;
    std::optional<EndScreen::Choice> m_end_choice;
    std::unique_ptr<OptionsPanel> m_options;
    OptionsModel m_options_model;
    std::vector<OptionsEvent> m_option_events;
    std::optional<ui::Panel> m_tutor;  ///< the mission's window
    /// Its gadgets at the right end with their places in the layout, and the text's size there.
    std::vector<std::pair<std::int32_t, std::array<std::int32_t, 2>>> m_tutor_right;
    std::array<std::int32_t, 2> m_tutor_text_size{};
    std::int32_t m_tutor_line = 0;  ///< first line of its text shown
    std::optional<std::string> m_question;
    std::optional<std::vector<HelpLine>> m_help;
    ui::Rect m_yes;  ///< the buttons of the question as last drawn
    ui::Rect m_no;
    /// Where the left button went down while the question or the key overview was shown: they
    /// answer to a click (press and release), never to the pointer merely passing over a button.
    enum class ModalPress : std::uint8_t { None, Yes, No, Elsewhere };
    ModalPress m_modal_press = ModalPress::None;
    void draw_question();
    /// "Auftrag ansehen": the mission's text in the window above the status bar (TUTOR.GAD).
    void draw_mission(const sim::World& world);
    void draw_help();
};

}  // namespace o1602::app
