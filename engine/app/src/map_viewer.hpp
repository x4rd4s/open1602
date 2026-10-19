// SPDX-License-Identifier: GPL-3.0-or-later
//
// First interactive program: shows the world map of a scenario or savegame and lets the user
// scroll, zoom and rotate it. Becomes the in-game map view once the simulation runs.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "app_settings.hpp"
#include "build_tool.hpp"
#include "command_sink.hpp"
#include "event_presenter.hpp"
#include "game_sound.hpp"
#include "game_state.hpp"
#include "hud.hpp"
#include "map_input.hpp"
#include "multiplayer_menu.hpp"
#include "video_controller.hpp"
#include "zoom_graphics.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/palette.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/result.hpp"
#include "o1602/net/enet_transport.hpp"
#include "o1602/net/lan.hpp"
#include "o1602/net/session.hpp"
#include "o1602/platform/window.hpp"
#include "o1602/render/ambient.hpp"
#include "o1602/render/animation_clock.hpp"
#include "o1602/render/camera.hpp"
#include "o1602/render/graphics_preloader.hpp"
#include "o1602/render/land_figures.hpp"
#include "o1602/render/sdl_sprite_renderer.hpp"
#include "o1602/render/sprite_atlas.hpp"
#include "o1602/render/sprite_hit_mask.hpp"
#include "o1602/render/walker_figures.hpp"
#include "o1602/render/world_graphics.hpp"
#include "o1602/sim/new_game.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/savegame_writer.hpp"
#include "o1602/sim/score.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/ui/key_bindings.hpp"
#include "o1602/ui/unit_selection.hpp"

union SDL_Event;

namespace o1602::app {

struct ViewerOptions {
    std::filesystem::path scenario;    ///< scenario (.szs / .szm), savegame (.gam) or Open1602 savegame (.o1602)
    std::uint32_t max_frames = 0;      ///< stop after N frames (0 = run until closed); for smoke tests
    std::filesystem::path screenshot;  ///< write the last frame as PNG before exiting
    std::filesystem::path save;        ///< save the game before exiting (.o1602, or .gam for the original)
    render::Zoom zoom = render::Zoom::Large;
    bool show_owners = false;  ///< start with the ownership view
    /// Show figures at the smallest zoom level from half-size graphics of the middle level (the
    /// original shows none there). Off: only the sheets the original has.
    bool derive_small_figures = true;
    /// The original red/blue/yellow/white flag images instead of the tinted generic flag.
    bool original_flags = false;
    std::int32_t width = 1280;
    std::int32_t height = 800;
    sim::Rules rules;  ///< settings of the game (refund on demolishing, ...)
    /// Start a new endless game instead of loading `scenario`.
    std::optional<sim::NewGameOptions> new_game;
    /// How the islands of a scenario that makes them at the start are drawn (also --scenario
    /// Szenes/Endlosspiel00.szs).
    sim::WorldStart world;
    bool frame = true;    ///< the original's panel and status bar around the map
    bool effects = true;  ///< sounds and voice messages (also off in the original's Game.dat)
    bool music = true;
    bool videos = true;                  ///< the videos of messages, victory, defeat and pause
    bool intro = false;                  ///< the logos and the intro before the game (the original always shows them)
    std::optional<std::uint16_t> video;  ///< one video before the game (VideoSmk number)
    /// The original's mouse: the left button gives orders too, the right one only cancels
    /// (ADR 0010: Open1602 orders with the right button).
    bool original_mouse = false;
    /// Ask before scuttling, disbanding and declaring war by an attack (ADR 0010).
    bool confirm = true;
    /// The key settings (ADR 0010); empty: keys.cfg in the user's data folder, written with the
    /// defaults if it is not there yet.
    std::filesystem::path keys_file;
    /// The language of Open1602's own texts ("de", "en", ...); empty: settings.cfg, else the
    /// language of the installation (ui/localization.hpp).
    std::string language;
    /// Start in the main menu instead of a game (no game was named on the command line).
    bool menu = false;
    /// Mouse input played at given frames (smoke tests of the interface without a person).
    struct ScriptedInput {
        enum class Kind : std::uint8_t { Move, Down, Up, Key };
        Kind kind = Kind::Move;
        std::uint32_t frame = 0;
        float x = 0.0F;
        float y = 0.0F;
        std::int32_t key = 0;   ///< Key: SDL key code
        std::uint16_t mod = 0;  ///< Key: SDL_KMOD_CTRL, SDL_KMOD_SHIFT
        bool right = false;     ///< Down/Up: the right button
    };
    std::vector<ScriptedInput> input;
    /// Select the building on this island tile at the start (island index, x, y) and look at it.
    std::optional<std::array<std::uint16_t, 3>> select;
    std::optional<std::uint32_t> select_ship;     ///< index into the game's ships
    std::optional<std::uint32_t> select_soldier;  ///< index into the game's land units
    /// A multiplayer game (ADR 0011): host the game named, or join a host and play the game it sends.
    struct Multiplayer {
        enum class Role : std::uint8_t { Host, Join };
        Role role = Role::Host;
        std::string address;  ///< Join: the host's name or IPv4 address
        std::uint16_t port = net::kDefaultPort;
        std::string name;      ///< the player's name for the others; empty: the menu's
        std::string password;  ///< Join: the game's password, if it has one
    };
    std::optional<Multiplayer> multiplayer;
};

/// Players five on of a new world (sim::add_players) get the names of their colours in the
/// game's language; players named otherwise keep their names.
void name_more_players(sim::World& world);

/// The running program: the window, the game and its presentation. Coordinates the parts that
/// take the input (MapInput), present the game's events (EventPresenter) and play the videos
/// (VideoController), and runs the frames.
class MapViewer : private MapInput::Host {
public:
    static core::Result<std::unique_ptr<MapViewer>> create(const assets::GameDirectory& game,
                                                           const ViewerOptions& options);
    ~MapViewer() override;
    MapViewer(const MapViewer&) = delete;
    MapViewer& operator=(const MapViewer&) = delete;

