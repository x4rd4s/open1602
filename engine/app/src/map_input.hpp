// SPDX-License-Identifier: GPL-3.0-or-later
//
// Input of the map view (ADR 0010): the key table, selecting ships and land units with the left
// button (click, rectangle, Shift, Ctrl, double click), orders with the right button (Shift: queued;
// also on the minimap), picks for attack and patrol, and the questions before what cannot be taken
// back. The original's mouse (orders with the left button) is an option. Owns what the mouse and
// the keys are doing and the selection; works on the viewer's camera, build tool and panel, and
// asks the viewer (Host) for what only it can do.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "build_tool.hpp"
#include "command_sink.hpp"
#include "game_sound.hpp"
#include "game_state.hpp"
#include "hud.hpp"
#include "map_gestures.hpp"
#include "zoom_graphics.hpp"

#include "o1602/platform/window.hpp"
#include "o1602/render/camera.hpp"
#include "o1602/render/world_renderer.hpp"
#include "o1602/ui/key_bindings.hpp"
#include "o1602/ui/unit_selection.hpp"

union SDL_Event;

namespace o1602::app {

/// The largest pixel scale (Ctrl +, and the screen's scale).
inline constexpr std::int32_t kMaxPixelScale = 4;

class MapInput {
public:
    /// What only the viewer can do; the input asks it.
    class Host {
    public:
        virtual ~Host() = default;
        /// What a click on the frame asked for (also the modes the input switches to).
        virtual void apply_hud(const Hud::Action& action) = 0;
        /// Zooms in or out around a screen point; the zoom level's graphics may have to be loaded.
        virtual void zoom_by(int steps, double sx, double sy) = 0;
        /// F5 and F9: the quick savegame.
        virtual void quick_save() = 0;
        virtual void quick_load() = 0;
        /// F12: a screenshot of the next frame, said in the status line.
        virtual void request_screenshot() = 0;
        virtual void quit() = 0;
        /// Ctrl+O: the ownership overlay.
        virtual void toggle_owners() = 0;
        /// Where Home goes without a selection.
        [[nodiscard]] virtual render::TilePos home_tile() const = 0;
        /// The graphics of the zoom level shown; null while they are not there.
        [[nodiscard]] virtual const ZoomGraphics* graphics() const = 0;
        /// The pictures of a unit as drawn; nothing if it is not drawn.
        [[nodiscard]] virtual std::optional<render::FigureSprites> unit_sprites(const ui::UnitRef& ref) const = 0;

    protected:
        Host() = default;
        Host(const Host&) = default;
        Host& operator=(const Host&) = default;
    };

    /// The viewer's parts the input works on. All outlive the input; the game in `state` and
    /// `local_player` change when another game is loaded.
    struct Parts {
        Host& host;
        const assets::BuildingTable& buildings;
        const assets::FigureTable& figures;
        const std::unique_ptr<GameState>& state;
        const sim::PlayerId& local_player;
        render::Camera& camera;
        BuildTool& tool;
        CommandSink& commands;
        GameSound& sound;
        platform::Window& window;
        Hud* hud;  ///< null without the panel
        /// The original's mouse: the left button gives orders too, the right one only cancels.
        bool original_mouse = false;
        /// Ask before scuttling, disbanding and declaring war by an attack.
        bool confirm = true;
        /// The key settings file of the command line (--keys); empty: the one in the user's folder.
        std::filesystem::path keys_file;
    };

    explicit MapInput(const Parts& parts);
    MapInput(const MapInput&) = delete;
    MapInput& operator=(const MapInput&) = delete;

    /// Applies the key settings file (--keys, else `default_file`) over the defaults, or writes
    /// `default_file` with them.
    void load_key_settings(const std::filesystem::path& default_file);

    /// Input events but the window's (mouse positions in output pixels).
    void handle_event(const SDL_Event& event);
    /// The left button went up while a video took the input.
    void release_left() noexcept { m_left_down = false; }
    /// Every frame: the map scrolls while its keys are held.
    void scroll_with_keys(std::uint32_t delta_ms);
    /// Every frame, in the menu as in the game: the platform's text input runs exactly while a
    /// name is typed.
    void sync_text_input();
    /// Every frame of the game: a window changed the selection (filters, formations), or units
    /// died, sank or changed hands: the windows follow.
    void follow_selection();

    /// The selection changed: the windows follow (a single ship in the info mode, groups and land
    /// units in the military mode).
    void selection_changed(bool box);
    /// Orders to the simulation; those that cannot be taken back wait for an answer (ADR 0010).
    void submit_orders(std::vector<sim::Command> orders, bool sound = true);
    /// The answer to the question before the waiting orders.
    void answer(bool yes);
    /// A building (or the demolish tool) was taken from the menu: the selected ships and units
    /// are let go, so a right click puts the building away instead of giving them an order.
    void drop_units();
    /// Another game: no selection, pick, order or press of the old one is left.
    void reset_game();

    /// Health bars over the selected units and the selection rectangle.
    void draw_overlay();

