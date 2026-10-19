// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sections put together: which are written in which order, and the reading of them all before
// anything is applied. Each section's records are in its own file (section_io.hpp).
#include "sections.hpp"

#include <algorithm>
#include <array>
#include <optional>

#include "section_io.hpp"

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/chunk_writer.hpp"
#include "o1602/sim/simulation.hpp"

namespace o1602::sim::save {

using core::ErrorCode;
using core::make_error;

namespace {

constexpr std::uint32_t tag(const char (&s)[5]) noexcept {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(s[0])) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(s[1])) << 8 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(s[2])) << 16 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(s[3])) << 24;
}

// Sections (version 1 unless said otherwise). A section that changes in a way older readers cannot
// follow gets the next version; a reader skips what it cannot read unless the section is required.
constexpr std::uint32_t kRules = tag("RULE");
constexpr std::uint32_t kWorld = tag("WRLD");
constexpr std::uint32_t kIslands = tag("ISLE");
constexpr std::uint32_t kTiles = tag("TILE");
constexpr std::uint32_t kPlayers = tag("PLAY");
constexpr std::uint32_t kCities = tag("CITY");
constexpr std::uint32_t kProductions = tag("PROD");
constexpr std::uint32_t kHouses = tag("HOUS");
constexpr std::uint32_t kGrowth = tag("GROW");
constexpr std::uint32_t kCosts = tag("COST");
constexpr std::uint32_t kShips = tag("SHIP");
constexpr std::uint32_t kSoldiers = tag("SOLD");
constexpr std::uint32_t kAnimals = tag("ANIM");
constexpr std::uint32_t kDisasters = tag("DISA");
constexpr std::uint32_t kVolcanoes = tag("VOLC");
constexpr std::uint32_t kSimulation = tag("SIMU");
constexpr std::uint32_t kFleet = tag("FLEE");
constexpr std::uint32_t kEpochs = tag("EPOC");
constexpr std::uint32_t kArmy = tag("ARMY");
constexpr std::uint32_t kDiplomacy = tag("DIPL");
constexpr std::uint32_t kExplored = tag("XPLR");
// Only in Open1602's own savegame (save_game.hpp): the whole map, further world settings, what the
// file holds.
constexpr std::uint32_t kMap = tag("MAPW");
constexpr std::uint32_t kWorld2 = tag("WRL2");
constexpr std::uint32_t kMeta = tag("META");
constexpr std::uint32_t kMissions = tag("MISN");
constexpr std::uint32_t kPlayerState = tag("PSTA");
constexpr std::uint32_t kCityState = tag("CSTA");
/// The score's counters of the players and the monument clocks of the cities.
constexpr std::uint32_t kScores = tag("SCOR");
/// The original's file the game came from, without the island tiles (Open1602's own savegame only):
/// the export to a .GAM takes the bytes Open1602 does not model from it (savegame_writer.cpp).
constexpr std::uint32_t kOrigin = tag("ORIG");
/// Orders waiting behind the current one of ships and units (Shift + right click).
constexpr std::uint32_t kQueues = tag("QUEU");
/// Attack move and hold position of ships and units (Open1602, ADR 0010).
constexpr std::uint32_t kStances = tag("STNC");

/// Every section this version reads; others are of a later version.
constexpr std::array kKnownSections = {
    kRules,    kWorld,       kIslands,   kTiles,     kPlayers,  kCities,    kProductions, kHouses,
    kGrowth,   kCosts,       kShips,     kSoldiers,  kAnimals,  kDisasters, kVolcanoes,   kSimulation,
    kFleet,    kEpochs,      kArmy,      kDiplomacy, kExplored, kMap,       kWorld2,      kMeta,
    kMissions, kPlayerState, kCityState, kOrigin,    kQueues,   kStances,   kScores};

/// Section flag (the u16 after the version): the section holds state the game cannot do without, so
/// a reader that cannot read it (unknown identifier or later version) must not load the file. Other
/// sections it skips with a warning. Files of earlier versions have no flags: all sections optional.
constexpr std::uint16_t kRequired = 1;

/// Whether a section is required (kRequired) when written.
enum class Need : std::uint8_t { Required, Optional };

/// The highest version of a section this version reads.
constexpr std::uint16_t max_version(std::uint32_t id) noexcept {
    return id == kIslands || id == kVolcanoes ? kWideCoordinates : 1;
}

/// Writes a section: tag, version, flags, size, payload.
template <class Fn>
void section(core::ByteWriter& out, std::uint32_t id, Fn&& body, Need need = Need::Required,
             std::uint16_t version = 1) {
    out.u32(id);
    out.u16(version);
    out.u16(need == Need::Required ? kRequired : 0);
    const std::size_t size_at = out.size();
    out.u32(0);
    const std::size_t start = out.size();
    Writer writer(out);
    body(writer);
    out.patch_u32(size_at, static_cast<std::uint32_t>(out.size() - start));
}

/// The identifier of a section as text, for messages.
std::string tag_name(std::uint32_t id) {
    std::string name;
    for (std::uint32_t i = 0; i < 4; ++i) {
        const auto c = static_cast<char>((id >> (8 * i)) & 0xFFU);
        name.push_back(c >= ' ' && c <= '~' ? c : '?');
    }
    return name;
}

