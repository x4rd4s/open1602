// SPDX-License-Identifier: GPL-3.0-or-later
#include "map_viewer.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <random>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/high_scores.hpp"
#include "o1602/assets/sprite_sheet.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/png_writer.hpp"
#include "o1602/core/text.hpp"
#include "o1602/render/world_renderer.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/growth.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

/// Why a building cannot go here, in the player's language, for the status line.
std::string refusal_text(sim::BuildRefusal refusal) {
    const char* key = [refusal] {
        switch (refusal) {
            case sim::BuildRefusal::None:
                return "build.refusal.ok";
            case sim::BuildRefusal::UnknownBuilding:
                return "build.refusal.unknown";
            case sim::BuildRefusal::OutsideIsland:
                return "build.refusal.outside_island";
            case sim::BuildRefusal::Terrain:
                return "build.refusal.terrain";
            case sim::BuildRefusal::Territory:
                return "build.refusal.territory";
            case sim::BuildRefusal::ForeignCity:
                return "build.refusal.foreign_city";
            case sim::BuildRefusal::CityLimit:
                return "build.refusal.city_limit";
            case sim::BuildRefusal::SecondKontor:
                return "build.refusal.second_kontor";
            case sim::BuildRefusal::NoCoast:
                return "build.refusal.no_coast";
            case sim::BuildRefusal::NoDeposit:
                return "build.refusal.no_deposit";
            case sim::BuildRefusal::NoRock:
                return "build.refusal.no_rock";
            case sim::BuildRefusal::NoRiver:
                return "build.refusal.no_river";
            case sim::BuildRefusal::UnitInTheWay:
                return "build.refusal.unit_in_the_way";
            case sim::BuildRefusal::Locked:
                return "build.refusal.locked";
            case sim::BuildRefusal::Limit:
                return "build.refusal.limit";
            case sim::BuildRefusal::Infertile:
                return "build.refusal.infertile";
            case sim::BuildRefusal::Overlap:
                return "build.refusal.overlap";
            case sim::BuildRefusal::Money:
                return "build.refusal.money";
            case sim::BuildRefusal::Tools:
                return "build.refusal.tools";
            case sim::BuildRefusal::Wood:
                return "build.refusal.wood";
            case sim::BuildRefusal::Bricks:
                return "build.refusal.bricks";
            case sim::BuildRefusal::Cannons:
                return "build.refusal.cannons";
            case sim::BuildRefusal::ShipTooFar:
                return "build.refusal.ship_too_far";
        }
        return "build.refusal.unknown";
    }();
    return ui::tr(key);
}

/// Ground under a building that cannot be built there.
constexpr assets::Rgba kRefusedTint{220, 30, 30, 140};
/// Real time a frame may spend on ticks; the rest of the frame draws.
constexpr int kTickBudgetMs = 25;
/// The end screen comes this long after the message of a decided game when it has no video,
/// after an own defeat without one the shorter time (docs/re/missions.md "Sieg"; with a video it
/// comes when the video ends).
constexpr std::uint64_t kEndScreenDelayMs = 5000;
constexpr std::uint64_t kDefeatDelayMs = 500;

/// What the help bar of the original shows over grass and trees of an explored island
/// (0x0040A3D0): the three crops of the climate (north sugar cane, grapes, tobacco; south spices,
/// cotton, cocoa) with their fertility in percent.
std::array<std::pair<std::uint8_t, std::uint32_t>, 3> island_crops(const sim::WorldIsland& island) {
    static constexpr std::array<std::uint8_t, 3> kNorth = {48, 50, 46};
    static constexpr std::array<std::uint8_t, 3> kSouth = {47, 49, 51};
    std::array<std::pair<std::uint8_t, std::uint32_t>, 3> crops{};
    const std::array<std::uint8_t, 3>& wares = island.header.climate == 1 ? kSouth : kNorth;
    for (std::size_t i = 0; i < crops.size(); ++i) {
        crops[i] = {wares[i], sim::island_fertility(island, wares[i]) * 100 / 128};
    }
    return crops;
}

/// The same for the console and the window title.
std::string crops_text(const sim::WorldIsland& island) {
    std::string text;
    for (const auto& [ware, percent] : island_crops(island)) {
        if (!text.empty()) {
            text += ", ";
        }
        text += "#" + std::to_string(ware) + " " + std::to_string(percent) + " %";
    }
    return text;
}

const char* zoom_name(render::Zoom zoom) {
    switch (zoom) {
        case render::Zoom::Large:
            return "GFX";
        case render::Zoom::Medium:
            return "MGFX";
        default:
            return "SGFX";
    }
}

std::string player_label(const sim::Player& p, sim::PlayerId slot, const render::BannerTable& banners) {
    std::string label = "player " + std::to_string(slot);
    if (const render::Banner* b = banners.get(p.banner)) {
        const auto colon = b->id.find(':');
        label += " " + (colon == std::string::npos ? b->id : b->id.substr(colon + 1));
    }
    return label + " (" + sim::role_name(p.role) + ")";
}

}  // namespace

void name_more_players(sim::World& world) {
    // Only a game that has not run yet: later a person may have taken a slot and its name.
    if (world.time_ms() != 0) {
        return;
    }
    // In the order of render::BannerTable::add_more_players.
    static constexpr std::array<const char*, sim::kMaxPlayers - sim::kOriginalPlayers> kKeys = {
        "player.colour.orange",     "player.colour.green", "player.colour.teal",   "player.colour.pink",
        "player.colour.light_blue", "player.colour.lime",  "player.colour.purple", "player.colour.brown"};
    for (std::size_t n = sim::kOriginalPlayers; n < sim::kMaxPlayers; ++n) {
        sim::Player* p = world.player(sim::player_slot(n));
        if (p != nullptr && p->role == sim::PlayerRole::Computer &&
            p->banner == sim::kFirstExtraBanner + (n - sim::kOriginalPlayers) &&
            p->name == "Player " + std::to_string(n + 1)) {
            p->name = ui::tr(kKeys[n - sim::kOriginalPlayers]);
        }
    }
}

core::Result<std::unique_ptr<MapViewer>> MapViewer::create(const assets::GameDirectory& game,
                                                           const ViewerOptions& options) {
    auto buildings = assets::BuildingTable::load(game);
    if (!buildings) {
        return buildings.error();
    }
    auto figures = assets::FigureTable::load(game);
    if (!figures) {
        return figures.error();
    }
    std::vector<std::string> warnings;
    core::Result<sim::World> world = core::make_error(core::ErrorCode::NotFound, "no game");
    const bool joining = options.multiplayer && options.multiplayer->role == ViewerOptions::Multiplayer::Role::Join;
    if (options.menu || joining) {
        world = sim::World(500, 350);  // the sea behind the menu until a game is chosen or comes from the host
    } else if (options.new_game) {
        auto texts = assets::TextTable::load(game);
        if (!texts) {
            return texts.error();
        }
        world = sim::start_new_game(game, *buildings, *texts, *options.new_game, &warnings);
    } else {
        auto bytes = core::read_file(options.scenario);
        if (!bytes) {
            return bytes.error();
        }
        if (sim::is_open1602_save(*bytes)) {
            world = sim::read_game(*bytes, *buildings, &warnings, &*figures);
        } else {
            auto file = assets::ChunkFile::from_bytes(std::move(bytes).value());
            if (!file) {
                return file.error();
            }
            world = sim::load_world(*file, *buildings, game, &warnings, options.world);
        }
    }
    if (!world) {
        return world.error();
    }
    for (const auto& w : warnings) {
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    }
    auto banners = render::BannerTable::original();
    banners.add_more_players();
    for (std::size_t i = 0; i < world->players().size(); ++i) {
        const auto& p = world->players()[i];
        std::fprintf(stderr, "%s, money %d\n", player_label(p, static_cast<sim::PlayerId>(i), banners).c_str(),
                     p.money);
    }
    std::fprintf(stderr, "%zu islands, %zu ships\n", world->islands().size(), world->ships().size());
    const auto palette_path = game.resolve(render::kTerrainPalettePath);
    if (!palette_path) {
        return core::make_error(core::ErrorCode::NotFound, "toolgfx/stadtfld.col not found");
    }
    auto palette = assets::Palette::load(*palette_path);
    if (!palette) {
        return palette.error();
    }

    std::unique_ptr<MapViewer> viewer(new MapViewer(game, std::move(buildings).value(), std::move(world).value(),
                                                    std::move(palette).value(), std::move(figures).value(), options));
    viewer->m_banners =
        render::BannerTable::original(&viewer->m_figures, render::BannerOptions{options.original_flags});
    viewer->m_banners.add_more_players();
    viewer->m_building_figures =
        render::collect_building_figures(viewer->m_state->world, viewer->m_buildings, viewer->m_figures);
    viewer->m_local_player = sim::first_human_player(viewer->m_state->world);
    if (auto texts = assets::TextTable::load(game)) {
        viewer->m_city_names = texts->section("STAEDTE");
        viewer->m_ship_names = texts->section("SHIPS");
        viewer->m_campaign_labels = texts->section("KAMPAGNE");
        viewer->m_state->simulation.offer_names(viewer->m_city_names, viewer->m_ship_names);
    }
    viewer->m_soldiers.reset(viewer->m_state->world, viewer->m_buildings, viewer->m_figures);
    viewer->m_show_owners = options.show_owners;
    auto sdl = platform::SdlSystem::init();
    if (!sdl) {
        return sdl.error();
    }
    viewer->m_sdl = std::move(sdl).value();
    platform::WindowConfig config;
    config.title = "Open1602";
    config.width = options.width;
    config.height = options.height;
    auto window = platform::Window::create(config);
    if (!window) {
        return window.error();
    }
    viewer->m_window = std::move(window).value();
    // The viewer's own copy: GameSound keeps a pointer, and `game` may be a temporary (UI-15).
    viewer->m_sound = std::make_unique<GameSound>(viewer->m_game, options.effects, options.music);
    if (options.videos) {
        viewer->m_video = std::make_unique<VideoPlayer>(viewer->m_window->sdl_renderer(), viewer->m_game);
        viewer->m_sound->set_video_check(
            [self = viewer.get()](std::uint16_t n) { return self->m_video->available(n); });
        if (options.intro) {
            // The start of the original: the two logos and the intro (0x004959AA, chained 8 -> 9 -> 58).
            for (const std::uint16_t n : std::array<std::uint16_t, 3>{8, 9, 58}) {
                viewer->request_video(n, 0, false, true);
            }
        }
        if (options.video) {
            const std::uint16_t n = *options.video;
            viewer->request_video(n, 0, false, n == 8 || n == 9 || n == 58);
        }
    }
    if (options.frame) {
        const auto texts = assets::TextTable::load(game);
        viewer->m_hud =
            Hud::create(viewer->m_window->sdl_renderer(), game, viewer->m_palette, texts ? &texts.value() : nullptr);
        if (!viewer->m_hud) {
            std::fprintf(stderr, "no panel: the interface's files are missing\n");
        } else {
            viewer->m_hud->set_game(viewer->m_state->simulation, viewer->m_buildings, viewer->m_figures,
                                    viewer->m_local_player);
            viewer->m_hud->set_units(&viewer->m_units);
            viewer->m_hud->set_available([self = viewer.get()](std::uint32_t id) {
                const sim::Player* p = self->m_state->world.player(self->m_local_player);
                const assets::BuildingType* b = self->m_buildings.by_id(id);
                return p != nullptr && b != nullptr && sim::building_available(*p, *b);
            });
        }
    }
    viewer->m_sound->set_listener([self = viewer.get()](std::uint16_t message, std::uint16_t flags,
                                                        std::optional<std::pair<double, double>> tile,
                                                        std::optional<sim::PlayerId> about) {
        if ((flags & audio::kMessageNormalSpeed) != 0 && self->m_state->simulation.speed() > 1) {
            // Fires, plagues and attacks: back to normal speed (0x004893F0), as a command like any
            // other change of the speed.
            self->m_command_sink.submit(
                sim::GameSpeedOrder{self->m_local_player, sim::GameSpeedOrder::Op::SetSpeed, 1});
        }
        if (self->m_hud) {
            // The icon in the colour of the player the message is about (a rival's new city).
            const sim::Player* p = self->m_state->world.player(about.value_or(self->m_local_player));
            self->m_hud->message(message, p != nullptr ? p->banner : 0, tile, platform::ticks_ms());
        }
    });
    viewer->load_key_settings();
    viewer->load_options();
    name_more_players(viewer->m_state->world);  // in the language the options set
    viewer->report_load_problems(warnings.size());
    if (options.new_game && options.new_game->name.empty() && viewer->m_state->world.time_ms() == 0) {
        // A new game started without --name: the name typed in the menu before, else "Nameless".
        if (sim::Player* p = viewer->m_state->world.player(viewer->m_local_player); p != nullptr) {
            p->name = viewer->m_app_settings.player_name.empty() ? ui::tr("menu.default_name")
                                                                 : viewer->m_app_settings.player_name;
        }
    }
    if (options.menu && viewer->m_hud) {
        viewer->m_hud->open_menu();
    }
    std::fprintf(stderr, "renderer: %s, max texture %d\n", viewer->m_window->renderer_name().c_str(),
                 viewer->m_window->max_texture_size());

    std::int32_t w = 0;
    std::int32_t h = 0;
    viewer->m_window->output_size(w, h);
    viewer->m_display_pixel_scale = render::pixel_scale_for(viewer->m_window->display_scale(), kMaxPixelScale);
    viewer->m_camera.set_world_size(viewer->m_state->world.width(), viewer->m_state->world.height());
    viewer->m_camera.set_screen(w, h, viewer->m_display_pixel_scale);
    viewer->lay_out();
    viewer->m_camera.set_zoom(options.zoom, w / 2.0, h / 2.0);
    // Start over the player's own city.
    viewer->m_camera.center_on(viewer->home_tile());
    // Townsfolk and sea creatures follow the game tick by tick, the same on every computer.
    MapViewer* self = viewer.get();
    viewer->m_state->simulation.set_tick_observer(
        [self](const sim::World& now) { self->m_ambient.tick(now, self->m_buildings, self->m_figures); });
    viewer->m_ambient.tick(viewer->m_state->world, viewer->m_buildings, viewer->m_figures);
    if (options.select && viewer->m_hud && (*options.select)[0] < viewer->m_state->world.islands().size()) {
        const auto& [isl, sx, sy] = *options.select;
        const sim::WorldIsland& island = viewer->m_state->world.islands()[isl];
        if (island.map.contains(sx, sy)) {
            const sim::SubTile t = island.map.sub_tile(viewer->m_buildings, sx, sy);
            Selection sel;
            sel.kind = Selection::Kind::Building;
            sel.island = isl;
            sel.x = static_cast<std::uint16_t>(t.building != nullptr ? t.origin_x : sx);
            sel.y = static_cast<std::uint16_t>(t.building != nullptr ? t.origin_y : sy);
            viewer->m_hud->set_selection(sel);
            viewer->m_camera.center_on(render::TilePos{island.x + sx, island.y + sy});
        }
    }
    if (options.select_ship && *options.select_ship < viewer->m_state->world.ships().size()) {
        const sim::Ship& ship = viewer->m_state->world.ships()[*options.select_ship];
        viewer->m_units.select(viewer->m_state->world, ui::UnitRef{ui::UnitRef::Kind::Ship, ship.id});
        viewer->selection_changed(false);
        viewer->m_camera.center_on(render::TilePos{static_cast<std::uint32_t>(std::max<std::int32_t>(ship.x, 0)),
                                                   static_cast<std::uint32_t>(std::max<std::int32_t>(ship.y, 0))});
    }
    if (options.select_soldier && *options.select_soldier < viewer->m_state->world.soldiers().size()) {
        const sim::Soldier& s = viewer->m_state->world.soldiers()[*options.select_soldier];
        viewer->m_units.select(viewer->m_state->world, ui::UnitRef{ui::UnitRef::Kind::Soldier, s.id});
        viewer->selection_changed(false);
        viewer->m_camera.center_on(render::TilePos{static_cast<std::uint32_t>(std::max(0.0, s.x())),
                                                   static_cast<std::uint32_t>(std::max(0.0, s.y()))});
    }
    viewer->start_preloading();
    if (auto status = viewer->ensure_graphics(viewer->m_camera.view().zoom); !status) {
        return status.error();
    }
    if (options.multiplayer) {
        if (auto status = viewer->start_multiplayer(); !status) {
            return status.error();
        }
        if (viewer->client_session() != nullptr && viewer->m_hud && viewer->m_hud->multiplayer() != nullptr) {
            // Joining from the command line: the menu shows the connection and the lobby.
            viewer->m_hud->open_multiplayer();
            viewer->m_hud->multiplayer()->show_connecting(options.multiplayer->address + ":" +
                                                          std::to_string(options.multiplayer->port));
        }
    }
    return viewer;
}

