// SPDX-License-Identifier: GPL-3.0-or-later
//
// o1602-replay: runs a game headless for a long time with a log of commands and prints the
// fingerprint of the game (Simulation::state_hash(), the multiplayer's) every N ticks. The output
// on stdout is the same on every computer that computes the same game (ADR 0004): the lines of two
// builds, compilers, optimisation levels or platforms are compared with diff. Timings and sizes go
// to stderr, so that stdout stays comparable.
//
// The game comes from a savegame (.gam, .o1602), a new own world (both need ANNO1602_DIR) or, with
// --synthetic, from a small world made here without the original data (CI): islands, computer
// players that sail out and found their cities, pirates, the free trader and armed ships.
//
// Commands: --record writes the commands the tool makes up (random but valid in form, for the
// players named by --command-players) to a log; --commands replays such a log. Commands are
// scheduled for the tick they are logged for, as a multiplayer session does.
//
// --roundtrip K saves and loads the game every K ticks and goes on with the loaded one (as a
// computer that joins or resyncs); --check-roundtrip K runs that game and one straight through
// side by side and stops at the first checkpoint where they differ (exit code 3).

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/core/byte_reader.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/core/file.hpp"
#include "o1602/sim/command_io.hpp"
#include "o1602/sim/new_game.hpp"
#include "o1602/sim/random.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/ship.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/sim/world_plan.hpp"

namespace o1602::replay {

namespace {

namespace fs = std::filesystem;

/// "--name value" / "--flag" options and positional arguments.
struct Args {
    std::vector<std::string_view> positional;
    std::map<std::string_view, std::string_view> options;

    Args(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg.starts_with("--")) {
                const bool has_value = i + 1 < argc && !std::string_view(argv[i + 1]).starts_with("--");
                options[arg] = has_value ? std::string_view(argv[++i]) : std::string_view();
            } else {
                positional.push_back(arg);
            }
        }
    }

    [[nodiscard]] bool flag(std::string_view name) const { return options.contains(name); }

    [[nodiscard]] std::optional<std::size_t> number(std::string_view name) const {
        const auto it = options.find(name);
        if (it == options.end()) {
            return std::nullopt;
        }
        std::size_t value = 0;
        const auto [ptr, ec] = std::from_chars(it->second.data(), it->second.data() + it->second.size(), value);
        if (ec != std::errc{} || ptr != it->second.data() + it->second.size()) {
            return std::nullopt;
        }
        return value;
    }