    /// Main loop; returns the process exit code.
    int run();

private:
    MapViewer(assets::GameDirectory game, assets::BuildingTable buildings, sim::World world, assets::Palette palette,
              assets::FigureTable figures, ViewerOptions options);

    /// Starts loading the zoom levels in the background (window and banners must be there).
    void start_preloading();
    /// The graphics of `zoom` on the GPU; waits for the background if it is not done yet.
    core::Status ensure_graphics(render::Zoom zoom);
    /// The menu is shown: no world is drawn.
    [[nodiscard]] bool in_menu() const noexcept { return m_hud && m_hud->menu_active(); }
    /// Every frame: uploads the zoom levels the background finished.
    void adopt_preloaded_graphics();
    core::Status install_graphics(render::Zoom zoom, render::PreparedGraphics prepared);
    /// The parts of the world's layers that do not change within a frame (graphics of the zoom
    /// level, figures, banners).
    [[nodiscard]] render::WorldLayers world_layers(const ZoomGraphics& graphics) const;
    /// The pictures of a unit as drawn (ship: hull and flag; land unit: body), with the row and
    /// depth that order them; nothing if it is not drawn.
    [[nodiscard]] std::optional<render::FigureSprites> unit_sprites(const ui::UnitRef& ref) const override;
    /// The graphics of the zoom level shown; null while they are not there.
    [[nodiscard]] const ZoomGraphics* graphics() const override;

    // What the input asks of the viewer (MapInput::Host).
    void quick_save() override;
    void quick_load() override;
    void request_screenshot() override;
    void quit() override { m_running = false; }
    void toggle_owners() override { m_show_owners = !m_show_owners; }