MapViewer::MapViewer(assets::GameDirectory game, assets::BuildingTable buildings, sim::World world,
                     assets::Palette palette, assets::FigureTable figures, ViewerOptions options)
    : m_game(std::move(game)),
      m_buildings(std::move(buildings)),
      m_palette(std::move(palette)),
      m_figures(std::move(figures)),
      m_options(std::move(options)),
      m_state(std::make_unique<GameState>(std::move(world), m_buildings, m_figures, m_options.rules)),
      m_clock(m_buildings),
      m_tool(m_buildings) {
    m_command_sink.set_simulation(&m_state->simulation);
    setup_keys();
    warn_if_state_damaged();
}

void MapViewer::warn_if_state_damaged() const {
    if (m_state->simulation.state_damaged()) {
        std::fprintf(stderr,
                     "warning: the savegame's running state could not be read; the game continues "
                     "from the world alone (computer players, walkers and clocks start anew)\n");
    }
}

MapViewer::~MapViewer() {
    // GPU resources must go before the renderer/window, which go before SDL_Quit: the textures of
    // the video and the HUD as well as the world's sprites.
    m_preloader.reset();  // waits for the zoom level being loaded
    m_video.reset();
    m_hud.reset();
    for (auto& g : m_graphics) {
        g.reset();
    }
    m_sound.reset();
    m_window.reset();
    m_sdl.reset();
}

void MapViewer::start_preloading() {
    render::GraphicsOptions options;
    options.page_size = m_window->max_texture_size() >= 4096 ? 4096U : 2048U;
    options.derive_missing_sheets = m_options.derive_small_figures;
    // The thread works on its own copies: nothing it reads changes under it.
    m_preloader = std::make_unique<render::GraphicsPreloader>(
        [game = m_game, palette = m_palette, banners = m_banners, figures = m_figures, options](render::Zoom zoom) {
            return render::prepare_world_graphics(game, palette, zoom, render::kWorldFigureSheets, options, &banners,
                                                  &figures);
        },
        true);
    // All zoom levels, the one shown first: switching to another one then waits for nothing.
    m_preloader->request(render::preload_order(m_camera.view().zoom));
}

core::Status MapViewer::ensure_graphics(render::Zoom zoom) {
    if (m_graphics[static_cast<std::size_t>(zoom)]) {
        return core::Ok{};
    }
    // Wanted before the background has it: the frames stand still until it is there, the sound
    // goes on.
    const std::uint64_t start = platform::ticks_ms();
    auto prepared = m_preloader->take(zoom, [this] {
        if (m_sound) {
            m_sound->prefill();
        }
    });
    if (const std::uint64_t waited = platform::ticks_ms() - start; waited > 0) {
        std::fprintf(stderr, "%s: waited %llu ms for it\n", zoom_name(zoom), static_cast<unsigned long long>(waited));
    }
    if (!prepared) {
        return prepared.error();
    }
    return install_graphics(zoom, std::move(prepared).value());
}

void MapViewer::adopt_preloaded_graphics() {
    while (auto ready = m_preloader->take_ready()) {
        auto& [zoom, prepared] = *ready;
        if (!prepared) {
            // Tried again (and reported) when the player switches to it.
            std::fprintf(stderr, "warning: %s: %s\n", zoom_name(zoom), prepared.error().message.c_str());
            continue;
        }
        if (m_graphics[static_cast<std::size_t>(zoom)]) {
            continue;
        }
        if (m_sound) {
            m_sound->prefill();  // the upload stands this frame still for a moment
        }
        if (auto status = install_graphics(zoom, std::move(prepared).value()); !status) {
            std::fprintf(stderr, "error: %s\n", status.error().message.c_str());
        }
    }
}

core::Status MapViewer::install_graphics(render::Zoom zoom, render::PreparedGraphics prepared) {
    auto& slot = m_graphics[static_cast<std::size_t>(zoom)];
    const std::uint32_t made_ms = prepared.ms;
    const std::uint64_t start = platform::ticks_ms();
    slot.emplace(ZoomGraphics{std::move(prepared.graphics), nullptr, std::move(prepared.hit_mask)});
    slot->renderer = std::make_unique<render::SdlSpriteRenderer>(m_window->sdl_renderer());
    if (auto status = slot->renderer->upload(slot->graphics.atlas); !status) {
        slot.reset();
        return status;
    }
    slot->graphics.atlas.drop_pixels();
    std::fprintf(stderr,
                 "%s: %zu sprites in %zu page(s) of %u px, figure masks %zu KB, made in %u ms, upload %llu ms\n",
                 zoom_name(zoom), slot->graphics.atlas.sprite_count(), slot->graphics.atlas.pages().size(),
                 slot->graphics.atlas.page_size(), slot->hit_mask.bytes() / 1024, made_ms,
                 static_cast<unsigned long long>(platform::ticks_ms() - start));
    return core::Ok{};
}

int MapViewer::run() {
    std::uint64_t last = platform::ticks_ms();
    const std::uint64_t run_start = last;
    std::uint64_t fps_start = last;
    std::uint32_t fps_frames = 0;
    std::uint32_t frames = 0;
    update_title(0.0);
    while (m_running) {
        adopt_preloaded_graphics();
        SDL_Event event;
        for (const ViewerOptions::ScriptedInput& in : m_options.input) {
            if (in.frame != frames) {
                continue;
            }
            SDL_Event e{};
            switch (in.kind) {
                case ViewerOptions::ScriptedInput::Kind::Move:
                    e.type = SDL_EVENT_MOUSE_MOTION;
                    e.motion.x = in.x;
                    e.motion.y = in.y;
                    break;
                case ViewerOptions::ScriptedInput::Kind::Down:
                case ViewerOptions::ScriptedInput::Kind::Up:
                    e.type = in.kind == ViewerOptions::ScriptedInput::Kind::Down ? SDL_EVENT_MOUSE_BUTTON_DOWN
                                                                                 : SDL_EVENT_MOUSE_BUTTON_UP;
                    e.button.button = in.right ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
                    e.button.down = in.kind == ViewerOptions::ScriptedInput::Kind::Down;
                    e.button.x = in.x;
                    e.button.y = in.y;
                    break;
                case ViewerOptions::ScriptedInput::Kind::Key:
                    e.type = SDL_EVENT_KEY_DOWN;
                    e.key.key = static_cast<SDL_Keycode>(in.key);
                    e.key.scancode = SDL_GetScancodeFromKey(e.key.key, nullptr);
                    e.key.mod = in.mod;
                    e.key.down = true;
                    break;
            }
            SDL_PushEvent(&e);
        }
        while (SDL_PollEvent(&event)) {
            if (m_video && m_video->playing() && !video_windowed()) {
                handle_video_event(event);
            } else if (video_in_frame() && (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                                            (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE))) {
                stop_video();  // the credits end with a click or Escape
            } else if (video_in_minimap() && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                       m_hud->minimap_area().contains(static_cast<std::int32_t>(event.button.x),
                                                      static_cast<std::int32_t>(event.button.y))) {
                stop_video();  // a click on the video ends it
            } else {
                handle_event(event);
            }
        }
        const std::uint64_t now = platform::ticks_ms();
        const auto delta = static_cast<std::uint32_t>(std::min<std::uint64_t>(now - last, 1000));
        last = now;
        // Messages of the scenery and the sound itself may ask for a video too.
        if (const auto n = m_sound->take_video()) {
            request_video(*n);
        }
        bool video = m_video && (m_video->playing() || start_next_video(now));
        if (video) {
            switch (m_video->update(now)) {
                case VideoPlayer::Step::Playing:
                    break;
                case VideoPlayer::Step::Restarted:
                    if (const video::Smacker* v = m_video->video()) {
                        m_sound->start_video(*v, m_video_now.tracks);
                    }
                    break;
                case VideoPlayer::Step::Ended:
                    m_sound->stop_video();
                    video = start_next_video(now);
                    break;
            }
        }
        if (video && !video_windowed()) {
            // The game stands still while a video fills the screen (the original's main loop
            // 0x00408293); the time does not reach the simulation afterwards. Not in a multiplayer
            // game: the others would wait (M-018).
            if (m_session) {
                update(delta);
            }
            std::int32_t w = 0;
            std::int32_t h = 0;
            m_window->output_size(w, h);
            m_video->draw(w, h);
            present();
        } else {
            sync_text_input();
            if (m_hud && m_hud->menu_active()) {
                apply_menu();  // no game runs behind the menu, but for a multiplayer game
                if (m_session) {
                    update(delta);
                }
                render();
            } else {
                if (m_hud) {
                    // What the windows of the panel ordered (scuttling and disbanding ask first).
                    submit_orders(m_hud->take_commands(), false);
                    apply_options();
                }
                update(delta);
                check_end_screen(now);
                render();
            }
        }
        m_sound->frame(m_state->world.time_ms(), delta);

        ++frames;
        ++fps_frames;
        if (now - fps_start >= 500) {
            update_title(fps_frames * 1000.0 / static_cast<double>(now - fps_start));
            fps_start = now;
            fps_frames = 0;
        }
        if (m_options.max_frames != 0 && frames >= m_options.max_frames) {
            break;
        }
    }
    if (m_options.max_frames != 0) {
        const std::uint64_t elapsed = std::max<std::uint64_t>(platform::ticks_ms() - run_start, 1);
        std::fprintf(stderr, "%u frames in %llu ms (%.1f fps), %zu sprites in the last frame\n", frames,
                     static_cast<unsigned long long>(elapsed), frames * 1000.0 / static_cast<double>(elapsed),
                     m_sprites_drawn);
    }
    if (!m_options.save.empty()) {
        if (auto status = save_game(m_options.save); !status) {
            std::fprintf(stderr, "error: %s\n", status.error().message.c_str());
            return 1;
        }
        std::fprintf(stderr, "game saved: %s\n", core::path_to_utf8(m_options.save).c_str());
    }
    if (!m_options.screenshot.empty()) {
        // Read back a complete frame.
        m_screenshot = ScreenshotRequest{m_options.screenshot};
        if (m_video && m_video->playing() && !video_windowed()) {
            std::int32_t w = 0;
            std::int32_t h = 0;
            m_window->output_size(w, h);
            m_video->draw(w, h);
            present();
        } else {
            render();
        }
        if (m_screenshot_error) {
            std::fprintf(stderr, "error: %s\n", m_screenshot_error->c_str());
            return 1;
        }
    }
    return 0;
}