/// The data of a section.
using Body = std::span<const std::uint8_t>;

/// Calls `fn(id, version, body)` for every section this version reads (a known identifier up to its
/// max_version);
/// an error if the sections are damaged or `fn` returns false. A section of a later version is
/// skipped and reported in `warnings`, or fails the load if it is required (kRequired).
template <class Fn>
core::Status for_each_section(std::span<const std::uint8_t> data, std::vector<std::string>* warnings, Fn&& fn) {
    core::ByteReader reader(data);
    while (!reader.at_end()) {
        const auto id = reader.u32();
        const auto section_version = reader.u16();
        const auto flags = reader.u16();
        const auto size = reader.u32();
        if (!id || !section_version || !flags || !size) {
            return make_error(ErrorCode::InvalidFormat, "O1602: damaged section header");
        }
        const auto body = reader.bytes(*size);
        if (!body) {
            return make_error(ErrorCode::InvalidFormat, "O1602: damaged section header");
        }
        if (*section_version == 0 || *section_version > max_version(*id) ||
            std::find(kKnownSections.begin(), kKnownSections.end(), *id) == kKnownSections.end()) {
            const std::string what = "section " + tag_name(*id) + " (version " + std::to_string(*section_version) + ")";
            if ((*flags & kRequired) != 0) {
                return make_error(ErrorCode::Unsupported, "O1602: " + what + " needs a newer Open1602");
            }
            if (warnings != nullptr) {
                warnings->push_back(what + " is of a newer Open1602 and was skipped");
            }
            continue;
        }
        if (!fn(*id, *section_version, *body)) {
            return make_error(ErrorCode::InvalidFormat, "O1602: damaged section " + tag_name(*id));
        }
    }
    return core::Ok{};
}

}  // namespace

core::Bytes encode_sections(const World& world, const assets::BuildingTable& buildings, const Simulation* simulation,
                            TileMode tiles, std::span<const std::uint8_t> before) {
    core::ByteWriter out;
    if (tiles == TileMode::Full) {
        // For a list of savegames; a reader takes only the fingerprint (world_from_sections).
        section(
            out, kMeta,
            [&](Writer& o) {
                o.u32(world.width());
                o.u32(world.height());
                o.u64(world.time_ms());
                o.u64(buildings_fingerprint(buildings));
                const PlayerId local = first_human_player(world);
                const Player* p = world.player(local);
                o.string(p != nullptr ? p->name : std::string());
            },
            Need::Optional);
        section(out, kMap, [&](Writer& o) { write_map(o, world, buildings); });
    }
    // The settings: those of the running game, or those the world was loaded with.
    if (simulation != nullptr || world.saved_rules()) {
        const Rules rules = simulation != nullptr ? simulation->rules() : *world.saved_rules();
        section(out, kRules, [&](Writer& o) { write_rules(o, rules); });
    }
    section(out, kWorld, [&](Writer& o) { write_world(o, world); });
    if (tiles == TileMode::Full) {
        section(out, kWorld2, [&](Writer& o) { o.u32(world.game_settings().pirate_interval_s); });
    }
    const std::uint16_t islands_at = islands_version(world);
    section(out, kIslands, [&](Writer& o) { write_islands(o, world, islands_at); }, Need::Required, islands_at);
    if (tiles == TileMode::Differences) {
        section(out, kTiles, [&](Writer& o) { write_tiles(o, world, buildings, before); });
    }
    section(out, kPlayers, [&](Writer& o) { write_players(o, world); });
    section(out, kPlayerState, [&](Writer& o) { write_player_state(o, world); });
    section(out, kMissions, [&](Writer& o) { write_missions(o, world); });
    section(out, kCities, [&](Writer& o) { write_cities(o, world); });
    section(out, kCityState, [&](Writer& o) { write_city_state(o, world); });
    section(out, kScores, [&](Writer& o) { write_scores(o, world); });
    section(out, kProductions, [&](Writer& o) { write_productions(o, world); });
    section(out, kHouses, [&](Writer& o) { write_houses(o, world); });
    section(out, kGrowth, [&](Writer& o) { write_growth(o, world); });
    section(out, kCosts, [&](Writer& o) { write_costs(o, world); });
    if (tiles == TileMode::Differences) {
        section(out, kShips, [&](Writer& o) { write_ships(o, world); }, Need::Optional);  // for readers older than FLEE
    }
    section(out, kAnimals, [&](Writer& o) { write_animals(o, world); });
    section(out, kDisasters, [&](Writer& o) { write_disasters(o, world); });
    const std::uint16_t volcanoes_at = volcanoes_version(world);
    section(out, kVolcanoes, [&](Writer& o) { write_volcanoes(o, world, volcanoes_at); }, Need::Required, volcanoes_at);
    section(out, kFleet, [&](Writer& o) { write_fleet(o, world); });
    section(out, kEpochs, [&](Writer& o) { write_epochs(o, world); });
    section(out, kArmy, [&](Writer& o) { write_army(o, world); });
    section(out, kDiplomacy, [&](Writer& o) { write_diplomacy(o, world); });
    section(out, kExplored, [&](Writer& o) { write_explored(o, world); });
    section(out, kQueues, [&](Writer& o) { write_queues(o, world); });
    section(out, kStances, [&](Writer& o) { write_stances(o, world); });
    if (tiles == TileMode::Full && world.source() != nullptr) {
        section(
            out, kOrigin,
            [&](Writer& o) {
                assets::ChunkWriter chunks;
                for (const assets::Chunk& c : world.source()->chunks()) {
                    if (c.name != "INSELHAUS") {
                        chunks.add(c.name, world.source()->payload(c));
                    }
                }
                o.out().bytes(chunks.bytes());
            },
            Need::Optional);  // only for the export as .GAM
    }
    if (simulation != nullptr) {
        section(out, kSimulation, [&](Writer& o) { simulation->save_state(o.out()); });
    } else if (!world.simulation_state().empty()) {
        // A world loaded but not yet run keeps the figures it was saved with.
        section(out, kSimulation, [&](Writer& o) { o.out().bytes(world.simulation_state()); });
    }
    return out.take();
}