    /// The window's events; the rest goes to the input.
    void handle_event(const SDL_Event& event);
    void update(std::uint32_t delta_ms);
    void render();
    void update_title(double fps);
    /// The English developer readout under O1602_DEBUG_TITLE: the map under the mouse, the selection, fps.
    [[nodiscard]] std::string debug_title(double fps);
    /// Where the camera starts and Home goes: the local player's Kontor, else his first ship,
    /// else the first island.
    [[nodiscard]] render::TilePos home_tile() const override;
    core::Status save_screenshot(const std::filesystem::path& path);
    /// Shows the drawn frame. A screenshot asked for since the last one is read first: SDL can read
    /// the picture back only before it is presented.
    void present();
    /// Where F12 puts the next screenshot: screenshots/open1602-YYYYMMDD-HHMMSS.png in the user's
    /// data folder, never over an older one.
    [[nodiscard]] static std::filesystem::path next_screenshot_path();
    /// Saves the game: Open1602's own savegame, or a .GAM the original loads if the path ends in
    /// .gam. Never into the game's installation.
    core::Status save_game(const std::filesystem::path& path);
    /// The settings and savegame names of the user's data folder into the panel (at the start).
    void load_options();
    /// Chooses the language of Open1602's own texts: --language, else settings.cfg, else the
    /// installation's; catalogues in <data folder>/locale extend the built-in ones.
    void apply_language();
    /// What the settings panel asked for this frame (save, load, quit, sounds, window size).
    void apply_options();
    /// How often the scenery shows itself, from the settings.
    [[nodiscard]] render::Ambient::Config ambient_config() const;
    void save_settings();
    /// Replaces the running game by a savegame (Open1602's own or the original's) while the
    /// program runs (F9); the game goes on as it was if the file cannot be read.
    core::Status load_game(const std::filesystem::path& path);
    /// A savegame or scenario as a world, not started.
    /// `warnings`: how many the loader gave (they are written to the log).
    [[nodiscard]] core::Result<sim::World> read_world(const std::filesystem::path& path,
                                                      std::size_t* warnings = nullptr) const;
    /// The game goes on in another world (a savegame, a scenario, a new game): the new game is
    /// built whole, then everything of the old one goes. `source`: the file it came from, for the
    /// window title when the game has no name of its own. Out of memory leaves the old game running.
    core::Status replace_world(sim::World world, const std::filesystem::path& source = {});
    /// The name of the game for the window title (UTF-8).
    [[nodiscard]] std::string game_title() const;
    /// The multiplayer part of the window title (players, ended, waiting), or nothing.
    [[nodiscard]] std::string session_title() const;
    /// Tells (on stderr) that a savegame's simulation state was damaged (Simulation::state_damaged).
    void warn_if_state_damaged() const;
    /// A game that loaded with `warnings` (parts skipped, other game data) or without its running
    /// state: a note in the status line, the details are in the log.
    void report_load_problems(std::size_t warnings);
    /// What the main menu asked for this frame.
    void apply_menu();
    /// The end screen of a decided game, when it is due (EventPresenter::end_screen_due). Called
    /// every frame the game runs.
    void check_end_screen(std::uint64_t now);
    /// The score sheets: the final screen of a decided game, or the "Bewertung" of a running one.
    void show_end_screen(sim::GameOutcome outcome, bool view_only);
    /// HighScore_UpdateAllHumans (0x00475990): every human still playing, and `defeated` (who is
    /// out at this moment), enters the scenario's high score list.
    void update_high_scores(sim::PlayerId defeated);
    /// A won single player game opens the next mission (GameDat_Write 0x00487FB0).
    void record_progress();
    /// Humans that take part: more than one is a multiplayer game.
    [[nodiscard]] std::size_t humans() const;
    /// How the game stands for the local player now.
    [[nodiscard]] sim::GameOutcome local_outcome() const;
    /// A new endless game of `level` (from the menu): name and banner of the human.
    core::Status start_endless(std::uint8_t level, std::uint8_t colour, const std::string& name);
    /// Open1602's own folder for savegames (the user's data folder, not the installation).
    [[nodiscard]] static std::filesystem::path save_folder();

    /// The user's data folder of Open1602 (settings; the savegames in "saves" below it).
    [[nodiscard]] static std::filesystem::path data_folder();

    // ---- Multiplayer (multiplayer.cpp, ADR 0011) ----
    /// Hosts or joins the multiplayer game of the command line.
    core::Status start_multiplayer();
    /// What the multiplayer session reported this frame: lines of the chat, the menu's screens.
    void apply_session_events();
    /// The game a host sent: replaces the world (also after a desync); nullptr if it cannot be read.
    sim::Simulation* load_from_host(std::span<const std::uint8_t> save);
    /// Fingerprint of the game data the simulation depends on: computers of a multiplayer game
    /// must have the same.
    [[nodiscard]] std::uint64_t content_fingerprint() const;
    /// The multiplayer menu was opened from the main menu: the search starts.
    void open_multiplayer_menu();
    /// The multiplayer menu: its model, what it asked for, the search on the local network.
    void apply_multiplayer_menu();
    void handle_multiplayer(const MultiplayerEvent& e);
    /// Answers searches for the hosted game, in the menu and in the game.
    void update_lan_beacon();
    /// Opens a lobby for `setup` on its port.
    core::Status host_lobby(const HostSetup& setup);
    /// The host starts the game of its lobby: makes the world, gives the slots their players.
    core::Status start_hosted_game();
    core::Status join_game(const std::string& address, std::uint16_t port, const std::string& password);
    /// Drops the session, its beacon and the hosted game; the menus stay as they are.
    void end_session();
    /// Out of the session, back to the search (with `notice`, why).
    void leave_session(std::string notice = {});
    /// The host of the running game left: the game moves on to the successor the host named (host
    /// migration): this computer hosts it on its port, or follows the successor there. False if it
    /// cannot (no successor, the port is taken).
    bool migrate();
    /// The host of the running game left and the game cannot move on: the client saves the game it
    /// holds as a whole and says where; loaded, it plays on alone or is hosted anew.
    void save_after_host_left();
    /// A line for the chat: a player's (`name` given) or a notice of the session (add_chat_line).
    void chat_line(std::string name, std::string text, sim::PlayerId seat = sim::kNoPlayer, bool echo = true);
    [[nodiscard]] net::HostSession* host_session() const noexcept;
    [[nodiscard]] net::ClientSession* client_session() const noexcept;