void MapViewer::handle_event(const SDL_Event& original) {
    SDL_Event event = original;
    // Mouse positions in output pixels, independent of the screen's pixel density.
    SDL_ConvertEventToRenderCoordinates(m_window->sdl_renderer(), &event);
    switch (event.type) {
        case SDL_EVENT_QUIT:
            m_running = false;
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
            std::int32_t w = 0;
            std::int32_t h = 0;
            m_window->output_size(w, h);
            m_camera.set_screen(w, h, m_camera.pixel_scale());
            lay_out();
            follow_display_scale();  // a move to another screen may change both
            break;
        }
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            follow_display_scale();
            break;
        // SDL_EVENT_RENDER_TARGETS_RESET needs nothing: no texture is drawn into.
        case SDL_EVENT_RENDER_DEVICE_RESET:
            recreate_textures();
            break;
        case SDL_EVENT_RENDER_DEVICE_LOST:
            std::fprintf(stderr, "error: the render device was lost and cannot be recovered\n");
            break;
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
                    apply_hud(*a);
                }
            }
            if (m_press && m_left_down) {
                const double dx = static_cast<double>(event.motion.x) - m_press->first;
                const double dy = static_cast<double>(event.motion.y) - m_press->second;
                m_rubber_band = m_rubber_band || dx * dx + dy * dy >= 25.0;  // 5 px (0x0040BA70)
            }
            if (m_dragging) {
                m_camera.scroll(-static_cast<double>(event.motion.xrel), -static_cast<double>(event.motion.yrel));
                m_drag_distance +=
                    std::abs(static_cast<double>(event.motion.xrel)) + std::abs(static_cast<double>(event.motion.yrel));
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
                    m_sound->click();
                    if (const auto a = m_hud->mouse(static_cast<std::int32_t>(event.button.x),
                                                    static_cast<std::int32_t>(event.button.y), true)) {
                        apply_hud(*a);
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
                m_dragging = true;
                m_drag_distance = 0.0;
            } else if (event.button.button == SDL_BUTTON_LEFT && m_tool.active()) {
                if (const auto spot = spot_at_mouse()) {
                    m_tool.press(*spot);
                }
            } else if (event.button.button == SDL_BUTTON_LEFT) {
                m_press = std::make_pair(static_cast<double>(event.button.x), static_cast<double>(event.button.y));
                m_rubber_band = false;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                m_left_down = false;
                if (m_hud) {
                    if (const auto a = m_hud->mouse(static_cast<std::int32_t>(event.button.x),
                                                    static_cast<std::int32_t>(event.button.y), false)) {
                        apply_hud(*a);
                    }
                }
                if (m_press) {
                    map_left_up(static_cast<double>(event.button.x), static_cast<double>(event.button.y),
                                static_cast<std::uint16_t>(SDL_GetModState()), event.button.clicks);
                    m_press.reset();
                    m_rubber_band = false;
                }
            }
            if (event.button.button == SDL_BUTTON_RIGHT || event.button.button == SDL_BUTTON_MIDDLE) {
                m_dragging = false;
                // A right click (not a drag): an order (ADR 0010), or back with the original's mouse.
                if (event.button.button == SDL_BUTTON_RIGHT && m_drag_distance < 4.0 && !(m_hud && m_hud->modal())) {
                    if (m_options.original_mouse) {
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
                    if (auto order = m_tool.build_order(m_state->world, m_local_player, *spot)) {
                        // A Kontor founded from a ship is the only one there: the tool is put away
                        // once it can be built (the ship's window gave it).
                        const bool founded =
                            order->ship &&
                            std::ranges::all_of(sim::plan_build(m_state->world, m_buildings, *order).items,
                                                [](const sim::PlannedBuild& i) { return i.ok(); });
                        m_command_sink.submit(std::move(*order));
                        if (founded) {
                            m_tool.set_mode(BuildTool::Mode::Off);
                        }
                    } else if (auto demolish = m_tool.demolish_order(m_local_player, *spot)) {
                        m_command_sink.submit(*demolish);
                    }
                }
                m_tool.release();
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL: {  // U-001: the original has no wheel zoom
            const float y = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
            if (y != 0.0F) {
                zoom_by(y > 0.0F ? -1 : 1, static_cast<double>(event.wheel.mouse_x),
                        static_cast<double>(event.wheel.mouse_y));
            }
            break;
        }
        default:
            break;
    }
}

void MapViewer::zoom_by(int steps, double sx, double sy) {
    const int current = static_cast<int>(m_camera.view().zoom);
    const int next = std::clamp(current + steps, 0, 2);
    if (next == current) {
        return;
    }
    const auto zoom = static_cast<render::Zoom>(next);
    if (auto status = ensure_graphics(zoom); !status) {
        std::fprintf(stderr, "error: %s\n", status.error().message.c_str());
        return;
    }
    m_camera.set_zoom(zoom, sx, sy);
}

void MapViewer::update(std::uint32_t delta_ms) {
    scroll_with_keys(delta_ms);
    // The pause of the video ended another way (F3, another player): the video goes too.
    if (m_pause_video) {
        if (m_state->simulation.paused()) {
            m_pause_seen = true;
        } else if (m_pause_seen) {
            m_pause_video = false;
            if (m_video && m_video->playing() && m_video->number() == 6 && m_video_now.loop) {
                stop_video();
            }
        }
    }
    // A window changed the selection (filters, formations): the windows follow.
    if (m_units.take_changed()) {
        selection_changed(false);
    }
    // Units that died, sank or changed hands leave the selection.
    if (m_units.prune(m_state->world)) {
        selection_changed(false);
    }
    // While the game is paused this only carries out the commands (building is possible then).
    // Ticks run while the frame has time (at least one); before each, the positions of the ships,
    // soldiers and walkers are kept so that they move smoothly between ticks. A computer too slow
    // for the speed runs the game slower instead of stalling (review point 20).
    const auto frame_end = std::chrono::steady_clock::now() + std::chrono::milliseconds(kTickBudgetMs);
    bool first = true;
    const auto before_tick = [&] {
        if (!first && std::chrono::steady_clock::now() >= frame_end) {
            return false;
        }
        first = false;
        m_ship_trail.capture(m_state->world);
        m_soldiers.capture(m_state->world);
        m_walkers.capture(m_state->simulation.logistics(), m_state->world);
        return true;
    };
    if (m_session) {
        // A multiplayer game runs the frames the host has closed (ADR 0011).
        m_session->update(delta_ms, before_tick);
        update_lan_beacon();
        m_tick_fraction = m_session->tick_fraction();
        apply_session_events();  // may end the session
    } else {
        m_state->simulation.advance(delta_ms, before_tick);
        m_tick_fraction = m_state->simulation.tick_fraction();
    }
    m_ship_trail.set_fraction(m_tick_fraction);
    if (!m_state->simulation.paused()) {
        place_listener();
        apply_production_events();
        apply_game_events();
        m_clock.advance(delta_ms);
        m_soldiers.sync(m_state->world, m_buildings, m_figures, m_tick_fraction);
        m_soldiers.advance(static_cast<std::uint32_t>(delta_ms), m_figures);
        m_walkers.update(m_state->simulation.logistics(), m_state->world, m_buildings, m_figures,
                         static_cast<std::uint32_t>(delta_ms) * m_state->simulation.speed(), m_tick_fraction);
        update_ambient();
        m_sound->figures({&m_walkers.instances(), &m_soldiers.instances(), &m_ambient.walkers()}, m_effects, m_figures,
                         m_state->world.time_ms(), m_state->simulation.speed());
    }
    apply_construction();
}

std::optional<BuildTool::Spot> MapViewer::spot_at_mouse() const {
    const auto tile = m_camera.tile_at_screen(m_mouse_x, m_mouse_y);
    if (!tile) {
        return std::nullopt;
    }
    const auto& cell = m_state->world.cell(tile->x, tile->y);
    if (cell.island == sim::World::kSea) {
        return std::nullopt;
    }
    return BuildTool::Spot{cell.island, cell.local_x, cell.local_y};
}

std::string MapViewer::crops_status() const {
    if (!m_hud ||
        !m_hud->game_view().contains(static_cast<std::int32_t>(m_mouse_x), static_cast<std::int32_t>(m_mouse_y))) {
        return {};
    }
    const auto spot = spot_at_mouse();
    if (!spot) {
        return {};
    }
    const sim::WorldIsland& island = m_state->world.islands()[spot->island];
    if (!island.explored_by(m_local_player)) {
        return {};
    }
    const assets::BuildingType* b = m_buildings.by_sprite(
        island.map.top(static_cast<std::uint32_t>(spot->x), static_cast<std::uint32_t>(spot->y)).sprite);
    if (b == nullptr || (b->ware != sim::ware::kGrass && b->ware != sim::ware::kTree)) {
        return {};
    }
    return m_hud->crops_line(island_crops(island));
}

sim::Catchment MapViewer::world_catchment(std::uint16_t island, sim::Catchment area) const {
    const sim::WorldIsland& i = m_state->world.islands()[island];  // island tiles -> world tiles
    area.centre.x += static_cast<std::int32_t>(i.x);
    area.centre.y += static_cast<std::int32_t>(i.y);
    return area;
}

std::optional<render::TileLighting> MapViewer::building_lighting(std::uint16_t island, std::uint16_t x,
                                                                 std::uint16_t y) const {
    // A catchment worth showing: production and public buildings, not houses (their Radius is
    // only how far they look for a market).
    if (island >= m_state->world.islands().size() || !m_state->world.islands()[island].map.contains(x, y)) {
        return std::nullopt;
    }
    const sim::IslandMap& map = m_state->world.islands()[island].map;
    const sim::MapCell& cell = map.top(x, y);
    const sim::SubTile t = map.sub_tile(m_buildings, x, y);
    if (t.building == nullptr || cell.owner != m_local_player || t.building->radius <= 0 ||
        t.building->prod_kind == sim::prod::kHouse) {
        return std::nullopt;
    }
    render::TileLighting l = render::TileLighting::circle(
        m_local_player, world_catchment(island, sim::catchment_of(*t.building, cell.rotation, sim::TilePos{x, y})));
    for (const sim::City& c : m_state->world.cities()) {
        if (c.island == island && c.warehouse_x == x && c.warehouse_y == y) {
            l.harbour(m_state->world, m_buildings, island, x, y);  // the Kontor: the water of its ships too
        }
    }
    return l;
}

render::TileLighting MapViewer::tile_lighting() const {
    const bool building = m_tool.mode() == BuildTool::Mode::Build || (m_hud && m_hud->mode() == Hud::kModeBuild);
    if (building) {
        // The field of a building looked at before opening the build menu: that building's
        // circle stays lit, to see where its fields go.
        const assets::BuildingType* b = m_tool.mode() == BuildTool::Mode::Build ? m_tool.selected() : nullptr;
        if (m_field_source && b != nullptr && b->id == m_field_source->field) {
            if (auto l = building_lighting(m_field_source->island, m_field_source->x, m_field_source->y)) {
                return *l;
            }
        }
        render::TileLighting l = render::TileLighting::territory(m_local_player);
        // A building with a catchment being placed: the border of its circle at the mouse.
        const auto spot = spot_at_mouse();
        if (b != nullptr && spot && b->radius > 0 && b->prod_kind != sim::prod::kHouse) {
            l.outline(world_catchment(spot->island,
                                      sim::catchment_of(*b, m_tool.rotation(), sim::TilePos{spot->x, spot->y})));
        }
        return l;
    }
    if (m_hud && m_hud->mode() == Hud::kModeInfo && m_hud->selection().kind == Selection::Kind::Building) {
        const Selection& sel = m_hud->selection();
        if (auto l = building_lighting(sel.island, sel.x, sel.y)) {
            return *l;
        }
    }
    return {};
}

void MapViewer::apply_construction() {
    for (const sim::CommandResult& r : m_state->simulation.take_command_results()) {
        if (!r.build) {
            if (r.demolished != 0) {
                std::fprintf(stderr, "demolished %zu, back: %lld gold, %u t tools, %u t wood, %u t bricks\n",
                             r.demolished, static_cast<long long>(r.refund.money), r.refund.tools / 32,
                             r.refund.wood / 32, r.refund.bricks / 32);
            }
            continue;
        }
        std::fprintf(stderr, "built %zu of %zu", r.build->placed, r.build->items.size());
        for (const sim::PlannedBuild& item : r.build->items) {
            if (!item.ok()) {
                std::fprintf(stderr, " - %s", sim::refusal_name(item.refusal));
                break;
            }
        }
        std::fprintf(stderr, "\n");
    }
    const std::vector<sim::ConstructionEvent> events = m_state->simulation.take_construction_events();
    const bool changed = !events.empty();
    m_sound->construction(m_state->world, events);
    for (const sim::ConstructionEvent& e : events) {
        // A production entry that was freed or taken by a new building starts from its saved phase.
        if (e.kind == sim::ConstructionEvent::Kind::Removed && e.production) {
            m_production_phases.forget(*e.production);
        } else if (e.kind == sim::ConstructionEvent::Kind::Placed && e.x >= 0 && e.y >= 0) {
            if (const auto index = m_state->world.production_index(e.island, static_cast<std::uint16_t>(e.x),
                                                                   static_cast<std::uint16_t>(e.y))) {
                m_production_phases.forget(*index);
            }
        }
        if (e.kind == sim::ConstructionEvent::Kind::CityFounded) {
            if (const sim::City* c = m_state->world.city(e.island, e.city)) {
                std::fprintf(stderr, "a new city was founded: %s\n", c->name.c_str());
                if (c->owner != m_local_player) {
                    const sim::WorldIsland& i = m_state->world.islands()[e.island];
                    announce(0x0E, std::make_pair(i.x + i.map.width() / 2.0, i.y + i.map.height() / 2.0), c->owner);
                }
            }
        }
    }
    if (changed) {
        // Flags, smoke and status icons of the new buildings: only the islands built on are
        // collected again. The list stays ordered by island, as collect_building_figures() gives it.
        std::vector<std::uint16_t> islands;
        for (const sim::ConstructionEvent& e : events) {
            islands.push_back(e.island);
        }
        std::sort(islands.begin(), islands.end());
        islands.erase(std::unique(islands.begin(), islands.end()), islands.end());
        std::vector<render::BuildingFigure> fresh;
        for (const std::uint16_t island : islands) {
            if (island >= m_state->world.islands().size()) {
                continue;
            }
            fresh.clear();
            render::collect_island_building_figures(m_state->world, m_buildings, m_figures, island, fresh);
            const auto by_island = [](const render::BuildingFigure& f, std::uint16_t i) { return f.island < i; };
            const auto first =
                std::lower_bound(m_building_figures.begin(), m_building_figures.end(), island, by_island);
            auto last = first;
            while (last != m_building_figures.end() && last->island == island) {
                ++last;
            }
            const auto at = m_building_figures.erase(first, last);
            m_building_figures.insert(at, fresh.begin(), fresh.end());
        }
    }
}

render::WorldLayers MapViewer::world_layers(const ZoomGraphics& graphics) const {
    render::WorldLayers layers;
    const render::SpriteAtlas& atlas = graphics.graphics.atlas;
    layers.extent = render::SpriteExtent{atlas.max_sprite_width(), atlas.max_sprite_height()};
    layers.figures = &m_figures;
    layers.atlas = &atlas;
    layers.ship_base = graphics.graphics.sheet_base(assets::FigureSheet::Ship).value_or(0);
    layers.ship_trail = &m_ship_trail;
    layers.sheet_base = graphics.graphics.base;
    // The deer walk as figures of the simulation (m_walkers).
    layers.land_figures = {&m_soldiers.instances(), &m_walkers.instances(), &m_ambient.walkers()};
    layers.unit_tint = &graphics.graphics.unit_tint;
    layers.banners = &m_banners;
    layers.banner_flags = &graphics.graphics.banner_flags;
    layers.owner_tint = m_show_owners;
    layers.building_figures = &m_building_figures;
    layers.local_player = m_local_player;
    layers.production_phases = &m_production_phases;
    return layers;
}

void MapViewer::render() {
    SDL_Renderer* r = m_window->sdl_renderer();
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    if (m_hud && m_hud->menu_active()) {
        m_hud->draw_menu();
        if (video_in_frame()) {
            const ui::Rect f = m_hud->menu_video_rect();
            m_video->draw_in(f.x, f.y, f.w, f.h);
        }
        present();
        return;
    }

    auto& graphics = m_graphics[static_cast<std::size_t>(m_camera.view().zoom)];
    if (graphics) {
        render::WorldLayers layers = world_layers(*graphics);
        collect_effects();
        layers.effects = &m_effects;
        // The building the tool would place, drawn into the map for this frame only. The guard puts
        // back every cell whole (random bits included) even if drawing throws, so that the world and
        // with it the savegame never depend on where the mouse was (ADR 0004).
        struct Saved {
            std::uint16_t island;
            std::uint32_t x;
            std::uint32_t y;
            sim::MapCell cell;
        };
        struct PreviewGuard {
            sim::World& world;
            std::vector<Saved> cells;
            explicit PreviewGuard(sim::World& w) : world(w) {}
            PreviewGuard(const PreviewGuard&) = delete;
            PreviewGuard& operator=(const PreviewGuard&) = delete;
            ~PreviewGuard() { restore(); }
            void restore() noexcept {
                for (auto it = cells.rbegin(); it != cells.rend(); ++it) {
                    world.island(it->island).map.set_top_cell(it->x, it->y, it->cell);
                }
                cells.clear();
            }
        };
        PreviewGuard preview{m_state->world};
        std::vector<Saved>& saved = preview.cells;
        m_tool_status.clear();
        m_pending_bill.reset();
        m_tile_marks.clear();
        layers.tile_marks = &m_tile_marks;
        m_lighting = tile_lighting();
        layers.lighting = &m_lighting;
        if (m_tool.mode() == BuildTool::Mode::Build) {
            if (const auto spot = spot_at_mouse()) {
                if (const auto order = m_tool.build_order(m_state->world, m_local_player, *spot)) {
                    const sim::BuildPlan plan = sim::plan_build(m_state->world, m_buildings, *order);
                    m_pending_bill = plan.bill;
                    sim::IslandMap& map = m_state->world.island(order->island).map;
                    // A field or forest being dragged: its whole rectangle hatched on the ground,
                    // with no crops or trees in it, as in the History Edition.
                    const bool hatched = m_tool.hatched_drag();
                    std::vector<std::pair<std::int32_t, std::int32_t>> fitted;  // road and wall tiles
                    if (hatched) {
                        for (const sim::BuildItem& item : order->items) {
                            if (map.contains(item.x, item.y)) {
                                render::TileMark mark{order->island, static_cast<std::uint16_t>(item.x),
                                                      static_cast<std::uint16_t>(item.y), render::kDragHatch};
                                mark.hatched = true;
                                m_tile_marks.push_back(mark);
                            }
                        }
                    }
                    for (const sim::PlannedBuild& item : plan.items) {
                        if (!item.ok() && m_tool_status.empty()) {
                            m_tool_status = refusal_text(item.refusal);
                        }
                        if (item.type == nullptr || hatched) {
                            continue;
                        }
                        // What cannot be built is shown too, on red ground, as in the original.
                        std::uint32_t w = 1;
                        std::uint32_t h = 1;
                        sim::rotated_size(*item.type, item.rotation, w, h);
                        bool inside = true;
                        for (std::uint32_t j = 0; j < h; ++j) {
                            for (std::uint32_t i = 0; i < w; ++i) {
                                const std::int32_t tx = item.x + static_cast<std::int32_t>(i);
                                const std::int32_t ty = item.y + static_cast<std::int32_t>(j);
                                if (!map.contains(tx, ty)) {
                                    inside = false;
                                    continue;
                                }
                                if (!item.ok()) {
                                    m_tile_marks.push_back(
                                        render::TileMark{order->island, static_cast<std::uint16_t>(tx),
                                                         static_cast<std::uint16_t>(ty), kRefusedTint});
                                }
                            }
                        }
                        // What only lacks gold or goods is shown on its red ground; anything else that
                        // cannot be built only as red ground (it might stand over a building or the sea).
                        const bool short_of_means =
                            item.refusal == sim::BuildRefusal::Money || item.refusal == sim::BuildRefusal::Tools ||
                            item.refusal == sim::BuildRefusal::Wood || item.refusal == sim::BuildRefusal::Bricks ||
                            item.refusal == sim::BuildRefusal::Cannons;
                        if (!inside || (!item.ok() && !short_of_means)) {
                            continue;
                        }
                        for (std::uint32_t j = 0; j < h; ++j) {
                            for (std::uint32_t i = 0; i < w; ++i) {
                                const auto tx = static_cast<std::uint32_t>(item.x) + i;
                                const auto ty = static_cast<std::uint32_t>(item.y) + j;
                                saved.push_back(Saved{order->island, tx, ty, map.top(tx, ty)});
                            }
                        }
                        const sim::MapCell& at =
                            map.top(static_cast<std::uint32_t>(item.x), static_cast<std::uint32_t>(item.y));
                        // A field or tree is shown grown, a seedling would hardly be seen.
                        const assets::BuildingType* shown = item.type;
                        if (const assets::BuildingType* ripe = sim::ripe_record(m_buildings, *item.type)) {
                            shown = ripe;
                        }
                        (void)map.build(*shown, item.x, item.y, item.rotation, 0, at.owner, at.city);
                        if (shown->kreuz_base > 0) {
                            fitted.push_back({item.x, item.y});
                        }
                    }
                    // Roads and walls fit their neighbours as they will once built, and the
                    // neighbours them (both put back after drawing like the rest).
                    for (const auto& [fx, fy] : fitted) {
                        sim::fit_connection(m_state->world, m_buildings, order->island, fx, fy);
                    }
                    for (const auto& [fx, fy] : fitted) {
                        for (const auto& [dx, dy] :
                             {std::pair{0, -1}, std::pair{1, 0}, std::pair{0, 1}, std::pair{-1, 0}}) {
                            const std::int32_t nx = fx + dx;
                            const std::int32_t ny = fy + dy;
                            if (map.contains(nx, ny)) {
                                const auto ux = static_cast<std::uint32_t>(nx);
                                const auto uy = static_cast<std::uint32_t>(ny);
                                saved.push_back(Saved{order->island, ux, uy, map.top(ux, uy)});
                                sim::fit_connection(m_state->world, m_buildings, order->island, nx, ny);
                            }
                        }
                    }
                    if (m_tool_status.empty()) {
                        m_tool_status = ui::tr("build.cost", {std::to_string(plan.bill.money)});
                    }
                }
            }
        }
        render::build_world_draw_list(m_state->world, m_buildings, m_clock, m_camera.view(), m_camera.viewport(),
                                      layers, m_commands, m_draw_scratch);
        preview.restore();
        std::int32_t ox = 0;
        std::int32_t oy = 0;
        m_camera.screen_origin(ox, oy);
        m_sprites_drawn = graphics->renderer->draw(m_commands, ox, oy, m_camera.pixel_scale());
        draw_selection_overlay();
    }
    if (m_hud) {
        // The corners of the visible map in world tiles, for the minimap's frame. A corner beyond
        // the map's edge stays where it is (the minimap clips the frame), so the frame keeps its shape.
        const ui::Rect& v = m_hud->game_view();
        std::array<std::pair<double, double>, 4> corners{};
        const std::array<std::pair<double, double>, 4> screen = {
            std::make_pair(0.0, 0.0), std::make_pair(static_cast<double>(v.w), 0.0),
            std::make_pair(static_cast<double>(v.w), static_cast<double>(v.h)),
            std::make_pair(0.0, static_cast<double>(v.h))};
        for (std::size_t i = 0; i < 4; ++i) {
            corners[i] = m_camera.point_at_screen(screen[i].first, screen[i].second);
        }
        const std::uint64_t now = platform::ticks_ms();
        if (!m_tool.active() && now < m_notice_until_ms) {
            m_hud->set_status(m_notice);
        } else {
            m_hud->set_status(m_tool_status.empty() && !m_tool.active() ? crops_status() : m_tool_status);
        }
        m_hud->set_rotation(m_camera.view().rotation);
        m_hud->set_building_rotation(m_tool.rotation());
        if (m_hud->mode() == Hud::kModeInfo && (now >= m_figures_ms + 1000 || now < m_figures_ms)) {
            // The player status: sums over all cities and units, once a second is enough.
            m_player_figures = sim::player_figures(m_state->world, m_buildings, m_figures, m_local_player);
            m_player_points = sim::player_score(m_state->world, m_buildings, m_local_player);
            m_figures_ms = now;
        }
        const sim::Player* local = m_state->world.player(m_local_player);
        m_hud->set_resources(resources());
        // "Bewertung" would stop the game on one computer only: alone only (Screen_PlayerStatus).
        m_hud->set_points(m_player_points, humans() <= 1);
        m_hud->draw(m_state->world, m_buildings, m_banners, corners, now, m_player_figures,
                    static_cast<std::uint8_t>(local != nullptr ? std::min<int>(local->banner, 255) : 0));
        if (video_in_minimap()) {
            // The video of a message over the minimap; the game goes on (ADR 0010).
            const ui::Rect a = m_hud->minimap_area();
            m_video->draw_in(a.x, a.y, a.w, a.h);
        }
    }
    present();
}

void MapViewer::apply_production_events() {
    for (const sim::ProductionEvent& e : m_state->simulation.take_production_events()) {
        if (e.kind != sim::ProductionEvent::Kind::Started && e.kind != sim::ProductionEvent::Kind::Stopped) {
            continue;
        }
        // The picture continues smoothly; the phase follows the animation clock and so stays with
        // the presentation, the game state keeps its own (ADR 0004).
        const sim::Production& p = m_state->world.productions()[e.production];
        const sim::MapCell& cell = m_state->world.islands()[p.island].map.top(p.x, p.y);
        if (const assets::BuildingType* b = m_buildings.by_sprite(cell.sprite)) {
            const std::uint8_t phase = m_production_phases.get(e.production).value_or(p.anim_phase);
            m_production_phases.set(e.production,
                                    render::phase_after_change(*b, phase, m_clock.frame(*b),
                                                               e.kind == sim::ProductionEvent::Kind::Started));
        }
    }
    // Messages for the local player until there is an interface for them.
    for (const sim::CityEvent& e : m_state->simulation.take_city_events()) {
        if (e.owner != m_local_player) {
            continue;
        }
        const sim::City* city = m_state->world.city(e.island, e.city);
        const std::string name = city != nullptr ? city->name : std::string("?");
        if (e.kind == sim::CityEvent::Kind::Hunger) {
            std::fprintf(stderr, "%s: the people are starving\n", name.c_str());
            announce(0x33);
        } else if (e.kind == sim::CityEvent::Kind::Monument) {
            announce(0x31);
        } else if (e.kind == sim::CityEvent::Kind::Unlocked) {
            std::fprintf(stderr, "%s: build level %u reached\n", name.c_str(), e.infra);
        } else {
            std::fprintf(stderr, "%s: short of ware %u\n", name.c_str(), e.ware);
        }
    }
    // Ships of the local player: discoveries and exploring (MELDUNG 0x2A-0x30).
    const std::vector<sim::ShipEvent> ship_events = m_state->simulation.take_ship_events();
    m_sound->ships(m_state->world, m_figures, ship_events, m_local_player, m_state->world.time_ms());
    for (const sim::ShipEvent& e : ship_events) {
        if (e.player != m_local_player || e.island >= m_state->world.islands().size()) {
            continue;
        }
        const unsigned number = m_state->world.islands()[e.island].header.number;
        switch (e.kind) {
            case sim::ShipEvent::Kind::NativesFound:
                std::fprintf(stderr, "you discovered a native village! (island %u)\n", number);
                break;
            case sim::ShipEvent::Kind::PiratesFound:
                std::fprintf(stderr, "the pirates' hideout has been found! (island %u)\n", number);
                break;
            case sim::ShipEvent::Kind::CropsExplored:
                std::fprintf(stderr, "the crops of island %u are explored: %s\n", number,
                             crops_text(m_state->world.islands()[e.island]).c_str());
                break;
            case sim::ShipEvent::Kind::DepositFound:
                std::fprintf(stderr, "%s on island %u (%u,%u)!\n",
                             e.ware == sim::ware::kGold ? "you discovered a gold vein" : "an ore deposit lies", number,
                             e.x, e.y);
                break;
            case sim::ShipEvent::Kind::ExploreFinished:
                std::fprintf(stderr, "island %u explored\n", number);
                break;
            default:
                break;
        }
    }
    // Fires and plagues in the player's cities (MELDUNG 1, 2).
    for (const sim::DisasterEvent& e : m_state->simulation.take_disaster_events()) {
        if (e.owner != m_local_player) {
            continue;
        }
        const sim::City* city = m_state->world.city(e.island, e.city);
        const char* name = city != nullptr ? city->name.c_str() : "?";
        const bool fire = e.disaster == sim::Disaster::Kind::Fire;
        if (e.kind == sim::DisasterEvent::Kind::Started && !e.spread) {
            std::fprintf(stderr, "%s: %s (%u,%u)\n", name,
                         fire ? "a fire broke out!" : "the plague haunts your settlement!", e.x, e.y);
            const sim::WorldIsland& isl = m_state->world.islands()[e.island];
            announce(fire ? 0x02 : 0x01,
                     std::make_pair(static_cast<double>(isl.x + e.x), static_cast<double>(isl.y + e.y)));
        } else if (e.kind == sim::DisasterEvent::Kind::BurnedDown) {
            std::fprintf(stderr, "%s: a building burned down (%u,%u)\n", name, e.x, e.y);
        }
    }
    // Droughts: the player hears of those on islands where he has a city (MELDUNG 3).
    for (const sim::IslandEvent& e : m_state->simulation.take_island_events()) {
        const sim::City* own = nullptr;
        for (const sim::City& c : m_state->world.cities()) {
            if (c.island == e.island && c.owner == m_local_player) {
                own = &c;
            }
        }
        if (own == nullptr) {
            continue;
        }
        switch (e.kind) {
            case sim::IslandEvent::Kind::DroughtStarted:
                std::fprintf(stderr, "%s: the drought destroys your harvest!\n", own->name.c_str());
                announce(0x03);
                break;
            case sim::IslandEvent::Kind::Withered:
                std::fprintf(stderr, "%s: %u fields withered\n", own->name.c_str(), e.count);
                break;
            case sim::IslandEvent::Kind::Recovered:
                std::fprintf(stderr, "%s: %u fields grow again\n", own->name.c_str(), e.count);
                break;
            case sim::IslandEvent::Kind::DroughtOver:
                std::fprintf(stderr, "%s: the drought is over\n", own->name.c_str());
                break;
        }
    }
    // Volcanoes (MELDUNG 94): only for players who have discovered the island.
    for (const sim::VolcanoEvent& e : m_state->simulation.take_volcano_events()) {
        if (e.kind == sim::VolcanoEvent::Kind::Erupted && e.message &&
            m_state->world.islands()[e.island].discovered_by(m_local_player)) {
            std::fprintf(stderr, "a volcano has erupted! (island %u)\n",
                         static_cast<unsigned>(m_state->world.islands()[e.island].header.number));
            const sim::WorldIsland& isl = m_state->world.islands()[e.island];
            announce(0x5E, std::make_pair(isl.x + isl.map.width() / 2.0, isl.y + isl.map.height() / 2.0));
        }
    }
}

void MapViewer::collect_effects() {
    m_effects.clear();
    const sim::VolcanoState& v = m_state->world.volcanoes();
    for (const sim::Eruption& e : v.eruptions) {
        const sim::WorldIsland& island = m_state->world.islands()[e.crater.island];
        render::EffectFigure f;
        f.x = island.x + e.crater.x + 2.0;  // the centre of the 4x4 mountain
        f.y = island.y + e.crater.y + 2.0;
        f.z = sim::kVolcanoHeight;
        f.figure = sim::kFigureVolcano;
        m_effects.push_back(f);
    }
    const std::uint64_t now_us = m_state->world.time_ms() * 1000;
    for (const sim::LavaLump& p : v.lava) {
        const sim::LavaPoint at = sim::lava_position(p, now_us);
        render::EffectFigure f;
        f.x = at.x;
        f.y = at.y;
        f.z = at.z;
        f.ground = sim::lava_ground(m_state->world, m_buildings, at.x, at.y);
        f.figure = sim::kFigureLava;
        f.anim = p.anim;
        f.phase = static_cast<std::uint32_t>(p.launch_us / 1000);
        m_effects.push_back(f);
    }
    const auto& ambient = m_ambient.effects();
    m_effects.insert(m_effects.end(), ambient.begin(), ambient.end());
}

void MapViewer::update_ambient() {
    // Decided tick by tick through the simulation's observer; here only the pictures and sounds.
    std::int32_t w = 0;
    std::int32_t h = 0;
    m_window->output_size(w, h);
    render::AmbientView view;
    view.game_speed = m_state->simulation.speed();
    view.tick_fraction = m_tick_fraction;
    // Islands on screen: those overlapping the box of the tiles at the screen's corners and edges.
    std::uint32_t x0 = m_state->world.width();
    std::uint32_t y0 = m_state->world.height();
    std::uint32_t x1 = 0;
    std::uint32_t y1 = 0;
    for (const double u : {0.0, 0.5, 1.0}) {
        for (const double v : {0.0, 0.5, 1.0}) {
            if (const auto t = m_camera.tile_at_screen(u * w, v * h)) {
                x0 = std::min(x0, t->x);
                y0 = std::min(y0, t->y);
                x1 = std::max(x1, t->x);
                y1 = std::max(y1, t->y);
            }
        }
    }
    view.islands.assign(m_state->world.islands().size(), false);
    for (std::size_t i = 0; i < m_state->world.islands().size(); ++i) {
        const sim::WorldIsland& island = m_state->world.islands()[i];
        view.islands[i] = island.x <= x1 && island.x + island.map.width() > x0 && island.y <= y1 &&
                          island.y + island.map.height() > y0;
    }
    m_ambient.frame(m_state->world, m_buildings, m_figures, view);
    m_sound->scenery(m_ambient.take_sounds());
}

void MapViewer::place_listener() {
    const render::Viewport v = m_camera.viewport();
    const render::WorldView view = m_camera.view();
    const double cx = v.left + v.width / 2.0;
    const double cy = v.top + v.height / 2.0;
    const double hw = std::max(1.0, v.width / 2.0);
    const double hh = std::max(1.0, v.height / 2.0);
    m_sound->set_view(
        [view, cx, cy, hw, hh](double tx, double ty) {
            // World pixels of the point: the tile centres around it, in between by its fraction.
            const auto x0 = static_cast<std::uint32_t>(std::max(0.0, std::floor(tx)));
            const auto y0 = static_cast<std::uint32_t>(std::max(0.0, std::floor(ty)));
            std::int32_t ax = 0;
            std::int32_t ay = 0;
            std::int32_t bx = 0;
            std::int32_t by = 0;
            std::int32_t c_x = 0;
            std::int32_t c_y = 0;
            render::world_tile_center(view, x0, y0, ax, ay);
            render::world_tile_center(view, x0 + 1, y0, bx, by);
            render::world_tile_center(view, x0, y0 + 1, c_x, c_y);
            const double fx = tx - x0;
            const double fy = ty - y0;
            const double px = ax + fx * (bx - ax) + fy * (c_x - ax);
            const double py = ay + fx * (by - ay) + fy * (c_y - ay);
            return std::make_pair(static_cast<float>((px - cx) / hw), static_cast<float>((py - cy) / hh));
        },
        static_cast<float>(v.width), static_cast<float>(v.height));
}

void MapViewer::announce(std::uint16_t message, std::optional<std::pair<double, double>> tile,
                         std::optional<sim::PlayerId> about) {
    m_sound->message(message, tile, about);  // the listener puts it into the status bar
    if (const auto n = m_sound->take_video()) {
        request_video(*n);
    }
}

void MapViewer::announce_with_tracks(std::uint16_t message, std::uint8_t tracks) {
    m_sound->message(message, std::nullopt);
    if (const auto n = m_sound->take_video()) {
        request_video(*n, tracks);
    }
}

void MapViewer::request_video(std::uint16_t number, std::uint8_t tracks, bool loop, bool intro) {
    if (!m_video) {
        return;
    }
    if (!intro) {
        // Video_Request 0x00488C00: the latest request wins.
        m_videos.clear();
        if (m_video->playing()) {
            stop_video();
        }
    }
    // Messages play in the minimap's window; the logos and the intro, the credits (10), the pause (6)
    // and the ends of the game (defeat 998, 1010, 2020; missions fulfilled from 2500) fill the screen.
    const bool full =
        intro || loop || number == 10 || number == 998 || number == 1010 || number == 2020 || number >= 2500;
    m_videos.push_back(VideoRequest{number, tracks, loop, intro, !full});
}

bool MapViewer::video_in_minimap() const {
    return m_video && m_video->playing() && m_video_now.in_minimap && m_hud;
}

bool MapViewer::video_in_frame() const {
    return m_video && m_video->playing() && m_video_now.in_frame && m_hud;
}

bool MapViewer::start_next_video(std::uint64_t now_ms) {
    while (m_video && !m_videos.empty()) {
        const VideoRequest r = m_videos.front();
        m_videos.pop_front();
        // Reading the file and decoding its sound stand the frames still.
        m_sound->prefill();
        // A missing file goes on with the next one (the original drops the rest of the chain, U-022).
        if (m_video->start(r.number, r.loop, now_ms)) {
            m_video_now = r;
            if (const video::Smacker* v = m_video->video()) {
                m_sound->start_video(*v, r.tracks);
                // The picture waits for the sound, which plays after what the device holds.
                m_video->hold_until(platform::ticks_ms() + m_sound->queued_ms());
            }
            return true;
        }
        std::fprintf(stderr, "video %u is missing\n", static_cast<unsigned>(r.number));
    }
    return false;
}

void MapViewer::stop_video() {
    if (m_video) {
        m_video->stop();
    }
    m_sound->stop_video();
}

void MapViewer::handle_video_event(const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_RENDER_DEVICE_RESET:
        case SDL_EVENT_RENDER_DEVICE_LOST:
            handle_event(event);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.key == SDLK_PAUSE || (event.key.key == SDLK_ESCAPE && m_pause_video)) {
                toggle_pause_video();
            } else if (event.key.key == SDLK_ESCAPE) {
                // Skipping a logo or the intro skips the whole start (0x00492690).
                if (m_video_now.intro) {
                    std::erase_if(m_videos, [](const VideoRequest& r) { return r.intro; });
                }
                stop_video();
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                m_left_down = false;
            }
            break;
        default:
            break;  // clicks on the game are swallowed while a video plays
    }
}