core::Result<World> world_from_sections(std::span<const std::uint8_t> sections, const assets::BuildingTable& buildings,
                                        std::vector<std::string>* warnings) {
    std::optional<core::Result<World>> world;
    bool meta_ok = true;
    const auto status = for_each_section(sections, warnings, [&](std::uint32_t id, std::uint16_t, Body body) {
        core::ByteReader sr(body);
        StateIn in(sr);
        if (id == kMeta) {
            in.u32();
            in.u32();
            in.u64();
            const std::uint64_t fingerprint = in.u64();
            meta_ok = in.ok() && fingerprint == buildings_fingerprint(buildings);
        } else if (id == kMap && !world) {
            Reader reader(in);
            world = read_map(reader, buildings);
        }
        return true;
    });
    if (!status) {
        return status.error();
    }
    if (!world) {
        return make_error(ErrorCode::InvalidFormat, "save: no map");
    }
    if (*world && !meta_ok && warnings != nullptr) {
        warnings->push_back("the save was made with other game data (haeuser.cod); buildings are matched by id");
    }
    return std::move(*world);
}

core::Status apply_sections(World& world, const assets::BuildingTable& buildings,
                            std::span<const std::uint8_t> sections, std::vector<std::string>* warnings) {
    Decoded d;
    bool damaged = false;
    const auto status = for_each_section(sections, warnings, [&](std::uint32_t id, std::uint16_t version, Body body) {
        core::ByteReader sr(body);
        StateIn state(sr);
        Reader in(state);
        switch (id) {
            case kRules:
                d.rules = read_rules(in);
                break;
            case kWorld:
                d.world = read_world(in);
                break;
            case kWorld2: {
                std::uint32_t interval = 0;
                in.u32(interval);
                d.pirate_interval_s = interval;
                break;
            }
            case kIslands:
                d.islands = read_islands(in, version);
                break;
            case kTiles:
                d.tiles = read_tiles(in, buildings);
                break;
            case kPlayers:
                d.players = read_players(in);
                break;
            case kPlayerState:
                d.player_state = read_player_state(in);
                break;
            case kMissions:
                d.missions = read_missions(in, world.islands().size());
                break;
            case kCities:
                d.cities = read_cities(in);
                break;
            case kCityState:
                d.city_state = read_city_state(in);
                break;
            case kScores:
                d.scores = read_scores(in);
                break;
            case kProductions:
                d.productions = read_productions(in);
                break;
            case kHouses:
                d.houses = read_houses(in);
                break;
            case kGrowth:
                d.growth = read_growth(in);
                break;
            case kCosts:
                d.costs = read_costs(in);
                break;
            case kShips:
                d.ships = read_ships(in);
                break;
            case kFleet:
                d.fleet = read_fleet(in);
                break;
            case kEpochs:
                d.epochs = read_epochs(in);
                break;
            case kSoldiers:
                d.soldiers = read_soldiers(in);
                break;
            case kArmy:
                d.army = read_army(in);
                break;
            case kDiplomacy:
                d.diplomacy = read_diplomacy(in);
                break;
            case kExplored:
                d.explored = read_explored(in);
                break;
            case kAnimals:
                d.animals = read_animals(in);
                break;
            case kDisasters:
                d.disasters = read_disasters(in);
                break;
            case kVolcanoes:
                d.volcanoes = read_volcanoes(in, version);
                break;
            case kSimulation:
                d.simulation = std::vector<std::uint8_t>(body.begin(), body.end());
                break;
            case kOrigin:
                d.origin.assign(body.begin(), body.end());
                break;
            case kStances:
                read_stances(in, d.ship_stances, d.soldier_stances);
                break;
            case kQueues:
                read_queues(in, d.ship_queues, d.soldier_queues);
                break;
            default:
                return true;  // META, MAPW (read before)
        }
        damaged = damaged || !in.ok();
        return !damaged;
    });
    if (!status) {
        return status;
    }
    return apply_decoded(world, d);
}

}  // namespace o1602::sim::save