    [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> pair(std::string_view name,
                                                                            char separator) const {
        const auto it = options.find(name);
        if (it == options.end()) {
            return std::nullopt;
        }
        const std::string_view v = it->second;
        const auto sep = v.find(separator);
        if (sep == std::string_view::npos) {
            return std::nullopt;
        }
        std::int32_t a = 0;
        std::int32_t b = 0;
        if (std::from_chars(v.data(), v.data() + sep, a).ec != std::errc{} ||
            std::from_chars(v.data() + sep + 1, v.data() + v.size(), b).ec != std::errc{}) {
            return std::nullopt;
        }
        return std::pair{a, b};
    }
};

int fail(const core::Error& error) {
    std::fprintf(stderr, "error: %s\n", error.message.c_str());
    return 1;
}

int usage() {
    std::fputs(
        "usage: o1602-replay [<file.gam|.o1602> | --synthetic [--islands N] [--sea N]] [--players N] [--world WxH]\n"
        "                    [--seed N] [--level 0-3] [--all-computer] [--minutes M] [--every TICKS]\n"
        "                    [--commands LOG] [--random-commands N] [--command-seed N] [--record LOG]\n"
        "                    [--roundtrip TICKS] [--check-roundtrip TICKS] [--dump DIR]\n"
        "  Runs a game headless and prints the state hash every TICKS ticks (default 1200, a game minute) on\n"
        "  stdout, the same on every platform and build; timings and sizes go to stderr. Without a file and\n"
        "  without --synthetic a new own world (needs ANNO1602_DIR). --random-commands: N made-up commands\n"
        "  per game minute for the human players (written to --record). --roundtrip: save and load every\n"
        "  TICKS ticks and go on with the loaded game. --check-roundtrip: also run a game saved and loaded\n"
        "  every TICKS ticks and exit 3 at the first checkpoint where the two differ.\n",
        stderr);
    return 2;
}

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

constexpr char kLogMagic[8] = {'O', '1', '6', '0', '2', 'C', 'M', 'D'};
constexpr std::uint32_t kLogVersion = 1;

// --- The synthetic world (no original data) ---

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kSea = 21201;
constexpr std::uint32_t kBeach = 20501;
constexpr std::uint32_t kTree = 20202;
constexpr std::uint16_t kTradeShip = 21;
constexpr std::uint16_t kWarship = 25;

/// Ground, sea, beach, a tree, two families of roads and the Kontor the computers found with (as
/// tests/unit/test_ai.cpp), so that the computer players sail, found and build roads.
core::Result<assets::BuildingTable> synthetic_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "  @Nummer: 1\n  Id: 21201\n  Kind: 19\n  Gfx: 1\n  Size: 1, 1\n"
        "  @Nummer: 2\n  Id: 20501\n  Kind: 23\n  Gfx: 2\n  Size: 1, 1\n  Destroyflg: 1\n"
        "  @Nummer: 3\n  Id: 20202\n  Kind: 10\n  Gfx: 3\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 53\n  EndObj\n"
        "  @Nummer: 4\n  Id: 20401\n  Kind: 1\n  Gfx: 4\n  Size: 1, 1\n  KreuzBase: 4\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 5\n  EndObj\n"
        "  @Nummer: 5\n  Id: 20402\n  Kind: 1\n  Gfx: 5\n  Size: 1, 1\n  KreuzBase: 4\n"
        "  @Nummer: 6\n  Id: 20403\n  Kind: 1\n  Gfx: 6\n  Size: 1, 1\n  KreuzBase: 4\n"
        "  @Nummer: 7\n  Id: 20404\n  Kind: 1\n  Gfx: 7\n  Size: 1, 1\n  KreuzBase: 4\n"
        "  @Nummer: 8\n  Id: 20411\n  Kind: 1\n  Gfx: 8\n  Size: 1, 1\n  KreuzBase: 8\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 2\n  EndObj\n"
        "  @Nummer: 9\n  Id: 20412\n  Kind: 1\n  Gfx: 9\n  Size: 1, 1\n  KreuzBase: 8\n"
        "  @Nummer: 10\n  Id: 20413\n  Kind: 1\n  Gfx: 10\n  Size: 1, 1\n  KreuzBase: 8\n"
        "  @Nummer: 11\n  Id: 20414\n  Kind: 1\n  Gfx: 11\n  Size: 1, 1\n  KreuzBase: 8\n"
        "  @Nummer: 12\n  Id: 22101\n  Kind: 35\n  Gfx: 20\n  Size: 2, 3\n  Strandflg: 1\n  PlaceFlg: 1\n"
        "  KreuzBase: 12\n  Ausbauflg: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 8\n    Maxlager: 30\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Werkzeug: 3\n    Holz: 4\n    Money: 100\n  EndObj\n"
        "EndObj\n",
        types);
    return assets::BuildingTable::from_cod(doc);
}

/// A trading cog and a warship with guns (as tests/unit/test_naval.cpp).
core::Result<assets::FigureTable> synthetic_figures() {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 4\n  Maxkanon: 6\n  Speed: 500\n"
        "  Drehtime: 0.13\n  Maxenergy: 50\n  Hitpoint: 2.0\n  Shotradius: 7\n  Shottime: 1.0\n  Worktime: 5.0\n"
        "  Stirbtime: 3.0\n  Preis: 1000\n  Rohstoff: 23\n  Rohmenge: 25\n"
        "  Nummer: 25\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 3\n  Maxkanon: 8\n  Speed: 550\n"
        "  Drehtime: 0.13\n  Maxenergy: 65\n  Hitpoint: 2.0\n  Shotradius: 7\n  Shottime: 1.0\n  Worktime: 5.0\n"
        "  Stirbtime: 4.0\n"
        "EndObj\n",
        types);
    return assets::FigureTable::from_cod(doc);
}