void MapViewer::toggle_pause_video() {
    // Follows the game: paused another way (F3, another player), the Pause key goes on.
    m_pause_video = !m_state->simulation.paused() && !m_pause_video;
    m_pause_seen = false;
    if (m_pause_video) {
        m_command_sink.submit(
            sim::GameSpeedOrder{m_local_player, sim::GameSpeedOrder::Op::Pause, m_state->simulation.speed()});
        if (m_video && m_video->available(6)) {
            request_video(6, 0, true);
        }
    } else {
        m_command_sink.submit(
            sim::GameSpeedOrder{m_local_player, sim::GameSpeedOrder::Op::Resume, m_state->simulation.speed()});
        if (m_video && m_video->playing() && m_video->number() == 6 && m_video_now.loop) {
            stop_video();
        }
    }
}

void MapViewer::lay_out() {
    if (!m_hud) {
        return;
    }
    std::int32_t w = 0;
    std::int32_t h = 0;
    m_window->output_size(w, h);
    const ui::Rect view = m_hud->layout(w, h);
    m_camera.set_screen(view.w, view.h, m_camera.pixel_scale());
}

void MapViewer::follow_display_scale() {
    const std::int32_t scale = render::pixel_scale_for(m_window->display_scale(), kMaxPixelScale);
    if (scale == m_display_pixel_scale) {
        return;
    }
    const std::int32_t own = m_camera.pixel_scale() - m_display_pixel_scale;  // Ctrl +/- of the player
    m_display_pixel_scale = scale;
    std::int32_t w = 0;
    std::int32_t h = 0;
    m_window->output_size(w, h);
    m_camera.set_pixel_scale(std::clamp(scale + own, 1, kMaxPixelScale), w / 2.0, h / 2.0);
    std::fprintf(stderr, "display scale %.2f: pixel scale %d\n", static_cast<double>(m_window->display_scale()),
                 m_camera.pixel_scale());
}