    /// Island tile under the mouse, nullopt over the sea.
    [[nodiscard]] std::optional<BuildTool::Spot> spot_at_mouse() const;
    [[nodiscard]] double mouse_x() const noexcept { return m_mouse_x; }
    [[nodiscard]] double mouse_y() const noexcept { return m_mouse_y; }
    [[nodiscard]] ui::UnitSelection& units() noexcept { return m_units; }
    [[nodiscard]] const ui::UnitSelection& units() const noexcept { return m_units; }

    /// A pick the next click on the map answers: an attack (A) or a patrol (P).
    enum class Pick : std::uint8_t { None, Attack, Patrol };
    [[nodiscard]] Pick pick() const noexcept { return m_pick; }

    /// The building looked at when the build menu picked its field: its circle stays lit while
    /// that field is placed.
    struct FieldSource {
        std::uint16_t island = 0;
        std::uint16_t x = 0;
        std::uint16_t y = 0;
        std::uint32_t field = 0;  ///< id of the field
    };
    [[nodiscard]] const std::optional<FieldSource>& field_source() const noexcept { return m_field_source; }

private:
    /// The keys of ADR 0010.
    void setup_keys();
    /// F1: the keys as they are bound now, over the map.
    void show_key_help();
    void handle_key(int key, int scancode, std::uint16_t mod);
    [[nodiscard]] ui::KeyContext key_context() const;
    void run_action(const ui::KeyBinding& binding);
    /// Switches the panel to a mode (and what that asks of the viewer).
    void set_mode(std::int32_t mode);
    /// Tells the panel which building is under the mouse for its window (none: the player status).
    void select_building_at_mouse();
    /// Closes the open pop-up of a build category, else lays the building in hand away (the
    /// original's right click). False if there was neither.
    bool cancel_build_step();
    /// A right click on the panel, or on the map with nothing to order: straight back to the
    /// player status, the general status page, from any mode, window or page of one. False if it
    /// was showing already (the click is an order then).
    bool back_to_status();
    /// Pages a window or the settings go back at most in one click (a guard against a loop).
    static constexpr int kMaxBackSteps = 8;
    /// Screen position of a world position in tiles.
    [[nodiscard]] std::pair<double, double> screen_of(double tx, double ty) const;
    /// Where a unit is drawn on screen: its box (x, y, w, h).
    [[nodiscard]] std::optional<std::array<double, 4>> unit_box(const ui::UnitRef& ref) const;
    /// Ships and land units whose picture lies in the rectangle (screen), with the part inside.
    [[nodiscard]] std::vector<ui::UnitCandidate> units_in(double x0, double y0, double x1, double y1) const;
    /// The unit under a screen point, the local player's first.
    [[nodiscard]] std::optional<ui::UnitRef> unit_at(double sx, double sy) const;
    /// Left button released on the map (no tool): a click or the end of a rectangle.
    void map_left_up(double x, double y, std::uint16_t mod, int clicks);
    /// An order of the selection to the screen point (right button; the left one with the
    /// original's mouse); `queue` with Shift.
    void map_order(double x, double y, bool queue);
    /// An order of the selection to a world position (the minimap).
    void order_to_tile(double tx, double ty, bool queue);
    /// Escape or the right button (original mouse): out of a pick, back a page, deselect.
    void cancel_action();
    /// The orders as submit_orders() gives them, `ask` first if they cannot be taken back.
    void submit(std::vector<sim::Command> orders, bool sound, bool ask);

    [[nodiscard]] const sim::World& world() const noexcept { return m_state->world; }
    [[nodiscard]] const sim::Simulation& simulation() const noexcept { return m_state->simulation; }
    [[nodiscard]] sim::PlayerId local() const noexcept { return m_local_player; }

    // The viewer's parts (Parts).
    Host& m_host;
    const assets::BuildingTable& m_buildings;
    const assets::FigureTable& m_figures;
    const std::unique_ptr<GameState>& m_state;
    const sim::PlayerId& m_local_player;
    render::Camera& m_camera;
    BuildTool& m_tool;
    CommandSink& m_commands;
    GameSound& m_sound;
    platform::Window& m_window;
    Hud* m_hud;
    bool m_original_mouse;
    bool m_confirm;
    std::filesystem::path m_keys_file;

    ui::KeyBindings m_keys;
    ui::UnitSelection m_units;  ///< the selected ships or land units
    Pick m_pick = Pick::None;
    LeftPress m_press;  ///< the left button pressed on the map
    MapDrag m_drag;     ///< the map dragged with the right or middle button
    GroupKey m_group_key;
    std::size_t m_formation = 0;                 ///< the next formation of the F key
    std::vector<sim::Command> m_pending_orders;  ///< waiting for the answer to a question
    std::optional<FieldSource> m_field_source;
    double m_mouse_x = 0.0;  ///< in output pixels
    double m_mouse_y = 0.0;
    bool m_left_down = false;   ///< the left mouse button, for the frame's buttons
    bool m_text_input = false;  ///< the platform's text input is on (a name is typed)
};

}  // namespace o1602::app