/// `side` x `side` islands of 50 x 50 tiles in a sea (ground, a wood, a beach facing east), four
/// settler players (0 and 1 human, 2 and 3 computers, each with a trading cog and a warship), the
/// free trader, the natives and the pirates. The sea is at least `sea` tiles wide and high.
core::Result<sim::World> synthetic_world(const assets::BuildingTable& buildings, std::uint32_t side,
                                         std::uint32_t sea) {
    const std::uint32_t cell = 90;
    sim::World world(std::max(cell * side, sea), std::max(cell * side, sea));
    for (std::uint32_t iy = 0; iy < side; ++iy) {
        for (std::uint32_t ix = 0; ix < side; ++ix) {
            sim::IslandMap map(50, 50);
            for (std::int32_t y = 0; y < 50; ++y) {
                for (std::int32_t x = 0; x < 50; ++x) {
                    std::uint32_t id = kGround;
                    if (x > 46) {
                        id = kSea;
                    } else if (x == 46) {
                        id = kBeach;
                    } else if (x >= 32 && x <= 43) {
                        id = kTree;
                    }
                    if (auto placed = map.place(*buildings.by_id(id), x, y, id == kBeach ? 3 : 0, 0); !placed) {
                        return placed.error();
                    }
                }
            }
            map.capture_ground();
            sim::WorldIsland island{assets::IslandHeader{}, std::move(map), ix * cell + 20, iy * cell + 20};
            island.header.number = static_cast<std::uint8_t>(world.islands().size());
            island.header.width = 50;
            island.header.height = 50;
            island.header.fertility = 0xFFFFFFFFU;
            if (auto added = world.add_island(std::move(island)); !added) {
                return added.error();
            }
        }
    }
    std::vector<sim::Player> players(7);
    for (std::size_t p = 0; p < 4; ++p) {
        players[p].role = p < 2 ? sim::PlayerRole::Human : sim::PlayerRole::Computer;
        players[p].money = 20000;
        players[p].banner = static_cast<sim::BannerId>(p);
        players[p].name = "Player " + std::to_string(p);
    }
    sim::make_neutral(players[4], sim::faction::kTrader);
    sim::make_neutral(players[5], sim::faction::kNatives);
    sim::make_neutral(players[6], sim::faction::kPirates);
    world.set_players(players);
    for (std::uint32_t p = 0; p < 4; ++p) {
        // East of the first islands, in open sea.
        const std::uint32_t x = (p % side) * cell + 80;
        const std::uint32_t y = (p / side % side) * cell + 30 + (p / (side * side)) * 10;
        sim::Ship trade =
            sim::make_ship("Cog " + std::to_string(p), x, y, kTradeShip, static_cast<sim::PlayerId>(p), 2);
        trade.cargo.resize(4);
        world.add_ship(trade);
        sim::Ship war =
            sim::make_ship("War " + std::to_string(p), x, y + 6, kWarship, static_cast<sim::PlayerId>(p), 2);
        war.cargo.resize(3);
        war.cannons = 8;
        world.add_ship(war);
    }
    sim::Ship pirate = sim::make_ship("Pirate", cell * side / 2, cell * side / 2 + 5, kWarship, 6, 2);
    pirate.cargo.resize(3);
    pirate.cannons = 8;
    world.add_ship(pirate);
    return world;
}

// --- Commands ---

/// A default command of variant index `index`.
template <std::size_t... I>
sim::Command command_of_index(std::size_t index, std::index_sequence<I...>) {
    sim::Command out;
    static_cast<void>(((index == I ? (out = sim::Command{std::in_place_index<I>}, true) : false) || ...));
    return out;
}

sim::Command command_of_index(std::size_t index) {
    return command_of_index(index, std::make_index_sequence<std::variant_size_v<sim::Command>>{});
}

/// Makes up commands for a player: a command of a random kind with random bytes in it (as a
/// damaged or hostile message could carry), then pointed at things that exist (own and other
/// ships and soldiers, islands, places on the map) most of the time, so that they also do something.
class CommandMaker {
public:
    explicit CommandMaker(std::uint64_t seed) : m_random(seed, 0x434D44) {}