void MapViewer::recreate_textures() {
    std::fprintf(stderr, "the render device was reset: textures are made again\n");
    // The world's sprites keep no pixels after the upload (drop_pixels): they are loaded again,
    // the one shown now at once, the others in the background (those still there keep theirs).
    for (auto& g : m_graphics) {
        g.reset();
    }
    m_preloader->request(render::preload_order(m_camera.view().zoom));
    if (auto status = ensure_graphics(m_camera.view().zoom); !status) {
        std::fprintf(stderr, "error: %s\n", status.error().message.c_str());
    }
    if (m_hud) {
        m_hud->release_textures();
    }
    if (m_video) {
        m_video->recreate_texture();
    }
}

void MapViewer::select_building_at_mouse() {
    if (!m_hud) {
        return;
    }
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
            if (!tile || tile->x >= m_state->world.width() || tile->y >= m_state->world.height()) {
                continue;
            }
            const sim::World::Cell& cell = m_state->world.cell(tile->x, tile->y);
            if (cell.island == sim::World::kSea) {
                continue;
            }
            const sim::IslandMap& map = m_state->world.islands()[cell.island].map;
            const sim::SubTile t = map.sub_tile(m_buildings, cell.local_x, cell.local_y);
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
        const sim::SubTile t = m_state->world.islands()[sel.island].map.sub_tile(m_buildings, sel.x, sel.y);
        if (t.building != nullptr && t.building->raw_ware != 0) {
            for (const assets::BuildingType& field : m_buildings.types()) {
                const assets::BuildingType* ripe =
                    field.prod_kind == sim::prod::kGrowing ? sim::ripe_record(m_buildings, field) : nullptr;
                if (ripe != nullptr && ripe->ware == t.building->raw_ware && m_hud->show_in_build_menu(field.id)) {
                    m_field_source = FieldSource{sel.island, sel.x, sel.y, field.id};
                    break;
                }
            }
        }
    }
}

Hud::Resources MapViewer::resources() const {
    Hud::Resources r;
    const sim::Player* local = m_state->world.player(m_local_player);
    if (local != nullptr && local->role == sim::PlayerRole::Human) {
        r.money = local->money;
    }
    if (m_pending_bill) {
        r.money_cost = m_pending_bill->money;
    }
    // The city on the tile under the mouse.
    const auto tile = m_camera.tile_at_screen(m_mouse_x, m_mouse_y);
    if (!tile || tile->x >= m_state->world.width() || tile->y >= m_state->world.height()) {
        return r;
    }
    const sim::World::Cell& cell = m_state->world.cell(tile->x, tile->y);
    if (cell.island == sim::World::kSea) {
        return r;
    }
    const sim::WorldIsland& island = m_state->world.islands()[cell.island];
    const sim::City* city = m_state->world.city(cell.island, island.map.top(cell.local_x, cell.local_y).city);
    if (city == nullptr || !island.discovered_by(m_local_player)) {
        return r;
    }
    if (city->owner == m_local_player) {
        constexpr std::array<std::uint8_t, 3> kWares{sim::ware::kTools, sim::ware::kWood, sim::ware::kBricks};
        std::array<std::int64_t, 3> goods{};
        for (std::size_t i = 0; i < 3; ++i) {
            goods[i] = city->stock(kWares[i]) >> 5U;
        }
        r.goods = goods;
        if (m_pending_bill) {
            r.goods_cost = std::array<std::int64_t, 3>{m_pending_bill->tools >> 5U, m_pending_bill->wood >> 5U,
                                                       m_pending_bill->bricks >> 5U};
        }
    }
    std::int64_t people = 0;
    for (const std::uint32_t n : city->inhabitants) {
        people += n;
    }
    r.inhabitants = people;
    const sim::Player* owner = m_state->world.player(city->owner);
    if (owner != nullptr && owner->role == sim::PlayerRole::Human) {
        r.shield = owner->banner;
    }
    return r;
}