    /// Tells the sound where the view is (every frame, before events are heard).
    void place_listener();
    /// Gives the map the window without the frame.
    void lay_out();
    /// The window went to a screen with another scale (or the desktop's scale changed): the map's
    /// pixel scale follows; a change the player made with the keys stays on top of it.
    void follow_display_scale();
    /// The render device was reset (a driver update, a GPU timeout, sleep on some laptops): every
    /// texture lost its contents, all are made again.
    void recreate_textures();
    /// The render device was lost: the window's renderer is made again, everything that draws
    /// takes the new one and makes its textures again. Without a renderer the game ends.
    void recreate_renderer();
    /// The world's sprites of every zoom level go and are loaded again.
    void reload_graphics();
    /// What a click on the frame asked for.
    void apply_hud(const Hud::Action& action) override;
    /// The resource bar: money, and goods and inhabitants of the city under the mouse.
    [[nodiscard]] Hud::Resources resources() const;
    /// Shots, hits, victory and defeat: sounds, messages and lines in the terminal.
    void apply_game_events();
    /// Plays what the EventPresenter made of the events: messages with their voices (a message that
    /// asks for it slows the game to normal speed, flag 0x02) and videos, high score lists.
    void show_notices(const std::vector<Notice>& notices);
    /// The pause of the original (Pause key) while a video fills the window: the game stops or
    /// goes on, the pause video loops (VideoController::toggle_pause).
    void toggle_pause_video();
    void zoom_by(int steps, double sx, double sy) override;
    /// What the world dims: while building the land the player cannot build on, or everything
    /// outside the circle of the building being placed or of the player's clicked building.
    [[nodiscard]] render::TileLighting tile_lighting() const;
    /// A catchment given in tiles of `island` in world tiles.
    [[nodiscard]] sim::Catchment world_catchment(std::uint16_t island, sim::Catchment area) const;
    /// Lighting of the local player's building at tile (x, y) of `island`: the land inside its
    /// circle (and a Kontor's harbour); none for other buildings and those without a circle.
    [[nodiscard]] std::optional<render::TileLighting> building_lighting(std::uint16_t island, std::uint16_t x,
                                                                        std::uint16_t y) const;
    /// The status line over grass and trees of an island the player explored: its crops.
    [[nodiscard]] std::string crops_status() const;
    /// Results of building and demolishing (messages, flags of new buildings).
    void apply_construction();
    /// Animation phases of buildings that started or stopped work (presentation only).
    void apply_production_events();
    /// Volcano fire and lava of the current game time (m_effects).
    void collect_effects();
    /// Townsfolk, sea creatures and jugglers of the game time since the last frame.
    void update_ambient();

    /// The pixel scale the screen asked for, last time it was looked at.
    std::int32_t m_display_pixel_scale = 1;