    std::optional<sim::Command> make(const sim::World& world, sim::PlayerId player) {
        for (int attempt = 0; attempt < 64; ++attempt) {
            const auto kind = m_random.below(static_cast<std::uint32_t>(std::variant_size_v<sim::Command>));
            std::vector<std::uint8_t> bytes = sim::encode_command(command_of_index(kind));
            // A few random bytes after the kind; more often none, so that most commands decode.
            const std::uint32_t flips = m_random.below(4);
            for (std::uint32_t i = 0; i < flips && bytes.size() > 1; ++i) {
                bytes[1 + m_random.below(static_cast<std::uint32_t>(bytes.size() - 1))] =
                    static_cast<std::uint8_t>(m_random.next());
            }
            auto decoded = sim::decode_command(bytes);
            if (!decoded) {
                continue;
            }
            sim::Command c = std::move(*decoded);
            // The host lets no player give these (net::HostSession::commands).
            if (std::holds_alternative<sim::PlayerControl>(c) || std::holds_alternative<sim::GameSpeedOrder>(c)) {
                continue;
            }
            std::visit([&](auto& command) { aim(command, world, player); }, c);
            return c;
        }
        return std::nullopt;
    }

private:
    std::uint32_t any_ship(const sim::World& world, sim::PlayerId owner, bool own) {
        std::vector<std::uint32_t> ids;
        for (const sim::Ship& s : world.ships()) {
            if ((s.owner == owner) == own) {
                ids.push_back(s.id);
            }
        }
        return ids.empty() ? m_random.next() : ids[m_random.below(static_cast<std::uint32_t>(ids.size()))];
    }
    std::uint32_t any_soldier(const sim::World& world, sim::PlayerId owner, bool own) {
        std::vector<std::uint32_t> ids;
        for (const sim::Soldier& s : world.soldiers()) {
            if ((s.owner == owner) == own) {
                ids.push_back(s.id);
            }
        }
        return ids.empty() ? m_random.next() : ids[m_random.below(static_cast<std::uint32_t>(ids.size()))];
    }
    void aim_target(sim::Target& t, const sim::World& world, sim::PlayerId player) {
        switch (m_random.below(4)) {
            case 0: {
                // One draw per statement: the order of function arguments differs between compilers.
                const std::uint32_t x = m_random.below(world.width());
                const std::uint32_t y = m_random.below(world.height());
                t = sim::Target::position(x, y);
                break;
            }
            case 1:
                t = sim::Target::other_ship(any_ship(world, player, false));
                break;
            case 2:
                t = sim::Target::soldier(any_soldier(world, player, false));
                break;
            default:
                break;  // as made up
        }
    }

    template <class C>
    void aim(C& c, const sim::World& world, sim::PlayerId player) {
        c.player = player;
        const bool point = m_random.below(8) != 0;  // most of the time at something that exists
        if constexpr (requires { c.ship; }) {
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype(c.ship)>, std::uint32_t>) {
                if (point) {
                    c.ship = any_ship(world, player, true);
                }
            }
        }
        if constexpr (requires { c.soldiers; }) {
            if (point) {
                c.soldiers.clear();
                const std::uint32_t n = 1 + m_random.below(8);
                for (std::uint32_t i = 0; i < n; ++i) {
                    c.soldiers.push_back(any_soldier(world, player, true));
                }
            }
        }
        if constexpr (requires { c.island; }) {
            if (point && !world.islands().empty()) {
                c.island =
                    static_cast<decltype(c.island)>(m_random.below(static_cast<std::uint32_t>(world.islands().size())));
            }
        }
        if constexpr (requires { c.target; }) {
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype(c.target)>, sim::Target>) {
                if (point) {
                    aim_target(c.target, world, player);
                }
            }
        }
        if constexpr (requires { c.partner; }) {
            if (point) {
                c.partner =
                    static_cast<sim::PlayerId>(m_random.below(static_cast<std::uint32_t>(world.players().size())));
            }
        }
    }

    sim::Random m_random;
};

struct Log {
    std::vector<sim::ScheduledCommand> commands;  ///< by tick
    std::size_t next = 0;
};

core::Result<Log> read_log(const fs::path& path) {
    auto bytes = core::read_file(path);
    if (!bytes) {
        return bytes.error();
    }
    core::ByteReader in(*bytes);
    const auto magic = in.bytes(sizeof kLogMagic);
    const auto version = in.u32();
    if (!magic || !std::equal(magic->begin(), magic->end(), std::begin(kLogMagic)) || version != kLogVersion) {
        return core::make_error(core::ErrorCode::InvalidFormat, "not a command log of this version");
    }
    Log log;
    while (!in.at_end()) {
        auto c = sim::read_scheduled(in);
        if (!c || (!log.commands.empty() && c->tick < log.commands.back().tick)) {
            return core::make_error(core::ErrorCode::InvalidFormat,
                                    "damaged command log after " + std::to_string(log.commands.size()) + " commands");
        }
        log.commands.push_back(std::move(*c));
    }
    return log;
}