void MapViewer::apply_hud(const Hud::Action& action) {
    const ui::Rect view = m_hud->game_view();
    const double cx = view.w / 2.0;
    const double cy = view.h / 2.0;
    switch (action.kind) {
        case Hud::Action::Kind::RotateLeft:
            m_camera.set_rotation(static_cast<std::uint8_t>(m_camera.view().rotation + 3), cx, cy);
            break;
        case Hud::Action::Kind::RotateRight:
            m_camera.set_rotation(static_cast<std::uint8_t>(m_camera.view().rotation + 1), cx, cy);
            break;
        case Hud::Action::Kind::ZoomIn:
            zoom_by(-1, cx, cy);
            break;
        case Hud::Action::Kind::ZoomOut:
            zoom_by(1, cx, cy);
            break;
        case Hud::Action::Kind::Minimap:
        case Hud::Action::Kind::Message:
            m_camera.center_on(render::TilePos{static_cast<std::uint32_t>(std::max(0.0, action.tile_x)),
                                               static_cast<std::uint32_t>(std::max(0.0, action.tile_y))});
            break;
        case Hud::Action::Kind::Rating:
            // "Bewertung" (INFO.GAD 57028): the score sheets while the game stands still.
            show_end_screen(local_outcome(), true);
            break;
        case Hud::Action::Kind::SelectShip:
            // The ship list (0x00434020): look at the ship and select it.
            m_camera.center_on(render::TilePos{static_cast<std::uint32_t>(std::max(0.0, action.tile_x)),
                                               static_cast<std::uint32_t>(std::max(0.0, action.tile_y))});
            if (m_state->world.ship(action.ship) != nullptr) {
                m_units.select(m_state->world, ui::UnitRef{ui::UnitRef::Kind::Ship, action.ship});
                selection_changed(false);
            }
            break;
        case Hud::Action::Kind::Build:
            if (m_tool.select(action.building)) {
                m_tool.set_mode(BuildTool::Mode::Build);
                drop_units();
            }
            break;
        case Hud::Action::Kind::Demolish:
            m_tool.set_mode(BuildTool::Mode::Demolish);
            drop_units();
            break;
        case Hud::Action::Kind::TurnBuilding:
            m_tool.rotate(action.step);
            break;
        case Hud::Action::Kind::ToolOff:
            m_tool.set_mode(BuildTool::Mode::Off);
            break;
        case Hud::Action::Kind::OrientToRoad:
            m_tool.set_orient_to_road(action.step != 0);
            break;
        case Hud::Action::Kind::Answer:
            if (std::exchange(m_quit_pending, false)) {
                if (action.step == 0) {
                    break;
                }
                if (m_hud && m_hud->menu_active()) {
                    m_running = false;  // "Beenden" in the main menu
                    break;
                }
                // "Ende": the game is kept as lastgame (0x0043BCF0), then back to the main menu
                // (no score sheet, no high score), or the end without one.
                const auto status = save_game(save_folder() / ("lastgame" + std::string(sim::kSaveExtension)));
                if (!status) {
                    std::fprintf(stderr, "lastgame: %s\n", status.error().message.c_str());
                }
                // Leaving a multiplayer game leaves its session: the others go on without us (or
                // learn that the host left), and the menu can open or join the next one.
                end_session();
                if (m_hud && m_hud->menu()) {
                    m_end_pending.reset();
                    m_hud->open_menu();
                } else {
                    m_running = false;
                }
                break;
            }
            // The question before scuttling, disbanding or war.
            if (action.step != 0) {
                std::vector<sim::Command> orders = std::move(m_pending_orders);
                const bool confirm = std::exchange(m_options.confirm, false);
                submit_orders(std::move(orders));
                m_options.confirm = confirm;
            }
            m_pending_orders.clear();
            break;
        case Hud::Action::Kind::FoundKontor:
            if (m_tool.select_kontor(action.ship)) {
                m_tool.set_mode(BuildTool::Mode::Build);
            }
            break;
    }
}

void MapViewer::apply_game_events() {
    const std::vector<sim::MilitaryEvent> military = m_state->simulation.take_military_events();
    m_sound->combat(m_state->world, m_figures, military, m_local_player, m_state->world.time_ms());
    for (const sim::DiplomacyNotice& n : m_state->simulation.take_diplomacy_notices()) {
        if (n.to != m_local_player) {
            continue;
        }
        static constexpr std::array<std::uint16_t, 6> kMessages = {0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52};
        announce(kMessages[static_cast<std::size_t>(n.kind)]);
    }
    const auto& banners = m_banners;
    for (const sim::GameEvent& e : m_state->simulation.take_game_events()) {
        const sim::Player* p = m_state->world.player(e.player);
        const std::string who =
            p != nullptr ? player_label(*p, e.player, banners) : std::string("player ") + std::to_string(e.player);
        switch (e.kind) {
            case sim::GameEvent::Kind::MissionAccomplished:
                std::fprintf(stderr, "%s accomplished the mission!\n", who.c_str());
                if (e.player == m_local_player && !m_end_pending && !m_end_shown) {
                    m_end_pending = EndPending{sim::GameOutcome::Won, kEndScreenDelayMs, platform::ticks_ms(), false};
                }
                if (e.player == m_local_player) {
                    // 0x6B is the multiplayer message with the winner's colour (0x6B..0x6E); its
                    // video speaks to the winner on track 1.
                    if (e.message == audio::kMessageMultiplayerWin && p != nullptr) {
                        announce_with_tracks(static_cast<std::uint16_t>(e.message + sim::four_colour_index(p->banner)),
                                             0x03);
                    } else {
                        announce(e.message);
                    }
                }
                break;
            case sim::GameEvent::Kind::GameOver:
                std::fprintf(stderr, "the game is over\n");
                update_high_scores(sim::kNoPlayer);  // op 0x3E, after the winners are known
                record_progress();
                if (e.player != m_local_player && m_state->world.player(m_local_player) != nullptr &&
                    !m_state->world.player(m_local_player)->won) {
                    if (!m_end_pending && !m_end_shown) {
                        m_end_pending =
                            EndPending{sim::GameOutcome::Lost, kEndScreenDelayMs, platform::ticks_ms(), false};
                    }
                    const sim::Mission* m = p != nullptr ? m_state->world.mission_of(*p) : nullptr;
                    if (m != nullptr && m->success_message == audio::kMessageMultiplayerWin) {
                        // A race: "the red player fulfilled his task", the others' voice (track 2).
                        announce_with_tracks(static_cast<std::uint16_t>(audio::kMessageMultiplayerWin +
                                                                        sim::four_colour_index(p->banner)),
                                             0x05);
                    } else {
                        announce(0x53);  // "Ihr wurdet besiegt!"
                    }
                }
                break;
            case sim::GameEvent::Kind::PlayerDefeated:
                std::fprintf(stderr, "%s is defeated\n", who.c_str());
                update_high_scores(e.player);  // op 3 sub 9, the defeated one still counted
                if (e.player == m_local_player) {
                    if (!m_end_pending && !m_end_shown) {
                        m_end_pending = EndPending{sim::GameOutcome::Lost, kDefeatDelayMs, platform::ticks_ms(), false};
                    }
                    announce(0x53);
                    // The defeat videos come directly, whatever the video switches say (0x004766A0):
                    // bankruptcy 998, otherwise 1010 or 2020.
                    request_video(e.reason == sim::GameEvent::Reason::Bankrupt ? 998
                                  : (m_defeat_variant++ & 1U) == 0             ? 1010
                                                                               : 2020);
                } else {
                    announce(e.reason == sim::GameEvent::Reason::Bankrupt ? 0x59 : 0x5A);
                }
                break;
            case sim::GameEvent::Kind::ArchAllowed:
                if (e.player == m_local_player) {
                    std::fprintf(stderr, "you may build a triumphal arch\n");
                    announce(0x32);
                }
                break;
        }
    }
}

render::TilePos MapViewer::home_tile() const {
    for (const sim::City& c : m_state->world.cities()) {
        if (c.owner == m_local_player && c.warehouse_x && c.warehouse_y && c.island < m_state->world.islands().size()) {
            const sim::WorldIsland& island = m_state->world.islands()[c.island];
            return render::TilePos{island.x + *c.warehouse_x, island.y + *c.warehouse_y};
        }
    }
    for (const sim::Ship& s : m_state->world.ships()) {
        if (s.owner == m_local_player) {
            return render::TilePos{static_cast<std::uint32_t>(s.x), static_cast<std::uint32_t>(s.y)};
        }
    }
    if (!m_state->world.islands().empty()) {
        const auto& island = m_state->world.islands().front();
        return render::TilePos{island.x + island.map.width() / 2, island.y + island.map.height() / 2};
    }
    return render::TilePos{m_state->world.width() / 2, m_state->world.height() / 2};
}

std::string MapViewer::debug_title(double fps) {
    // For development (O1602_DEBUG_TITLE): what lies under the mouse and is selected, in English.
    const auto& view = m_camera.view();
    std::string title = "Open1602 - " + game_title() + " | " + zoom_name(view.zoom) + " x" +
                        std::to_string(m_camera.pixel_scale()) + " | rotation " + std::to_string(view.rotation);
    if (const auto tile = m_camera.tile_at_screen(m_mouse_x, m_mouse_y)) {
        title += " | tile " + std::to_string(tile->x) + "," + std::to_string(tile->y);
        const auto& cell = m_state->world.cell(tile->x, tile->y);
        if (cell.island != sim::World::kSea) {
            title += " (island " + std::to_string(cell.island);
            const auto& island = m_state->world.islands()[cell.island];
            const sim::MapCell& top = island.map.top(cell.local_x, cell.local_y);
            if (const sim::City* city = m_state->world.city(cell.island, top.city)) {
                title += ", " + core::cp1252_to_utf8(city->name);
            }
            if (const sim::Player* p = m_state->world.player(top.owner)) {
                title += ", " + player_label(*p, top.owner, m_banners);
            }
            title += ")";
        }
    }
    const sim::Soldier* soldier = m_units.size() == 1 && m_units.kind() == ui::UnitRef::Kind::Soldier
                                      ? m_state->world.soldier(m_units.units().front().id)
                                      : nullptr;
    if (soldier != nullptr) {
        const assets::FigureType* type = m_figures.get(soldier->figure);
        const std::uint32_t max = type != nullptr ? std::max<std::uint32_t>(1, type->combat.max_health) : 1U;
        const std::uint32_t health = soldier->health != 0 ? soldier->health : max;
        title += " | unit #" + std::to_string(soldier->figure);
        if (const sim::Player* p = m_state->world.player(soldier->owner)) {
            title += " (" + player_label(*p, soldier->owner, m_banners) + ")";
        }
        title += " " + std::to_string(health * 100 / max) + " %";
    } else if (m_units.size() > 1) {
        title += " | " + std::to_string(m_units.size()) + " selected";
    }
    if (m_pick != Pick::None) {
        title += m_pick == Pick::Attack ? " | ATTACK" : " | PATROL";
    }
    const sim::Ship* selected_ship = m_units.size() == 1 && m_units.kind() == ui::UnitRef::Kind::Ship
                                         ? m_state->world.ship(m_units.units().front().id)
                                         : nullptr;
    if (const sim::Ship* ship = selected_ship) {
        title += " | ship " + core::cp1252_to_utf8(ship->name);
        if (const sim::Player* p = m_state->world.player(ship->owner)) {
            title += " (" + player_label(*p, ship->owner, m_banners) + ")";
        }
        char info[64];
        std::snprintf(info, sizeof info, " %.1f tiles/s", m_state->simulation.fleet().speed_milli(*ship) / 1000.0);
        title += info;
        for (const sim::CargoSlot& c : ship->cargo) {
            if (c.ware != 0) {
                title += ", " + std::to_string(c.amount / 32) + " t #" + std::to_string(c.ware);
            }
        }
        if (ship->route.active) {
            title += ", trade route";
        }
        if (ship->explore_steps != 0) {
            title += ", exploring " + std::to_string((11 - ship->explore_steps) * 100 / 11) + " %";
        } else if (ship->owner == m_local_player &&
                   m_state->simulation.fleet().explore_state(*ship, m_local_player) == sim::ExploreState::Possible) {
            title += ", X: explore island";
        }
    }
    // Over an island its player has explored: the crops, as in the original's help bar.
    if (const auto spot = spot_at_mouse()) {
        const sim::WorldIsland& isl = m_state->world.islands()[spot->island];
        if (isl.explored_by(m_local_player)) {
            title += " | " + crops_text(isl);
        }
    }
    if (m_tool.active()) {
        title += " | " + m_tool.describe();
        if (!m_tool_status.empty()) {
            title += ": " + m_tool_status;
        }
    }
    char perf[64];
    std::snprintf(perf, sizeof perf, " | %.0f fps, %zu sprites", fps, m_sprites_drawn);
    title += perf;
    return title;
}

std::string MapViewer::game_title() const {
    // The game's own name, the same on every computer of a multiplayer game; else its file.
    if (const std::string name = sim::game_identity(m_state->world).name; !name.empty()) {
        return core::cp1252_to_utf8(name);
    }
    return core::path_to_utf8(m_options.scenario.stem());
}