    assets::GameDirectory m_game;
    assets::BuildingTable m_buildings;
    assets::Palette m_palette;
    assets::FigureTable m_figures;
    render::BannerTable m_banners;  ///< colours and flags of the players (ADR 0007)
    /// Flags, smoke and icons: buildings do not move, so they are collected once at load.
    std::vector<render::BuildingFigure> m_building_figures;
    std::vector<std::string> m_city_names;  ///< Text.cod [STAEDTE], [SHIPS]: for every game loaded
    std::vector<std::string> m_ship_names;
    std::vector<std::string> m_campaign_labels;     ///< [KAMPAGNE]: the high score lists of campaign parts
    sim::PlayerId m_local_player = sim::kNoPlayer;  ///< whose buildings show status icons
    render::SoldierFigures m_soldiers;              ///< land units in their owners' colours
    render::WalkerFigures m_walkers;                ///< carriers, carts, workers, deer and hunters
    render::ShipTrail m_ship_trail;                 ///< ships drawn between two ticks
    double m_tick_fraction = 1.0;                   ///< share of the next tick that has passed
    std::uint64_t m_title_real_ms = 0;              ///< real and game time of the last title (the speed reached)
    std::uint64_t m_title_game_ms = 0;
    bool m_debug_title = core::environment_variable("O1602_DEBUG_TITLE").has_value();
    ViewerOptions m_options;
    std::unique_ptr<GameState> m_state;  ///< the running game (after m_buildings, m_figures, m_options)
    /// Messages, the end screen and the notes of the status line (quick save, screenshot, errors).
    EventPresenter m_events;
    /// A multiplayer game: drives the simulation in place of Simulation::advance().
    std::unique_ptr<net::Session> m_session;
    bool m_host_game_loaded = false;                ///< this client runs the game its host sent
    std::string m_host_address;                     ///< of the host joined
    std::uint16_t m_host_port = net::kDefaultPort;  ///< of the host joined: the successor hosts on it too
    bool m_migrating = false;                       ///< following a successor, its game not here yet
    std::string m_successor_name;                   ///< the one followed
    /// The local player's commands: to m_state's simulation, or to m_session while there is one.
    CommandSink m_command_sink;
    std::unique_ptr<net::LanBrowser> m_lan_browser;  ///< while the multiplayer menu looks for games
    std::unique_ptr<net::LanBeacon> m_lan_beacon;    ///< while this computer hosts a game shown on the network
    std::uint64_t m_lan_search_ms = 0;               ///< real time of the last search
    std::uint64_t m_lan_update_ms = 0;               ///< real time of the last look at the answers
    std::optional<HostSetup> m_hosted;               ///< the game this computer hosts
    std::optional<sim::World> m_hosted_world;        ///< its scenario or savegame until the start

    std::unique_ptr<platform::SdlSystem> m_sdl;
    std::unique_ptr<platform::Window> m_window;
    std::array<std::optional<ZoomGraphics>, 3> m_graphics;
    std::unique_ptr<render::GraphicsPreloader> m_preloader;  ///< the CPU side of the zoom levels

    render::Camera m_camera;
    render::AnimationClock m_clock;
    render::ProductionPhases m_production_phases;  ///< animation phases, presentation only
    std::vector<render::DrawCommand> m_commands;   ///< reused every frame
    render::DrawListScratch m_draw_scratch;        ///< reused every frame

    bool m_running = true;
    bool m_show_owners = false;   ///< ownership overlay (key O)
    bool m_quit_pending = false;  ///< "Ende" waits for the answer to its question
    AppSettings m_app_settings;   ///< settings.cfg in the user's data folder
    SaveSlots m_slots;            ///< names of the savegames of the panel DISK
    std::size_t m_sprites_drawn = 0;
    BuildTool m_tool;
    std::string m_tool_status;        ///< what the build tool would do at the mouse
    std::size_t m_load_problems = 0;  ///< of the game loaded last (report_load_problems)
    struct ScreenshotRequest {
        std::filesystem::path path;
        bool notify = false;  ///< say in the status line how it went (F12)
    };
    std::optional<ScreenshotRequest> m_screenshot;  ///< read in the next present()
    std::optional<std::string> m_screenshot_error;  ///< why the last one failed
    std::vector<render::EffectFigure> m_effects;    ///< volcano fire, lava, sea creatures, jugglers of this frame
    render::Ambient m_ambient;                      ///< life in the picture, no part of the game
    std::unique_ptr<GameSound> m_sound;             ///< effects, voices and music (after the window)
    std::unique_ptr<Hud> m_hud;                     ///< panel and status bar (after the window), may be null
    /// Mouse and keys on the map, the selection (after the window, the sound and the panel).
    std::unique_ptr<MapInput> m_input;
    std::vector<render::TileMark> m_tile_marks;    ///< red ground under what cannot be built, a dragged field
    render::TileLighting m_lighting;               ///< tiles drawn darker this frame
    std::optional<sim::BuildBill> m_pending_bill;  ///< costs of the order the build tool shows
    sim::PlayerFigures m_player_figures;           ///< the info panel's figures, refreshed once a second
    std::int64_t m_player_points = 0;              ///< the info panel's "Punkte", refreshed with them
    std::uint64_t m_figures_ms = 0;
    /// The videos (after the window and the sound); plays none without videos.
    std::unique_ptr<VideoController> m_video;
};

}  // namespace o1602::app