// --- The game ---

struct Tables {
    assets::BuildingTable buildings;
    assets::FigureTable figures;
};

/// A running game that can be saved and loaded in place.
struct Run {
    const Tables* tables = nullptr;
    std::unique_ptr<sim::World> world;
    std::unique_ptr<sim::Simulation> simulation;

    void start(sim::World w) {
        simulation.reset();
        world = std::make_unique<sim::World>(std::move(w));
        simulation = std::make_unique<sim::Simulation>(*world, tables->buildings, tables->figures);
    }

    core::Result<core::Bytes> save() const {
        return sim::write_game(*world, tables->buildings, simulation.get(), sim::SaveOptions{false});
    }

    core::Status reload() {
        auto bytes = save();
        if (!bytes) {
            return bytes.error();
        }
        auto loaded = sim::read_game(*bytes, tables->buildings, nullptr, &tables->figures);
        if (!loaded) {
            return loaded.error();
        }
        start(std::move(loaded).value());
        if (simulation->state_damaged()) {
            return core::make_error(core::ErrorCode::InvalidFormat, "the saved state of the game could not be read");
        }
        return core::Ok{};
    }

    /// Schedules `commands` for the next tick and runs it; events are taken as an interface would.
    void tick(const std::vector<sim::Command>& commands) {
        const std::uint64_t now = simulation->next_tick();
        for (const sim::Command& c : commands) {
            simulation->schedule(now, c);
        }
        simulation->tick();
        (void)simulation->take_production_events();
        (void)simulation->take_construction_events();
        (void)simulation->take_military_events();
        (void)simulation->take_city_events();
        (void)simulation->take_ship_events();
        (void)simulation->take_diplomacy_notices();
        (void)simulation->take_command_results();
        (void)simulation->take_house_events();
        (void)simulation->take_island_events();
        (void)simulation->take_disaster_events();
        (void)simulation->take_volcano_events();
        (void)simulation->take_game_events();
    }
};