void MapViewer::update_title(double fps) {
    // The window title: the game, its time and speed, in the player's language. With
    // O1602_DEBUG_TITLE set it also shows what lies under the mouse, the selection and the frame rate.
    // In the menus no game is played (the world behind them is only a backdrop): the name alone.
    const bool in_menu = m_hud && m_hud->menu_active() && !m_debug_title;
    if (in_menu) {
        m_title_real_ms = 0;  // the time in the menus does not count as a slow game
        m_window->set_title("Open1602" + session_title());
        return;
    }
    std::string title = m_debug_title ? debug_title(fps) : "Open1602 - " + game_title();
    const std::uint64_t seconds = m_state->simulation.game_time_ms() / 1000;
    char clock[32];
    std::snprintf(clock, sizeof clock, " | %02llu:%02llu:%02llu", static_cast<unsigned long long>(seconds / 3600),
                  static_cast<unsigned long long>(seconds / 60 % 60), static_cast<unsigned long long>(seconds % 60));
    title += clock;
    if (m_state->simulation.paused()) {
        title += " | " + ui::tr("title.paused");
    } else if (m_state->simulation.speed() != 1) {
        title += " | " + ui::tr("title.speed", {std::to_string(m_state->simulation.speed())});
    }
    // A computer too slow for the speed lets the game run slower (Simulation::behind()): the speed
    // reached since the last title.
    const std::uint64_t now = platform::ticks_ms();
    const std::uint64_t game_ms = m_state->simulation.game_time_ms();
    // A multiplayer game that ended stands still on purpose.
    const bool stands = m_state->simulation.paused() || (m_session && m_session->ended());
    if (!stands && now > m_title_real_ms && game_ms >= m_title_game_ms && m_title_real_ms != 0) {
        const double reached =
            static_cast<double>(game_ms - m_title_game_ms) / static_cast<double>(now - m_title_real_ms);
        if (reached < 0.9 * m_state->simulation.speed()) {
            char speed[16];
            std::snprintf(speed, sizeof speed, "%.1f", reached);
            title += " | " + ui::tr("title.too_slow", {speed});
        }
    }
    m_title_real_ms = now;
    m_title_game_ms = game_ms;
    m_window->set_title(title + session_title());
}

std::string MapViewer::session_title() const {
    if (!m_session) {
        return {};
    }
    std::size_t players = 0;
    for (const net::Member& m : m_session->roster()) {
        players += m.seat != sim::kNoPlayer ? 1 : 0;
    }
    std::string title = " | " + ui::tr("title.multiplayer", {std::to_string(players)});
    if (m_session->ended()) {
        title += ", " + ui::tr("title.multiplayer_ended");
    } else if (m_session->waiting()) {
        title += ", " + ui::tr("title.multiplayer_waiting");
    }
    return title;
}

std::filesystem::path MapViewer::data_folder() {
    std::filesystem::path folder;
    if (char* pref = SDL_GetPrefPath(nullptr, "Open1602")) {
        folder = core::path_from_utf8(pref);
        SDL_free(pref);
    } else {
        // Never the working folder: started from the installation, the game would write into it.
        std::error_code ec;
        folder = std::filesystem::temp_directory_path(ec) / "Open1602";
        static bool warned = false;
        if (!std::exchange(warned, true)) {
            std::fprintf(stderr, "warning: no user data folder (%s), using %s\n", SDL_GetError(),
                         core::path_to_utf8(folder).c_str());
        }
    }
    return folder;
}

std::filesystem::path MapViewer::save_folder() {
    return data_folder() / "saves";
}

core::Status MapViewer::save_game(const std::filesystem::path& path) {
    // The installation stays as it is: no savegame goes into it (nor into Game.dat).
    std::error_code ec;
    const auto target = std::filesystem::weakly_canonical(std::filesystem::absolute(path), ec);
    const auto root = std::filesystem::weakly_canonical(m_game.root(), ec);
    const auto rel = target.lexically_relative(root);
    if (!rel.empty() && *rel.begin() != "..") {
        return core::make_error(core::ErrorCode::Unsupported,
                                "will not write into the game's installation: " + core::path_to_utf8(target));
    }
    // Open1602's own savegame, or with the ending .gam one the original loads (as long as the game
    // fits its limits; without the file the game came from, the fields Open1602 does not keep are 0).
    std::string ending = core::path_to_utf8(target.extension());
    std::transform(ending.begin(), ending.end(), ending.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto bytes = ending == ".gam" ? sim::write_savegame(m_state->world, m_buildings, &m_state->simulation)
                                  : sim::write_game(m_state->world, m_buildings, &m_state->simulation);
    if (!bytes) {
        return bytes.error();
    }
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
    }
    // A failed write (a full disk) never leaves half a file in place of the last good one.
    return core::write_file_atomic(target, *bytes);
}

void MapViewer::load_options() {
    // Defaults: the sounds as GameSound took them (the original's Game.dat, read only).
    if (m_sound) {
        m_app_settings.audio = m_sound->settings();
    }
    const std::filesystem::path settings = data_folder() / "settings.cfg";
    if (auto bytes = core::read_file(settings)) {
        m_app_settings = parse_settings(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()),
                                        m_app_settings);
        // What the command line switched off stays off for this run.
        m_app_settings.audio.effects = m_app_settings.audio.effects && m_options.effects;
        m_app_settings.audio.music = m_app_settings.audio.music && m_options.music;
        if (m_sound) {
            m_sound->set_settings(m_app_settings.audio);
        }
    }
    apply_language();
    m_ambient.set_config(ambient_config());
    if (auto bytes = core::read_file(save_folder() / "slots.cfg")) {
        m_slots = parse_slots(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    }
    if (!m_hud) {
        return;
    }
    OptionsModel& model = m_hud->options_model();
    model.audio = m_app_settings.audio;
    model.mission_window = m_app_settings.mission_window;
    model.tracks = m_sound ? m_sound->tracks() : std::vector<std::string>{};
    model.slot_names = m_slots.names;
    model.slot_humans = m_slots.humans;
    if (MainMenu* menu = m_hud->menu()) {
        menu->set_slots(m_slots);
        menu->set_score_folder(data_folder() / "scores");
        menu->set_progress_file(data_folder() / "progress.cfg");
        menu->set_name(m_app_settings.player_name);
    }
}

void MapViewer::apply_language() {
    std::string code = m_options.language;
    if (code.empty() && m_app_settings.language != "auto") {
        code = m_app_settings.language;
    }
    if (code.empty()) {
        const auto texts = assets::TextTable::load(m_game);
        code = ui::installation_language(m_game, texts ? &texts.value() : nullptr);
    }
    ui::set_language(code, data_folder() / "locale");
    std::fprintf(stderr, "language: %s\n", ui::current_language().c_str());
}

void MapViewer::save_settings() {
    const std::string text = write_settings(m_app_settings);
    const std::filesystem::path path = data_folder() / "settings.cfg";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto* data = reinterpret_cast<const std::uint8_t*>(text.data());
    if (auto status = core::write_file_atomic(path, std::span<const std::uint8_t>(data, text.size())); !status) {
        std::fprintf(stderr, "settings: %s\n", status.error().message.c_str());
    }
}

void MapViewer::drop_units() {
    if (!m_units.empty()) {
        m_units.clear();
        selection_changed(false);
    }
}

bool MapViewer::cancel_build_step() {
    if (m_hud && m_hud->close_build_popup()) {
        return true;
    }
    if (m_tool.active()) {
        m_tool.set_mode(BuildTool::Mode::Off);
        return true;
    }
    return false;
}

bool MapViewer::back_to_status() {
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
        if (const auto a = m_hud->set_mode(Hud::kModeInfo)) {
            apply_hud(*a);
        }
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

void MapViewer::sync_text_input() {
    // Every frame, in the menu as in the game: SDL sends no text (SDL_EVENT_TEXT_INPUT) without it.
    const bool typing = m_hud && m_hud->typing();
    if (typing == m_text_input) {
        return;
    }
    m_text_input = typing;
    if (typing) {
        SDL_StartTextInput(m_window->sdl_window());
    } else {
        SDL_StopTextInput(m_window->sdl_window());
    }
}

render::Ambient::Config MapViewer::ambient_config() const {
    render::Ambient::Config config;
    config.sea_interval_ms = static_cast<std::uint32_t>(std::max(m_app_settings.sea_creatures_every, 0)) * 1000U;
    return config;
}

void MapViewer::apply_options() {
    if (!m_hud) {
        return;
    }
    OptionsModel& model = m_hud->options_model();
    if (m_sound) {
        model.playing = m_sound->playing();
    }
    std::int32_t w = 0;
    std::int32_t h = 0;
    m_window->output_size(w, h);
    model.resolution = w >= 1024 ? 2 : (w >= 800 ? 1 : 0);
    for (const OptionsEvent& e : m_hud->take_option_events()) {
        switch (e.kind) {
            case OptionsEvent::Kind::Save: {
                const std::string name =
                    e.text.empty() ? ui::tr("save.default_name", {std::to_string(e.value + 1)}) : e.text;
                const auto status = save_game(slot_file(save_folder(), e.value));
                if (!status) {
                    notify(ui::tr("notice.save_failed", {ui::error_text(status.error(), "save")}));
                    break;
                }
                std::uint8_t humans = 0;
                for (const sim::Player& p : m_state->world.players()) {
                    humans = static_cast<std::uint8_t>(humans + (p.role == sim::PlayerRole::Human ? 1 : 0));
                }
                m_slots.set(e.value, name, humans);
                model.slot_names = m_slots.names;
                model.slot_humans = m_slots.humans;
                const std::string text = write_slots(m_slots);
                const auto* data = reinterpret_cast<const std::uint8_t*>(text.data());
                if (auto written = core::write_file_atomic(save_folder() / "slots.cfg",
                                                           std::span<const std::uint8_t>(data, text.size()));
                    !written) {
                    std::fprintf(stderr, "slots: %s\n", written.error().message.c_str());
                }
                // The voice confirms it (0x0043F71E: Speech_Queue(0x79, 2): files 121 or 123).
                m_sound->speak(121, 2);
                // As the original: after saving, the info mode (0x0043F751).
                if (const auto a = m_hud->set_mode(Hud::kModeInfo)) {
                    apply_hud(*a);
                }
                break;
            }
            case OptionsEvent::Kind::Load: {
                const std::filesystem::path path = slot_file(save_folder(), e.value);
                std::error_code ec;
                if (!std::filesystem::exists(path, ec)) {
                    std::fprintf(stderr, "load: slot %zu is empty\n", e.value + 1);
                    break;
                }
                if (const auto status = load_game(path); !status) {
                    notify(ui::tr("notice.load_failed", {ui::error_text(status.error(), "load")}));
                } else {
                    std::fprintf(stderr, "game loaded: %s\n", core::path_to_utf8(path).c_str());
                }
                break;
            }
            case OptionsEvent::Kind::Quit:
                // The original leaves at once (M1 in the report); Open1602 asks first.
                if (m_options.confirm) {
                    m_quit_pending = true;
                    m_hud->ask(ui::tr("dialog.quit_game"));
                } else {
                    m_quit_pending = true;
                    Hud::Action yes{Hud::Action::Kind::Answer};
                    yes.step = 1;
                    apply_hud(yes);
                }
                break;
            case OptionsEvent::Kind::Resolution: {
                // The original's three sizes as window sizes (in full screen nothing changes).
                constexpr std::array<std::pair<int, int>, 3> kSizes = {{{640, 480}, {800, 600}, {1024, 768}}};
                const auto [rw, rh] = kSizes[std::min<std::size_t>(e.value, 2)];
                SDL_SetWindowSize(m_window->sdl_window(), rw, rh);
                break;
            }
            case OptionsEvent::Kind::Audio:
                m_app_settings.audio = model.audio;
                if (m_sound) {
                    m_sound->set_settings(model.audio);
                    model.tracks = m_sound->tracks();
                }
                save_settings();
                break;
            case OptionsEvent::Kind::PlayTrack:
                if (m_sound) {
                    m_sound->play_track(e.text);
                }
                break;
            case OptionsEvent::Kind::TestEffects:
                if (m_sound) {
                    m_sound->test_effects();
                }
                break;
            case OptionsEvent::Kind::MissionWindow:
                m_app_settings.mission_window = model.mission_window;
                save_settings();
                break;
        }
    }
}

core::Status MapViewer::load_game(const std::filesystem::path& path) {
    if (m_session && m_session->ended()) {
        end_session();  // one that ended in its game stood only to show the game
    }
    if (m_session) {
        return core::make_error(core::ErrorCode::Unsupported, "no other game while a multiplayer game runs");
    }
    if (m_sound) {
        m_sound->prefill();  // the frames stand still while the game loads
    }
    std::size_t warnings = 0;
    auto world = read_world(path, &warnings);
    if (!world) {
        return world.error();
    }
    if (auto replaced = replace_world(std::move(world).value(), path); !replaced) {
        return replaced;
    }
    report_load_problems(warnings);
    return core::Ok{};
}

void MapViewer::report_load_problems(std::size_t warnings) {
    m_load_problems = warnings + (m_state->simulation.state_damaged() ? 1 : 0);
    if (m_load_problems != 0) {
        notify(ui::tr("notice.load_problems", {std::to_string(m_load_problems)}));
    }
}

core::Result<sim::World> MapViewer::read_world(const std::filesystem::path& path, std::size_t* warning_count) const {
    auto bytes = core::read_file(path);
    if (!bytes) {
        return bytes.error();
    }
    std::vector<std::string> warnings;
    core::Result<sim::World> world = core::make_error(core::ErrorCode::NotFound, "no game");
    if (sim::is_open1602_save(*bytes)) {
        world = sim::read_game(*bytes, m_buildings, &warnings, &m_figures);
    } else {
        auto file = assets::ChunkFile::from_bytes(std::move(bytes).value());
        if (!file) {
            return file.error();
        }
        world = sim::load_world(*file, m_buildings, m_game, &warnings, m_options.world);
    }
    for (const auto& w : warnings) {
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    }
    if (warning_count != nullptr) {
        *warning_count = warnings.size();
    }
    return world;
}

core::Status MapViewer::replace_world(sim::World world, const std::filesystem::path& source) {
    // The new game is built whole before anything of the old one goes: if memory runs out on the
    // way, the old game goes on.
    std::unique_ptr<GameState> next;
    try {
        next = std::make_unique<GameState>(std::move(world), m_buildings, m_figures, m_options.rules);
    } catch (const std::bad_alloc&) {
        return core::make_error(core::ErrorCode::PlatformError, "not enough memory for the game");
    }
    // What belongs to the old game goes: videos, selection, orders waiting for an answer, tools.
    stop_video();
    m_videos.clear();
    m_pause_video = false;
    m_end_pending.reset();
    m_end_shown = false;
    m_quit_pending = false;
    m_units.reset();
    m_pending_orders.clear();
    m_pick = Pick::None;
    m_press.reset();
    m_rubber_band = false;
    m_last_group = 0xFF;
    m_formation = 0;
    m_field_source.reset();
    m_ship_trail = render::ShipTrail{};
    m_tick_fraction = 1.0;
    m_tool.set_mode(BuildTool::Mode::Off);
    m_state = std::move(next);
    m_command_sink.set_simulation(&m_state->simulation);
    m_options.scenario = source;
    m_title_real_ms = 0;  // loading took real time the game did not run: no "too slow" for it
    warn_if_state_damaged();
    m_state->simulation.offer_names(m_city_names, m_ship_names);  // a saved game keeps its own
    m_state->simulation.set_tick_observer(
        [this](const sim::World& now) { m_ambient.tick(now, m_buildings, m_figures); });
    // In a multiplayer game the slot the host gave this computer (kNoPlayer: watching).
    m_local_player = m_session ? m_session->local_seat() : sim::first_human_player(m_state->world);
    m_building_figures = render::collect_building_figures(m_state->world, m_buildings, m_figures);
    m_production_phases.clear();
    m_soldiers.reset(m_state->world, m_buildings, m_figures);
    m_walkers = render::WalkerFigures{};
    m_ambient = render::Ambient{1602, ambient_config()};
    m_ambient.tick(m_state->world, m_buildings, m_figures);
    m_effects.clear();
    m_player_figures = sim::PlayerFigures{};
    m_figures_ms = 0;
    if (m_hud) {
        m_hud->set_game(m_state->simulation, m_buildings, m_figures, m_local_player);
        m_hud->reset_game();
    }
    m_camera.set_world_size(m_state->world.width(), m_state->world.height());
    m_camera.center_on(home_tile());
    return core::Ok{};
}

core::Status MapViewer::start_endless(std::uint8_t level, std::uint8_t colour, const std::string& name) {
    if (m_session && m_session->ended()) {
        end_session();  // one that ended in its game stood only to show the game
    }
    if (m_session) {
        return core::make_error(core::ErrorCode::Unsupported, "no other game while a multiplayer game runs");
    }
    auto texts = assets::TextTable::load(m_game);
    if (!texts) {
        return texts.error();
    }
    // What the original rolls with rand() (map layout, start of the island table) and the game id
    // from the clock (0x0048F5A0, 0x00469410).
    std::random_device device;
    sim::NewGameOptions o;
    o.difficulty = std::min<std::uint8_t>(level, sim::NewGameOptions::kDifficulties - 1);
    o.layout = static_cast<std::uint8_t>(device() % sim::NewGameOptions::kLayouts);
    o.colour = std::min<std::uint8_t>(colour, 3);
    o.name = name;
    o.world = m_options.world;
    o.world.seed = (std::uint64_t{device()} << 32U) | device();
    const std::time_t now = std::time(nullptr);
    if (const std::tm* t = std::localtime(&now)) {
        o.game_id =
            sim::new_game_id(static_cast<std::uint32_t>(t->tm_year + 1900), static_cast<std::uint32_t>(t->tm_mon + 1),
                             static_cast<std::uint32_t>(t->tm_mday), static_cast<std::uint32_t>(t->tm_hour),
                             static_cast<std::uint32_t>(t->tm_min), device());
    }
    std::vector<std::string> warnings;
    auto world = sim::start_new_game(m_game, m_buildings, *texts, o, &warnings);
    if (!world) {
        return world.error();
    }
    for (const auto& w : warnings) {
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    }
    return replace_world(std::move(world).value());
}

void MapViewer::apply_menu() {
    if (m_hud && m_hud->end_screen_active()) {
        if (const auto choice = m_hud->take_end_choice()) {
            m_hud->close_end_screen(*choice);
            // To the menus: out of a multiplayer game too, as "Ende" in the settings does.
            if (*choice != EndScreen::Choice::Resume && m_session) {
                end_session();
            }
            // "Neues Spiel": the mission screen of the same scenario alone, the main menu with
            // several players.
            if (*choice == EndScreen::Choice::NewGame && humans() == 1 && m_hud->menu() != nullptr) {
                m_hud->menu()->open_mission(sim::game_identity(m_state->world).name);
            }
        }
        return;
    }
    MainMenu* menu = m_hud ? m_hud->menu() : nullptr;
    if (menu == nullptr) {
        return;
    }
    if (m_hud->multiplayer_open()) {
        apply_multiplayer_menu();
        return;
    }
    menu->set_slots(m_slots);
    for (const MenuEvent& e : m_hud->take_menu_events()) {
        core::Status started = core::make_error(core::ErrorCode::NotFound, "nothing started");
        switch (e.kind) {
            case MenuEvent::Kind::Quit:
                // The original quits at once (U-036).
                m_quit_pending = true;
                if (m_options.confirm) {
                    m_hud->ask(ui::tr("dialog.quit_anno"));
                } else {
                    Hud::Action yes{Hud::Action::Kind::Answer};
                    yes.step = 1;
                    apply_hud(yes);
                }
                continue;
            case MenuEvent::Kind::Credits:
                if (m_video) {
                    request_video(10);
                    if (!m_videos.empty()) {
                        m_videos.back().in_minimap = false;
                        m_videos.back().in_frame = true;
                    }
                }
                continue;
            case MenuEvent::Kind::Intro:
                request_video(58, 0, false, true);
                continue;
            case MenuEvent::Kind::Multiplayer:
                open_multiplayer_menu();
                return;  // the rest belongs to the main menu, which is gone
            case MenuEvent::Kind::NewEndless:
                started = start_endless(e.level, e.colour, e.name);
                break;
            case MenuEvent::Kind::Scenario:
                started = load_game(e.path);
                if (started && !e.name.empty()) {
                    // The stored name goes to the human (0x0048A680).
                    m_command_sink.submit(sim::PlayerRename{m_local_player, e.name});
                }
                break;
            case MenuEvent::Kind::LoadSlot:
                started = load_game(slot_file(save_folder(), e.slot));
                break;
            case MenuEvent::Kind::ContinueLast:
                started = load_game(save_folder() / ("lastgame" + std::string(sim::kSaveExtension)));
                break;
            case MenuEvent::Kind::NameEntered:
                // "Unter welcher Flagge wollt Ihr segeln?" and four more, one at random (Speech8
                // 107..115). The original speaks on entering the screen; Open1602 once the name is in.
                m_sound->speak(107, 5);
                continue;
        }
        if (!started) {
            std::fprintf(stderr, "menu: %s\n", started.error().message.c_str());
            continue;
        }
        if (!e.name.empty() && e.name != m_app_settings.player_name) {
            m_app_settings.player_name = e.name;
            save_settings();
        }
        m_hud->close_menu();
        if (const auto a = m_hud->set_mode(Hud::kModeInfo)) {
            apply_hud(*a);
        }
        break;  // the rest belongs to the menu that is gone
    }
}

void MapViewer::present() {
    if (auto request = std::exchange(m_screenshot, std::nullopt)) {
        const auto status = save_screenshot(request->path);
        m_screenshot_error.reset();
        if (!status) {
            m_screenshot_error = status.error().message;
        }
        if (request->notify) {
            notify(status ? ui::tr("notice.screenshot_saved",
                                   {utf8_to_cp1252(core::path_to_utf8(request->path.filename()))})
                          : ui::tr("notice.screenshot_failed", {ui::error_text(status.error(), "screenshot")}));
        }
    }
    SDL_RenderPresent(m_window->sdl_renderer());
    if (m_hud) {
        m_hud->next_frame();
    }
}

std::filesystem::path MapViewer::next_screenshot_path() {
    const std::filesystem::path folder = data_folder() / "screenshots";
    const std::time_t now = std::time(nullptr);
    char stamp[32] = "open1602";
    if (const std::tm* local = std::localtime(&now)) {
        std::strftime(stamp, sizeof stamp, "open1602-%Y%m%d-%H%M%S", local);
    }
    std::filesystem::path path = folder / (std::string(stamp) + ".png");
    std::error_code ec;
    for (int n = 2; std::filesystem::exists(path, ec) && n < 1000; ++n) {
        path = folder / (std::string(stamp) + "-" + std::to_string(n) + ".png");
    }
    return path;
}

void MapViewer::notify(const std::string& text) {
    std::fprintf(stderr, "%s\n", core::cp1252_to_utf8(text).c_str());
    m_notice = text;
    m_notice_until_ms = platform::ticks_ms() + 4000;
}

core::Status MapViewer::save_screenshot(const std::filesystem::path& path) {
    SDL_Surface* surface = SDL_RenderReadPixels(m_window->sdl_renderer(), nullptr);
    if (surface == nullptr) {
        return platform::sdl_error("SDL_RenderReadPixels");
    }
    SDL_Surface* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);
    if (rgba == nullptr) {
        return platform::sdl_error("SDL_ConvertSurface");
    }
    const auto width = static_cast<std::uint32_t>(rgba->w);
    const auto height = static_cast<std::uint32_t>(rgba->h);
    std::vector<std::uint8_t> pixels(std::size_t{width} * height * 4);
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto* row =
            static_cast<const std::uint8_t*>(rgba->pixels) + std::size_t{y} * static_cast<std::size_t>(rgba->pitch);
        std::copy_n(row, std::size_t{width} * 4,
                    pixels.begin() + static_cast<std::ptrdiff_t>(std::size_t{y} * width * 4));
    }
    SDL_DestroySurface(rgba);
    for (std::size_t i = 3; i < pixels.size(); i += 4) {
        pixels[i] = 255;
    }
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    return core::write_png_rgba(path, width, height, pixels);
}

sim::GameOutcome MapViewer::local_outcome() const {
    const sim::Player* p = m_state->world.player(m_local_player);
    if (p != nullptr && p->won) {
        return sim::GameOutcome::Won;
    }
    if ((p != nullptr && p->status == sim::PlayerStatus::Defeated) || m_state->world.game_end()) {
        return sim::GameOutcome::Lost;
    }
    return sim::GameOutcome::Left;
}

std::size_t MapViewer::humans() const {
    std::size_t n = 0;
    for (const sim::Player& p : m_state->world.players()) {
        n += p.role == sim::PlayerRole::Human && p.status != sim::PlayerStatus::Absent ? 1U : 0U;
    }
    return n;
}

void MapViewer::check_end_screen(std::uint64_t now) {
    if (!m_end_pending || !m_hud) {
        return;
    }
    EndPending& e = *m_end_pending;
    const std::uint64_t step = now >= e.last_ms ? now - e.last_ms : 0;
    e.last_ms = now;
    // WinMain runs no game frame while a video plays, the next video of a chain included.
    if ((m_video && m_video->playing()) || !m_videos.empty()) {
        e.after_video = true;
        return;
    }
    if (!m_state->simulation.paused()) {
        e.left_ms -= std::min(e.left_ms, step);
    }
    if (!e.after_video && e.left_ms > 0) {
        return;
    }
    const sim::GameOutcome outcome = e.outcome;
    m_end_pending.reset();
    // Never in the tutorial (mission 0): it goes on.
    if (sim::game_identity(m_state->world).mission_number == 0) {
        return;
    }
    show_end_screen(outcome, false);
}

void MapViewer::show_end_screen(sim::GameOutcome outcome, bool view_only) {
    if (!m_hud) {
        return;
    }
    if (!view_only) {
        m_end_shown = true;
    }
    m_hud->open_end_screen(sim::end_screen_summary(m_state->world, m_buildings, m_local_player, outcome), view_only);
}

void MapViewer::update_high_scores(sim::PlayerId defeated) {
    const sim::GameIdentity game = sim::game_identity(m_state->world);
    const std::filesystem::path name = high_score_file(game, m_campaign_labels, humans() > 1);
    if (name.empty()) {
        return;
    }
    // Open1602's own list in the data folder; the first line starts from the original's (only read).
    const std::filesystem::path own = data_folder() / "scores" / name;
    auto bytes = core::read_file(own);
    if (!bytes) {
        const std::u8string relative = (std::filesystem::path(u8"Szenes") / name).generic_u8string();
        if (const auto original = m_game.resolve(std::string(relative.begin(), relative.end()))) {
            bytes = core::read_file(*original);
        }
    }
    std::vector<assets::HighScore> scores;
    if (bytes) {
        scores =
            assets::parse_high_scores(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    }
    bool changed = false;
    for (std::size_t i = 0; i < m_state->world.players().size(); ++i) {
        const sim::Player& p = m_state->world.players()[i];
        const auto id = static_cast<sim::PlayerId>(i);
        if (p.role != sim::PlayerRole::Human || (p.status != sim::PlayerStatus::Playing && id != defeated)) {
            continue;  // computers never enter the list
        }
        assets::HighScore entry;
        entry.name = p.name;
        entry.points = sim::player_score(m_state->world, m_buildings, id);
        entry.stars = game.ranking == sim::GameIdentity::kNone ? 0 : game.ranking;
        entry.game_id = game.game_id;
        changed = assets::insert_high_score(scores, std::move(entry)).has_value() || changed;
    }
    if (!changed) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(own.parent_path(), ec);
    const std::string text = assets::write_high_scores(scores);
    const auto* data = reinterpret_cast<const std::uint8_t*>(text.data());
    if (auto status = core::write_file_atomic(own, std::span<const std::uint8_t>(data, text.size())); !status) {
        std::fprintf(stderr, "high scores: %s\n", status.error().message.c_str());
    }
}

void MapViewer::record_progress() {
    const sim::Player* local = m_state->world.player(m_local_player);
    if (local == nullptr || !local->won || humans() != 1) {
        return;
    }
    // Open1602 keeps it in its own file instead of Game.dat.
    const std::filesystem::path file = data_folder() / "progress.cfg";
    assets::CampaignProgress progress;
    if (auto bytes = core::read_file(file)) {
        progress =
            assets::parse_progress(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    }
    const sim::GameIdentity game = sim::game_identity(m_state->world);
    if (progress.record_win(game.campaign, game.ranking, game.mission_number)) {
        const std::string text = assets::write_progress(progress);
        const auto* data = reinterpret_cast<const std::uint8_t*>(text.data());
        if (auto status = core::write_file_atomic(file, std::span<const std::uint8_t>(data, text.size())); !status) {
            std::fprintf(stderr, "progress: %s\n", status.error().message.c_str());
        }
    }
}

}  // namespace o1602::app