int run(const Args& args) {
    if (args.positional.size() > 1 || args.flag("--help")) {
        return usage();
    }
    const bool synthetic = args.flag("--synthetic");
    std::optional<assets::GameDirectory> game;
    Tables tables;
    if (synthetic) {
        auto b = synthetic_buildings();
        auto f = synthetic_figures();
        if (!b) {
            return fail(b.error());
        }
        if (!f) {
            return fail(f.error());
        }
        tables = Tables{std::move(b).value(), std::move(f).value()};
    } else {
        auto dir = assets::GameDirectory::from_environment();
        if (!dir) {
            return fail(dir.error());
        }
        game = std::move(dir).value();
        auto b = assets::BuildingTable::load(*game);
        if (!b) {
            return fail(b.error());
        }
        auto f = assets::FigureTable::load(*game);
        if (!f) {
            return fail(f.error());
        }
        tables = Tables{std::move(b).value(), std::move(f).value()};
    }

    std::vector<std::string> warnings;
    const auto make_world = [&]() -> core::Result<sim::World> {
        if (synthetic) {
            return synthetic_world(
                tables.buildings,
                static_cast<std::uint32_t>(std::clamp<std::size_t>(args.number("--islands").value_or(2), 1, 8)),
                static_cast<std::uint32_t>(
                    std::min<std::size_t>(args.number("--sea").value_or(0), sim::World::kMaxSide)));
        }
        if (!args.positional.empty()) {
            auto bytes = core::read_file(fs::path(args.positional[0]));
            if (!bytes) {
                return bytes.error();
            }
            if (sim::is_open1602_save(*bytes)) {
                return sim::read_game(*bytes, tables.buildings, &warnings, &tables.figures);
            }
            auto file = assets::ChunkFile::from_bytes(*bytes);
            if (!file) {
                return file.error();
            }
            return sim::load_world(*file, tables.buildings, *game, &warnings);
        }
        auto texts = assets::TextTable::load(*game);
        if (!texts) {
            return texts.error();
        }
        sim::NewGameOptions options;
        options.name = "Replay";
        options.difficulty = static_cast<std::uint8_t>(std::min<std::size_t>(args.number("--level").value_or(1), 3));
        options.world.seed = args.number("--seed").value_or(1);
        options.world.guarantee_resources = true;
        sim::WorldSettings settings = sim::settings_for_players(static_cast<std::uint8_t>(
            std::clamp<std::size_t>(args.number("--players").value_or(4), 1, sim::WorldSettings::kMaxPlayers)));
        if (const auto size = args.pair("--world", 'x')) {
            settings.width = static_cast<std::uint32_t>(std::max(0, size->first));
            settings.height = static_cast<std::uint32_t>(std::max(0, size->second));
        }
        options.custom = settings;
        return sim::start_new_game(*game, tables.buildings, *texts, options, &warnings);
    };
    auto world = make_world();
    if (!world) {
        return fail(world.error());
    }
    for (const auto& text : warnings) {
        std::fprintf(stderr, "warning: %s\n", text.c_str());
    }

    // Who gives the made-up commands: the human players as they are at the start.
    std::vector<sim::PlayerId> commanders;
    for (std::size_t p = 0; p < world->players().size() && p < sim::kNoPlayer; ++p) {
        if (world->players()[p].role == sim::PlayerRole::Human) {
            commanders.push_back(static_cast<sim::PlayerId>(p));
        }
    }

    Run straight;
    straight.tables = &tables;
    straight.start(std::move(world).value());
    if (args.flag("--all-computer")) {
        for (sim::PlayerId p = sim::first_human_player(*straight.world); p != sim::kNoPlayer;
             p = sim::first_human_player(*straight.world)) {
            if (!straight.simulation->execute(sim::PlayerControl{p, true}).accepted) {
                return fail(core::make_error(core::ErrorCode::Unsupported,
                                             "player " + std::to_string(p) + " cannot be handed to the computer"));
            }
        }
    }

    const std::uint64_t ticks_per_minute = 60'000 / sim::Simulation::kTickMs;
    const std::uint64_t ticks = std::max<std::size_t>(args.number("--minutes").value_or(60), 1) * ticks_per_minute;
    const std::uint64_t every = std::max<std::size_t>(args.number("--every").value_or(ticks_per_minute), 1);
    const std::uint64_t roundtrip = args.number("--roundtrip").value_or(0);
    const std::uint64_t check = args.number("--check-roundtrip").value_or(0);
    const std::uint32_t per_minute = static_cast<std::uint32_t>(args.number("--random-commands").value_or(0));

    Log log;
    if (const auto it = args.options.find("--commands"); it != args.options.end()) {
        auto read = read_log(fs::path(it->second));
        if (!read) {
            return fail(read.error());
        }
        log = std::move(read).value();
    }
    core::ByteWriter record;
    const bool recording = args.options.contains("--record");
    if (recording) {
        record.bytes(std::span(reinterpret_cast<const std::uint8_t*>(kLogMagic), sizeof kLogMagic));
        record.u32(kLogVersion);
    }
    CommandMaker maker(args.number("--command-seed").value_or(7));
    sim::Random when(args.number("--command-seed").value_or(7), 0x5748454E);

    // The second game for --check-roundtrip: from a save of the first, so both start alike.
    Run checked;
    checked.tables = &tables;
    if (check != 0) {
        auto bytes = straight.save();
        if (!bytes) {
            return fail(bytes.error());
        }
        auto loaded = sim::read_game(*bytes, tables.buildings, nullptr, &tables.figures);
        if (!loaded) {
            return fail(loaded.error());
        }
        checked.start(std::move(loaded).value());
        if (straight.simulation->state_hash() != checked.simulation->state_hash()) {
            std::printf("start: the loaded game differs from the one saved\n");
            return 3;
        }
    }

    std::printf("# replay %s, %zu islands, %zu players, %llu ticks, hash every %llu ticks%s\n",
                synthetic ? "synthetic" : (args.positional.empty() ? "new world" : "savegame"),
                straight.world->islands().size(), straight.world->players().size(),
                static_cast<unsigned long long>(ticks), static_cast<unsigned long long>(every),
                roundtrip != 0 ? ", saved and loaded in between" : "");
    std::fprintf(stderr, "tick       hash             hash ms  save bytes  save ms  load ms\n");
    std::vector<sim::Command> commands;
    double max_hash_ms = 0.0;
    double max_save_ms = 0.0;
    std::size_t max_save_bytes = 0;
    std::uint64_t made = 0;
    const auto started = Clock::now();
    for (std::uint64_t t = 1; t <= ticks; ++t) {
        const std::uint64_t now = straight.simulation->next_tick();
        commands.clear();
        while (log.next < log.commands.size() && log.commands[log.next].tick <= now) {
            if (log.commands[log.next].tick == now) {
                commands.push_back(log.commands[log.next].command);
            }
            ++log.next;
        }
        if (per_minute != 0 && !commanders.empty() &&
            when.below(static_cast<std::uint32_t>(ticks_per_minute)) < per_minute) {
            const sim::PlayerId p = commanders[when.below(static_cast<std::uint32_t>(commanders.size()))];
            if (auto c = maker.make(*straight.world, p)) {
                commands.push_back(*c);
                ++made;
            }
        }
        if (recording) {
            for (const sim::Command& c : commands) {
                sim::write_scheduled(record, sim::ScheduledCommand{now, c});
            }
        }
        straight.tick(commands);
        if (check != 0) {
            checked.tick(commands);
            if (t % check == 0) {
                if (auto status = checked.reload(); !status) {
                    return fail(status.error());
                }
            }
        }
        if (roundtrip != 0 && t % roundtrip == 0) {
            const auto start = Clock::now();
            if (auto status = straight.reload(); !status) {
                return fail(status.error());
            }
            std::fprintf(stderr, "%-10llu reloaded in %.1f ms\n", static_cast<unsigned long long>(t), ms_since(start));
        }
        if (t % every == 0 || t == ticks) {
            auto start = Clock::now();
            const std::uint64_t hash = straight.simulation->state_hash();
            const double hash_ms = ms_since(start);
            start = Clock::now();
            auto save = sim::write_game(*straight.world, tables.buildings, straight.simulation.get());
            const double save_ms = ms_since(start);
            const std::size_t save_bytes = save ? save->size() : 0;
            start = Clock::now();
            if (save) {
                (void)sim::read_game(*save, tables.buildings, nullptr, &tables.figures);
            }
            const double load_ms = ms_since(start);
            max_hash_ms = std::max(max_hash_ms, hash_ms);
            max_save_ms = std::max(max_save_ms, save_ms);
            max_save_bytes = std::max(max_save_bytes, save_bytes);
            std::printf("%llu %016llx\n",
                        static_cast<unsigned long long>(straight.simulation->game_time_ms() / sim::Simulation::kTickMs),
                        static_cast<unsigned long long>(hash));
            std::fflush(stdout);
            std::fprintf(stderr, "%-10llu %016llx %7.1f %11zu %8.1f %8.1f  ships %zu soldiers %zu cities %zu\n",
                         static_cast<unsigned long long>(t), static_cast<unsigned long long>(hash), hash_ms, save_bytes,
                         save_ms, load_ms, straight.world->ships().size(), straight.world->soldiers().size(),
                         straight.world->cities().size());
            if (check != 0) {
                const std::uint64_t other = checked.simulation->state_hash();
                if (other != hash) {
                    std::printf("tick %llu: the game saved and loaded every %llu ticks differs: %016llx\n",
                                static_cast<unsigned long long>(t), static_cast<unsigned long long>(check),
                                static_cast<unsigned long long>(other));
                    if (const auto dir = args.options.find("--dump"); dir != args.options.end()) {
                        const auto a = straight.save();
                        const auto b = checked.save();
                        if (a && b) {
                            (void)core::write_file(fs::path(dir->second) / "straight.o1602", *a);
                            (void)core::write_file(fs::path(dir->second) / "roundtrip.o1602", *b);
                        }
                    }
                    return 3;
                }
            }
        }
    }
    if (recording) {
        if (auto status = core::write_file(fs::path(args.options.at("--record")), record.take()); !status) {
            return fail(status.error());
        }
    }
    std::fprintf(
        stderr,
        "done in %.1f s; %llu commands made up, %zu from the log; state hash at most %.1f ms, savegame at most "
        "%zu bytes and %.1f ms\n",
        ms_since(started) / 1000.0, static_cast<unsigned long long>(made), log.commands.size(), max_hash_ms,
        max_save_bytes, max_save_ms);
    return 0;
}

}  // namespace

}  // namespace o1602::replay

int main(int argc, char** argv) {
    return o1602::replay::run(o1602::replay::Args(argc, argv));
}
