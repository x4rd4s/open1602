// SPDX-License-Identifier: GPL-3.0-or-later
//
// o1602-stress: runs the simulation headless under load and measures it against the tick budget of
// docs/performance.md (6.25 ms per tick at speed 8, mean, p99 and max). Unlike `o1602-tool bench`
// it needs no game data: the worlds are built from a synthetic building and figure table
// (content.hpp), so it runs in CI and in containers. With --data (ANNO1602_DIR) it takes the
// installation's tables and, unless a savegame is given, the island generator, so that the same
// scenarios can be repeated with the real content.
//
// A scenario is a base world (a synthetic archipelago, a generated world or a savegame) plus what
// is put on top of it: cities filled densely, soldiers, ships with trade routes and warships,
// fires, plagues and eruptions. See `o1602-stress --help` and tools/stress/run-on-device.sh.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "content.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/core/file.hpp"
#include "o1602/net/protocol.hpp"
#include "o1602/sim/disasters.hpp"
#include "o1602/sim/new_game.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/volcanoes.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/sim/world_plan.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// windows.h first
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#else
#include <sys/resource.h>
#endif

// ---- Allocation counting (D4: allocations per tick). Single-threaded tool: plain counters. ----

namespace {
std::uint64_t g_allocations = 0;
std::uint64_t g_allocated_bytes = 0;
}  // namespace

// GCC inlines these replacements into the standard containers and then sees malloc paired with
// `delete`; the pairing is correct because both sides are replaced here.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
void* operator new(std::size_t size) {
    ++g_allocations;
    g_allocated_bytes += size;
    if (void* p = std::malloc(size == 0 ? 1 : size)) {
        return p;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) {
    return ::operator new(size);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace o1602::stress {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kBudgetUs = 6250.0;  // a tick at speed 8 (docs/performance.md)

double ms_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

/// CPU time of this thread in microseconds: unlike the wall clock it does not count the time
/// the thread was not running (a shared or busy machine), so it shows the game's own spikes.
double thread_cpu_us() {
#if defined(_WIN32)
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user) == 0) {
        return 0.0;
    }
    const auto ticks = [](const FILETIME& f) {
        return (static_cast<std::uint64_t>(f.dwHighDateTime) << 32U) | f.dwLowDateTime;
    };
    return static_cast<double>(ticks(kernel) + ticks(user)) / 10.0;  // 100 ns units
#else
    timespec ts{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) {
        return 0.0;
    }
    return static_cast<double>(ts.tv_sec) * 1e6 + static_cast<double>(ts.tv_nsec) / 1000.0;
#endif
}

// ---- Memory ----

struct Memory {
    double rss_mb = 0.0;
    double peak_mb = 0.0;
};

Memory memory() {
    Memory m;
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters) != 0) {
        m.rss_mb = static_cast<double>(counters.WorkingSetSize) / 1048576.0;
        m.peak_mb = static_cast<double>(counters.PeakWorkingSetSize) / 1048576.0;
    }
#elif defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) ==
        KERN_SUCCESS) {
        m.rss_mb = static_cast<double>(info.resident_size) / 1048576.0;
    }
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        m.peak_mb = static_cast<double>(usage.ru_maxrss) / 1048576.0;
    }
#else
    if (std::FILE* f = std::fopen("/proc/self/status", "r")) {
        char line[256];
        while (std::fgets(line, sizeof line, f) != nullptr) {
            unsigned long kb = 0;
            if (std::sscanf(line, "VmRSS: %lu kB", &kb) == 1) {
                m.rss_mb = static_cast<double>(kb) / 1024.0;
            } else if (std::sscanf(line, "VmHWM: %lu kB", &kb) == 1) {
                m.peak_mb = static_cast<double>(kb) / 1024.0;
            }
        }
        std::fclose(f);
    }
#endif
    return m;
}

// ---- Statistics ----

/// Log histogram of microseconds (1 % buckets from 0.1 us to 100 s): quantiles of runs too long
/// to keep every tick.
class Histogram {
public:
    void add(double us) {
        ++m_count;
        m_sum += us;
        m_max = std::max(m_max, us);
        const double v = std::max(us, 0.1);
        const auto b = static_cast<std::size_t>(std::log(v / 0.1) / std::log(1.01));
        ++m_buckets[std::min(b, m_buckets.size() - 1)];
    }
    [[nodiscard]] std::uint64_t count() const noexcept { return m_count; }
    [[nodiscard]] double mean() const noexcept { return m_count == 0 ? 0.0 : m_sum / static_cast<double>(m_count); }
    [[nodiscard]] double max() const noexcept { return m_max; }
    [[nodiscard]] double sum() const noexcept { return m_sum; }
    [[nodiscard]] double quantile(double q) const {
        if (m_count == 0) {
            return 0.0;
        }
        const auto want = static_cast<std::uint64_t>(std::ceil(q * static_cast<double>(m_count)));
        std::uint64_t seen = 0;
        for (std::size_t b = 0; b < m_buckets.size(); ++b) {
            seen += m_buckets[b];
            if (seen >= want) {
                return std::min(m_max, 0.1 * std::pow(1.01, static_cast<double>(b + 1)));
            }
        }
        return m_max;
    }

private:
    std::array<std::uint64_t, 2100> m_buckets{};
    std::uint64_t m_count = 0;
    double m_sum = 0.0;
    double m_max = 0.0;
};

double quantile(std::vector<double>& v, double q) {
    if (v.empty()) {
        return 0.0;
    }
    std::sort(v.begin(), v.end());
    const auto i = static_cast<std::size_t>(q * static_cast<double>(v.size()));
    return v[std::min(i, v.size() - 1)];
}

// ---- Arguments ----

struct Args {
    std::map<std::string, std::string, std::less<>> values;
    std::vector<std::string> flags;

    [[nodiscard]] bool flag(std::string_view name) const {
        return std::find(flags.begin(), flags.end(), name) != flags.end();
    }
    [[nodiscard]] std::optional<std::string> value(std::string_view name) const {
        const auto it = values.find(name);
        return it == values.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
    [[nodiscard]] std::uint64_t number(std::string_view name, std::uint64_t fallback) const {
        const auto v = value(name);
        return v ? std::strtoull(v->c_str(), nullptr, 10) : fallback;
    }
    [[nodiscard]] double real(std::string_view name, double fallback) const {
        const auto v = value(name);
        return v ? std::strtod(v->c_str(), nullptr) : fallback;
    }
};

constexpr std::array<std::string_view, 10> kFlags = {"--data",     "--humans",     "--battle",    "--volcanoes",
                                                     "--no-drain", "--reignite",   "--eruptions", "--keep-rules",
                                                     "--no-save",  "--start-ships"};

int usage() {
    std::fputs(
        "usage: o1602-stress [base] [load] [run]\n"
        "base (default: a synthetic archipelago, no game data needed):\n"
        "  --world WxH           world size in tiles (default 500x350; synthetic up to 4096, generator up to 2000)\n"
        "  --players N           players with a start, all computers unless --humans (default 8)\n"
        "  --island-side S       synthetic: side of the square islands (default 40)\n"
        "  --island-gap G        synthetic: sea between islands (default 12)\n"
        "  --islands N           synthetic: at most N islands (default: as many as fit)\n"
        "  --volcanoes           synthetic: a volcano mountain on every island\n"
        "  --cities K            found K cities per island by build orders (default 1, 0 = none)\n"
        "  --start-ships         one trading ship per player at sea (what the computers settle with)\n"
        "  --data                the installation's tables (ANNO1602_DIR); without --load the island generator\n"
        "  --seed N              generator seed (default 7)\n"
        "  --load FILE           a savegame (Open1602's .o1602 or the original's .gam/.szs) as the base\n"
        "load put on the base:\n"
        "  --dense N             fill every city with up to N buildings (roads, houses, markets, workshops...)\n"
        "  --soldiers N          N land units (swordsmen and musketeers), all players at war\n"
        "  --battle              ... all on the largest island (one battle; synthetic: island sized to fit)\n"
        "  --ships N             N ships: --traders F of them on trade routes (default 0.5), the rest warships\n"
        "                        in hostile groups\n"
        "  --fires N --plagues N fires and plagues on N houses each at the start (--reignite: every minute again)\n"
        "  --eruptions           every crater erupts at the start and again every minute\n"
        "  --keep-rules          keep the rules' unit and ship limits (default: unlimited for the stress)\n"
        "run:\n"
        "  --minutes M           game minutes (default 60); --period P minutes per report line (default 10)\n"
        "  --hash-every N        state_hash() every N ticks inside the timed tick (multiplayer: 100 x speed)\n"
        "  --no-drain            leave the events lying (checks the event buffers stay bounded)\n"
        "  --csv FILE            one line per period (time series for soak runs)\n"
        "  --out FILE            save the end state; --no-save skips the save/load/snapshot measurement\n",
        stderr);
    return 2;
}

std::optional<Args> parse(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--help" || a == "-h") {
            return std::nullopt;
        }
        if (std::find(kFlags.begin(), kFlags.end(), a) != kFlags.end()) {
            args.flags.emplace_back(a);
        } else if (a.starts_with("--") && i + 1 < argc) {
            args.values[std::string(a)] = argv[++i];
        } else {
            std::fprintf(stderr, "unknown argument %s\n", argv[i]);
            return std::nullopt;
        }
    }
    return args;
}

std::pair<std::uint32_t, std::uint32_t> size_of(const Args& args, std::uint32_t w, std::uint32_t h) {
    if (const auto v = args.value("--world")) {
        unsigned a = 0;
        unsigned b = 0;
        if (std::sscanf(v->c_str(), "%ux%u", &a, &b) == 2) {
            return {a, b};
        }
    }
    return {w, h};
}

int fail(const std::string& what) {
    std::fprintf(stderr, "error: %s\n", what.c_str());
    return 1;
}

// ---- Counting what is in the world ----

struct Counts {
    std::size_t islands = 0, cities = 0, houses = 0, productions = 0, ships = 0, soldiers = 0, walkers = 0,
                disasters = 0, lava = 0, eruptions = 0;
    std::uint64_t inhabitants = 0;
};

Counts count(const sim::World& world, const sim::Simulation& simulation) {
    Counts c;
    c.islands = world.islands().size();
    c.cities = world.cities().size();
    c.houses = world.houses().size();
    c.productions = world.productions().size();
    c.ships = world.ships().size();
    c.soldiers = world.soldiers().size();
    c.walkers = simulation.logistics().walkers().size();
    c.disasters = world.disasters().size();
    c.lava = world.volcanoes().lava.size();
    c.eruptions = world.volcanoes().eruptions.size();
    for (const sim::City& city : world.cities()) {
        for (const std::uint32_t n : city.inhabitants) {
            c.inhabitants += n;
        }
    }
    return c;
}

// ---- Putting load on the world ----

/// Places `n` land units: all on the largest island (battle) or on every island in hostile pairs.
std::size_t add_soldiers(sim::World& world, const Tables& tables, std::size_t n, bool battle,
                         const std::vector<sim::PlayerId>& players, std::string* problem) {
    if (players.size() < 2 || world.islands().empty()) {
        *problem = "soldiers need two players and an island";
        return 0;
    }
    std::vector<std::uint16_t> islands;
    if (battle) {
        std::uint16_t best = 0;
        for (std::size_t i = 1; i < world.islands().size(); ++i) {
            const auto& m = world.islands()[i].map;
            const auto& b = world.islands()[best].map;
            if (std::uint64_t{m.width()} * m.height() > std::uint64_t{b.width()} * b.height()) {
                best = static_cast<std::uint16_t>(i);
            }
        }
        islands.push_back(best);
    } else {
        for (std::size_t i = 0; i < world.islands().size(); ++i) {
            islands.push_back(static_cast<std::uint16_t>(i));
        }
    }
    // Free land per island, then the units spread evenly: on each island two players (hostile),
    // the first in the western half of the free tiles, the second in the eastern.
    std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> land(islands.size());
    std::size_t total_land = 0;
    for (std::size_t k = 0; k < islands.size(); ++k) {
        land[k] = free_land(world, tables, islands[k]);
        std::sort(land[k].begin(), land[k].end());  // by x: west first
        total_land += land[k].size();
    }
    if (total_land == 0) {
        *problem = "no free land";
        return 0;
    }
    std::size_t placed = 0;
    for (std::size_t k = 0; k < islands.size() && placed < n; ++k) {
        const auto& tiles = land[k];
        // Share of this island, by its free land.
        std::size_t want =
            k + 1 == islands.size()
                ? n - placed
                : static_cast<std::size_t>(std::llround(static_cast<double>(n) * static_cast<double>(tiles.size()) /
                                                        static_cast<double>(total_land)));
        want = std::min({want, n - placed, tiles.size()});
        const sim::PlayerId a = players[(2 * k) % players.size()];
        const sim::PlayerId b = players[(2 * k + 1) % players.size()];
        // Every unit on its own tile; the two sides from the two ends of the island towards the middle.
        for (std::size_t u = 0; u < want; ++u) {
            const bool west = u % 2 == 0;
            const std::size_t at = west ? u / 2 : tiles.size() - 1 - u / 2;
            const auto [x, y] = tiles[at];
            sim::Soldier s = sim::make_soldier(x, y, u % 3 == 2 ? kMusket : kSword, west ? a : b, islands[k]);
            if (const assets::FigureType* type = tables.figures.get(s.figure)) {
                s.health = type->combat.max_health;  // as a castle trains it
            }
            world.add_soldier(std::move(s));
            ++placed;
        }
    }
    if (placed < n) {
        *problem = "only " + std::to_string(placed) + " of " + std::to_string(n) +
                   " units found a free tile (one per tile); a larger --island-side or --world helps";
    }
    return placed;
}

/// Open sea tiles (no island cell), on a grid of `step` tiles.
std::vector<std::pair<std::uint32_t, std::uint32_t>> sea_tiles(const sim::World& world, std::uint32_t step) {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> out;
    for (std::uint32_t y = 4; y + 4 < world.height(); y += step) {
        for (std::uint32_t x = 4; x + 4 < world.width(); x += step) {
            bool free = true;
            for (std::uint32_t dy = 0; dy < 3 && free; ++dy) {
                for (std::uint32_t dx = 0; dx < 3 && free; ++dx) {
                    free = world.cell(x + dx - 1, y + dy - 1).island == sim::World::kSea;
                }
            }
            if (free) {
                out.emplace_back(x, y);
            }
        }
    }
    return out;
}

struct ShipLoad {
    std::size_t traders = 0;
    std::size_t routes = 0;
    std::size_t warships = 0;
};

/// `n` ships: a share on trade routes between two cities of their owner, the rest warships in
/// groups of hostile owners spread over the sea.
ShipLoad add_ships(sim::Simulation& simulation, sim::World& world, const Tables& tables, std::size_t n,
                   double trader_share, const std::vector<sim::PlayerId>& players, std::string* problem) {
    ShipLoad load;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> sea = sea_tiles(world, 2);
    if (sea.empty() || players.empty()) {
        *problem = "no open sea";
        return load;
    }
    // Spread the ships evenly over the sea tiles: index i * stride.
    const double stride = std::max(1.0, static_cast<double>(sea.size()) / static_cast<double>(n));
    // Cities by owner, for the routes.
    std::map<sim::PlayerId, std::vector<const sim::City*>> cities;
    for (const sim::City& c : world.cities()) {
        if (c.warehouse_x) {
            cities[c.owner].push_back(&c);
        }
    }
    const auto traders = static_cast<std::size_t>(std::llround(static_cast<double>(n) * trader_share));
    std::size_t placed = 0;
    std::vector<std::pair<std::uint32_t, sim::PlayerId>> route_ships;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t at = std::min(sea.size() - 1, static_cast<std::size_t>(static_cast<double>(i) * stride));
        if (i > 0 && static_cast<std::size_t>(static_cast<double>(i - 1) * stride) == at && stride <= 1.0 &&
            i >= sea.size()) {
            break;  // more ships than sea tiles
        }
        const auto [x, y] = sea[at];
        const bool trader = i < traders;
        // Warships: neighbours belong to different players (groups of hostile owners).
        const sim::PlayerId owner = players[i % players.size()];
        sim::Ship& s = add_ship(world, tables, x, y, trader ? kTradeShip : kWarship, owner);
        if (!trader) {
            s.cannons = 8;
            ++load.warships;
        } else {
            ++load.traders;
            route_ships.emplace_back(s.id, owner);
        }
        ++placed;
    }
    if (placed < n) {
        *problem = "only " + std::to_string(placed) + " of " + std::to_string(n) + " ships found open sea";
    }
    // Routes: wood from one city of the owner to another and back with cloth.
    using Op = sim::RouteEdit::Op;
    for (const auto& [id, owner] : route_ships) {
        const auto it = cities.find(owner);
        if (it == cities.end() || it->second.size() < 2) {
            continue;
        }
        const auto& list = it->second;
        const sim::City* from = list[id % list.size()];
        const sim::City* to = list[(id + 1 + id / list.size()) % list.size()];
        if (from == to) {
            to = list[(id + 1) % list.size()];
        }
        bool ok =
            simulation
                .execute(sim::RouteEdit{owner, id, Op::SetStop, 0, 0, sim::Target::city_of(from->island, from->index)})
                .accepted;
        ok = ok && simulation
                       .execute(sim::RouteEdit{
                           owner, id, Op::SetEntry, 0, 0, {}, sim::RouteEntry{assets::ware::kWood, false, 20 * 32}})
                       .accepted;
        ok = ok && simulation
                       .execute(sim::RouteEdit{
                           owner, id, Op::SetEntry, 0, 1, {}, sim::RouteEntry{assets::ware::kCloth, true, 50 * 32}})
                       .accepted;
        ok = ok &&
             simulation
                 .execute(sim::RouteEdit{owner, id, Op::SetStop, 1, 0, sim::Target::city_of(to->island, to->index)})
                 .accepted;
        ok = ok && simulation
                       .execute(sim::RouteEdit{
                           owner, id, Op::SetEntry, 1, 0, {}, sim::RouteEntry{assets::ware::kWood, true, 50 * 32}})
                       .accepted;
        ok = ok && simulation
                       .execute(sim::RouteEdit{
                           owner, id, Op::SetEntry, 1, 1, {}, sim::RouteEntry{assets::ware::kCloth, false, 20 * 32}})
                       .accepted;
        ok = ok && simulation.execute(sim::RouteEdit{owner, id, Op::Start}).accepted;
        load.routes += ok ? 1U : 0U;
    }
    return load;
}

/// Starts fires or plagues on up to `n` houses, spread evenly over the house list.
std::size_t start_disasters(sim::World& world, const Tables& tables, std::size_t n, sim::Disaster::Kind kind,
                            std::size_t offset) {
    if (n == 0 || world.houses().empty()) {
        return 0;
    }
    std::size_t started = 0;
    const std::size_t houses = world.houses().size();
    const std::size_t step = std::max<std::size_t>(1, houses / n);
    for (std::size_t i = offset % step; i < houses && started < n; i += step) {
        const sim::House h = world.houses()[i];
        if (sim::disaster_at(world, h.island, h.x, h.y) != nullptr) {
            continue;
        }
        started +=
            sim::start_disaster(world, tables.buildings, h.island, sim::TilePos{h.x, h.y}, kind, 12, nullptr) ? 1U : 0U;
    }
    return started;
}

std::size_t erupt_all(sim::World& world, std::uint16_t seed) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < world.islands().size(); ++i) {
        for (const sim::Crater& c : world.islands()[i].craters) {
            const sim::CraterRef ref{static_cast<std::uint16_t>(i), c.x, c.y};
            bool running = false;
            for (const sim::Eruption& e : world.volcanoes().eruptions) {
                running = running || e.crater == ref;
            }
            if (!running && sim::erupt(world, ref, static_cast<std::uint16_t>(seed + i), false, nullptr)) {
                ++n;
            }
        }
    }
    return n;
}

void print_counts(const char* label, const Counts& c) {
    std::printf(
        "%s islands %zu, cities %zu, houses %zu, inhabitants %llu, productions %zu, ships %zu, soldiers %zu, "
        "walkers %zu, disasters %zu, eruptions %zu, lava %zu\n",
        label, c.islands, c.cities, c.houses, static_cast<unsigned long long>(c.inhabitants), c.productions, c.ships,
        c.soldiers, c.walkers, c.disasters, c.eruptions, c.lava);
}

int run(const Args& args) {
    const auto setup_start = Clock::now();
    const bool data = args.flag("--data");
    const std::size_t players = std::max<std::size_t>(args.number("--players", 8), 1);
    std::vector<std::string> warnings;

    // ---- Tables ----
    std::optional<assets::GameDirectory> game;
    Tables tables;
    if (data) {
        auto g = assets::GameDirectory::from_environment();
        if (!g) {
            return fail("--data needs ANNO1602_DIR: " + g.error().message);
        }
        game = std::move(g).value();
        auto buildings = assets::BuildingTable::load(*game);
        if (!buildings) {
            return fail(buildings.error().message);
        }
        auto figures = assets::FigureTable::load(*game);
        if (!figures) {
            return fail(figures.error().message);
        }
        tables = Tables{std::move(buildings).value(), std::move(figures).value(), false};
    } else {
        auto t = synthetic_tables();
        if (!t) {
            return fail("synthetic tables: " + t.error().message);
        }
        tables = std::move(t).value();
    }

    // ---- Base world ----
    std::optional<sim::World> world;
    std::string base;
    if (const auto file = args.value("--load")) {
        auto bytes = core::read_file(std::filesystem::path(*file));
        if (!bytes) {
            return fail(bytes.error().message);
        }
        if (sim::is_open1602_save(*bytes)) {
            auto w = sim::read_game(*bytes, tables.buildings, &warnings, &tables.figures);
            if (!w) {
                return fail(w.error().message);
            }
            world = std::move(w).value();
        } else {
            if (!game) {
                return fail("an original file needs --data");
            }
            auto f = assets::ChunkFile::from_bytes(*bytes);
            if (!f) {
                return fail(f.error().message);
            }
            auto w = sim::load_world(*f, tables.buildings, *game, &warnings);
            if (!w) {
                return fail(w.error().message);
            }
            world = std::move(w).value();
        }
        base = "savegame " + *file;
    } else if (data) {
        auto texts = assets::TextTable::load(*game);
        if (!texts) {
            return fail(texts.error().message);
        }
        sim::NewGameOptions options;
        options.name = "Stress";
        options.difficulty = 1;
        options.world.seed = args.number("--seed", 7);
        options.world.guarantee_resources = true;
        sim::WorldSettings settings =
            sim::settings_for_players(static_cast<std::uint8_t>(std::min<std::size_t>(players, 255)));
        const auto [w, h] = size_of(args, settings.width, settings.height);
        settings.width = w;
        settings.height = h;
        options.custom = settings;
        auto made = sim::start_new_game(*game, tables.buildings, *texts, options, &warnings);
        if (!made) {
            return fail("generator: " + made.error().message);
        }
        world = std::move(made).value();
        base = "generated world (seed " + std::to_string(options.world.seed) + ")";
    } else {
        const auto [w, h] = size_of(args, 500, 350);
        if (w == 0 || h == 0 || w > sim::World::kMaxSide || h > sim::World::kMaxSide) {
            return fail("world size beyond World::kMaxSide (" + std::to_string(sim::World::kMaxSide) + ")");
        }
        world.emplace(w, h);
        world->set_players(make_players(players, args.flag("--humans")));
        Archipelago a;
        a.width = w;
        a.height = h;
        a.side = static_cast<std::uint32_t>(args.number("--island-side", 40));
        a.gap = static_cast<std::uint32_t>(args.number("--island-gap", 12));
        a.max_islands = args.number("--islands", 0);
        a.volcanoes = args.flag("--volcanoes");
        if (args.flag("--battle")) {
            // One island large enough for every unit on a tile of its own, plus room.
            const auto units = static_cast<double>(args.number("--soldiers", 0));
            const auto side = static_cast<std::uint32_t>(std::ceil(std::sqrt(units * 1.35))) + 12;
            a.side = std::max(a.side, side);
            if (a.side + a.gap > std::min(w, h)) {
                return fail("the battle island of " + std::to_string(a.side) + " tiles does not fit into the world");
            }
        }
        if (auto st = build_archipelago(*world, tables, a); !st) {
            return fail("archipelago: " + st.error().message);
        }
        base = "synthetic archipelago (islands " + std::to_string(a.side) + "x" + std::to_string(a.side) + ", gap " +
               std::to_string(a.gap) + ")";
    }
    for (const auto& text : warnings) {
        std::fprintf(stderr, "warning: %s\n", text.c_str());
    }

    sim::Rules rules = sim::Rules::open1602();
    if (!args.flag("--keep-rules")) {
        rules.max_army = 0;
        rules.max_ships_per_player = 0;
    }
    sim::Simulation simulation(*world, tables.buildings, tables.figures, rules);
    std::vector<sim::PlayerId> starters = start_players(*world);
    // Every player to the computer (as bench), unless --humans.
    std::size_t handed = 0;
    if (!args.flag("--humans")) {
        for (const sim::PlayerId p : starters) {
            const sim::Player* player = world->player(p);
            if (player != nullptr && player->role == sim::PlayerRole::Human) {
                handed += simulation.execute(sim::PlayerControl{p, true}).accepted ? 1U : 0U;
            }
        }
    }
    std::printf("base: %s, world %ux%u, %zu islands, %zu players with a start (%zu handed to the computer)\n",
                base.c_str(), world->width(), world->height(), world->islands().size(), starters.size(), handed);

    // ---- Load ----
    std::string problem;
    const std::size_t per_island = args.number("--cities", data || args.value("--load") ? 0 : 1);
    if (per_island > 0 && !starters.empty()) {
        if (!tables.synthetic) {
            std::printf("note: --cities founds cities on the synthetic coast only; ignored with --data\n");
        } else {
            std::string why;
            const std::size_t founded = found_cities(simulation, *world, tables, starters, per_island, &why);
            std::printf("cities founded: %zu%s%s\n", founded, founded == 0 ? ", refused: " : "",
                        founded == 0 ? why.c_str() : "");
        }
    }
    top_up(*world, 200 * 32);
    if (const std::size_t dense = args.number("--dense", 0); dense > 0) {
        const auto t = Clock::now();
        const std::size_t placed = fill_cities(simulation, *world, tables, starters, dense);
        std::printf("dense: %zu buildings placed in %.0f ms\n", placed, ms_since(t));
        // Let the houses fill up before measuring the late game: the setup is not timed.
        top_up(*world, 200 * 32);
    }
    if (args.flag("--start-ships")) {
        const auto sea = sea_tiles(*world, 7);
        for (std::size_t i = 0; i < starters.size() && !sea.empty(); ++i) {
            add_ship(*world, tables, sea[(i * sea.size()) / starters.size()].first,
                     sea[(i * sea.size()) / starters.size()].second, kTradeShip, starters[i]);
        }
    }
    const std::size_t soldiers = args.number("--soldiers", 0);
    const std::size_t ships = args.number("--ships", 0);
    if (soldiers > 0 || ships > 0) {
        declare_war(*world, starters);
    }
    if (soldiers > 0) {
        problem.clear();
        const auto t = Clock::now();
        const std::size_t placed = add_soldiers(*world, tables, soldiers, args.flag("--battle"), starters, &problem);
        std::printf("soldiers: %zu placed (%s) in %.0f ms%s%s\n", placed,
                    args.flag("--battle") ? "one battle" : "spread", ms_since(t),
                    problem.empty() ? "" : "; LIMIT: ", problem.c_str());
    }
    if (ships > 0) {
        problem.clear();
        const auto t = Clock::now();
        const ShipLoad l =
            add_ships(simulation, *world, tables, ships, args.real("--traders", 0.5), starters, &problem);
        std::printf("ships: %zu traders (%zu on routes), %zu warships in %.0f ms%s%s\n", l.traders, l.routes,
                    l.warships, ms_since(t), problem.empty() ? "" : "; LIMIT: ", problem.c_str());
    }
    const std::size_t fires = args.number("--fires", 0);
    const std::size_t plagues = args.number("--plagues", 0);
    const auto ignite = [&] {
        const std::size_t f = start_disasters(*world, tables, fires, sim::Disaster::Kind::Fire, 0);
        const std::size_t p = start_disasters(*world, tables, plagues, sim::Disaster::Kind::Plague,
                                              world->houses().size() / std::max<std::size_t>(1, 2 * plagues));
        return std::pair{f, p};
    };
    if (fires > 0 || plagues > 0) {
        const auto [f, p] = ignite();
        std::printf("disasters: %zu fires, %zu plagues started\n", f, p);
    }
    if (args.flag("--eruptions")) {
        std::printf("eruptions: %zu craters erupt (%u craters counted by the world)\n", erupt_all(*world, 1),
                    sim::crater_count(*world));
    }
    print_counts("start:", count(*world, simulation));
    const Memory setup_memory = memory();
    std::printf("setup %.1f s, memory %.0f MB (peak %.0f MB)\n", ms_since(setup_start) / 1000.0, setup_memory.rss_mb,
                setup_memory.peak_mb);

    // ---- Run ----
    const std::size_t minutes = std::max<std::size_t>(args.number("--minutes", 60), 1);
    const std::size_t period = std::clamp<std::size_t>(args.number("--period", 10), 1, minutes);
    const std::uint64_t hash_every = args.number("--hash-every", 0);
    const bool drain = !args.flag("--no-drain");
    std::FILE* csv = nullptr;
    if (const auto path = args.value("--csv")) {
        csv = std::fopen(path->c_str(), "w");
        if (csv == nullptr) {
            return fail("cannot write " + *path);
        }
        std::fprintf(csv,
                     "minute,mean_us,p50_us,p99_us,max_us,over_budget,rss_mb,peak_mb,allocs_per_tick,"
                     "alloc_bytes_per_tick,houses,inhabitants,productions,ships,soldiers,walkers,disasters,lava,"
                     "cities,next_ship_id,next_soldier_id,pending_events\n");
    }
    std::printf("(us per tick of %u ms game time; budget %.0f us at speed 8)\n", sim::Simulation::kTickMs, kBudgetUs);
    std::printf(
        "minute    mean     p50     p99     max >budget   RSS MB  allocs/t  houses   inhab.  ships soldiers "
        "walkers disasters\n");

    const std::vector<std::string_view> steps = sim::Simulation::tick_step_names();
    std::vector<std::uint64_t> step_ns(steps.size(), 0);
    std::vector<Histogram> step_hist(steps.size());
    Histogram all;
    Histogram cpu;  // the same ticks in thread CPU time
    std::uint64_t cpu_over = 0;
    Histogram hash_hist;
    std::vector<double> period_ticks;
    const std::uint64_t ticks_per_minute = 60'000 / sim::Simulation::kTickMs;
    period_ticks.reserve(period * ticks_per_minute);
    std::uint64_t over_budget = 0;
    std::uint64_t period_over = 0;
    std::uint64_t period_allocs = 0;
    std::uint64_t period_alloc_bytes = 0;
    std::uint64_t ticks = 0;
    std::size_t pending = 0;
    std::uint64_t max_allocs_tick = 0;
    for (std::size_t minute = 1; minute <= minutes; ++minute) {
        for (std::uint64_t t = 0; t < ticks_per_minute; ++t) {
            std::fill(step_ns.begin(), step_ns.end(), 0);
            const std::uint64_t allocs = g_allocations;
            const std::uint64_t bytes = g_allocated_bytes;
            const double cpu_start = thread_cpu_us();
            const auto start = Clock::now();
            simulation.tick_timed(step_ns);
            double hash_us = 0.0;
            if (hash_every != 0 && ++ticks % hash_every == 0) {
                const auto h = Clock::now();
                (void)simulation.state_hash();
                hash_us = ms_since(h) * 1000.0;
                hash_hist.add(hash_us);
            }
            const double us = ms_since(start) * 1000.0;
            const double cpu_us = thread_cpu_us() - cpu_start;
            cpu.add(cpu_us);
            if (cpu_us > kBudgetUs) {
                ++cpu_over;
            }
            const std::uint64_t a = g_allocations - allocs;
            max_allocs_tick = std::max(max_allocs_tick, a);
            period_allocs += a;
            period_alloc_bytes += g_allocated_bytes - bytes;
            if (drain) {
                (void)simulation.take_production_events();
                (void)simulation.take_construction_events();
                (void)simulation.take_military_events();
                (void)simulation.take_city_events();
                (void)simulation.take_ship_events();
                (void)simulation.take_diplomacy_notices();
                (void)simulation.take_command_results();
                (void)simulation.take_house_events();
                (void)simulation.take_island_events();
                (void)simulation.take_disaster_events();
                (void)simulation.take_volcano_events();
                (void)simulation.take_game_events();
            }
            all.add(us);
            period_ticks.push_back(us);
            if (us > kBudgetUs) {
                ++over_budget;
                ++period_over;
            }
            for (std::size_t i = 0; i < steps.size(); ++i) {
                step_hist[i].add(static_cast<double>(step_ns[i]) / 1000.0);
            }
        }
        // Keep the load up: goods (the throwaway approach), fires, eruptions.
        if (args.value("--dense")) {
            top_up(*world, 200 * 32);
        }
        if (args.flag("--reignite") && (fires > 0 || plagues > 0)) {
            (void)ignite();
        }
        if (args.flag("--eruptions")) {
            (void)erupt_all(*world, static_cast<std::uint16_t>(minute));
        }
        if (minute % period == 0 || minute == minutes) {
            const std::size_t n = period_ticks.size();
            double sum = 0.0;
            for (const double v : period_ticks) {
                sum += v;
            }
            const double mean = sum / static_cast<double>(n);
            const double p50 = quantile(period_ticks, 0.5);
            const double p99 = quantile(period_ticks, 0.99);
            const double mx = period_ticks.back();
            const Memory m = memory();
            const Counts c = count(*world, simulation);
            if (!drain) {
                // What the buffers hold: take them once to see, then they refill.
                pending = simulation.take_production_events().size() + simulation.take_military_events().size() +
                          simulation.take_ship_events().size() + simulation.take_construction_events().size() +
                          simulation.take_city_events().size();
            }
            std::printf("%6zu %7.1f %7.0f %7.0f %7.0f %7llu %8.0f %9.0f %7zu %8llu %6zu %8zu %7zu %9zu\n", minute, mean,
                        p50, p99, mx, static_cast<unsigned long long>(period_over), m.rss_mb,
                        static_cast<double>(period_allocs) / static_cast<double>(n), c.houses,
                        static_cast<unsigned long long>(c.inhabitants), c.ships, c.soldiers, c.walkers, c.disasters);
            std::fflush(stdout);
            if (csv != nullptr) {
                std::fprintf(
                    csv,
                    "%zu,%.2f,%.1f,%.1f,%.1f,%llu,%.1f,%.1f,%.1f,%.0f,%zu,%llu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%u,%u,%zu\n",
                    minute, mean, p50, p99, mx, static_cast<unsigned long long>(period_over), m.rss_mb, m.peak_mb,
                    static_cast<double>(period_allocs) / static_cast<double>(n),
                    static_cast<double>(period_alloc_bytes) / static_cast<double>(n), c.houses,
                    static_cast<unsigned long long>(c.inhabitants), c.productions, c.ships, c.soldiers, c.walkers,
                    c.disasters, c.lava, c.cities, world->next_ship_id(), world->next_soldier_id(), pending);
                std::fflush(csv);
            }
            period_ticks.clear();
            period_over = 0;
            period_allocs = 0;
            period_alloc_bytes = 0;
        }
    }
    if (csv != nullptr) {
        std::fclose(csv);
    }

    // ---- Summary ----
    const double cnt = static_cast<double>(all.count());
    std::printf(
        "\nSUMMARY ticks %llu: mean %.1f us, p50 %.0f, p99 %.0f, p99.9 %.0f, max %.0f us; over budget %llu "
        "(%.3f%%); at speed 8 one core is %.1f%% busy\n",
        static_cast<unsigned long long>(all.count()), all.mean(), all.quantile(0.5), all.quantile(0.99),
        all.quantile(0.999), all.max(), static_cast<unsigned long long>(over_budget),
        100.0 * static_cast<double>(over_budget) / cnt, all.mean() * 160.0 / 10'000.0);
    std::printf(
        "CPU     ticks %llu: mean %.1f us, p50 %.0f, p99 %.0f, p99.9 %.0f, max %.0f us; over budget %llu "
        "(thread CPU time, without the time the machine ran something else)\n",
        static_cast<unsigned long long>(cpu.count()), cpu.mean(), cpu.quantile(0.5), cpu.quantile(0.99),
        cpu.quantile(0.999), cpu.max(), static_cast<unsigned long long>(cpu_over));
    const auto verdict = [](const Histogram& h) {
        return std::string("mean ") + (h.mean() <= kBudgetUs ? "ok" : "OVER") + ", p99 " +
               (h.quantile(0.99) <= kBudgetUs ? "ok" : "OVER") + ", max " + (h.max() <= kBudgetUs ? "ok" : "OVER");
    };
    std::printf("VERDICT wall: %s; cpu: %s (budget %.0f us)\n", verdict(all).c_str(), verdict(cpu).c_str(), kBudgetUs);
    std::printf("allocations: %.1f per tick on average, at most %llu in one tick\n",
                static_cast<double>(g_allocations) / std::max(1.0, cnt),
                static_cast<unsigned long long>(max_allocs_tick));
    std::printf("step           share     mean      p99      max\n");
    for (std::size_t i = 0; i < steps.size(); ++i) {
        std::printf("%-13.*s %5.1f%% %8.1f %8.0f %8.0f\n", static_cast<int>(steps[i].size()), steps[i].data(),
                    all.sum() > 0.0 ? 100.0 * step_hist[i].sum() / all.sum() : 0.0, step_hist[i].mean(),
                    step_hist[i].quantile(0.99), step_hist[i].max());
    }
    if (hash_hist.count() > 0) {
        std::printf("state hash in the loop: %llu times, mean %.0f us, max %.0f us\n",
                    static_cast<unsigned long long>(hash_hist.count()), hash_hist.mean(), hash_hist.max());
    }
    print_counts("end:", count(*world, simulation));
    std::printf("ids: next ship %u, next soldier %u\n", world->next_ship_id(), world->next_soldier_id());
    {
        // Health 0 means "full" to a loader (ships of old files): a ship or unit that is going
        // down with health 0 comes back whole from a savegame or a resync snapshot.
        std::size_t sinking = 0;
        std::size_t ships_zero = 0;
        std::size_t dying = 0;
        std::size_t soldiers_zero = 0;
        for (const sim::Ship& s : world->ships()) {
            sinking += s.sinking ? 1U : 0U;
            ships_zero += s.health == 0 ? 1U : 0U;
        }
        for (const sim::Soldier& s : world->soldiers()) {
            dying += s.dying() ? 1U : 0U;
            soldiers_zero += s.health == 0 ? 1U : 0U;
        }
        std::printf("going down at the end: ships sinking %zu (health 0: %zu), units dying %zu (health 0: %zu)\n",
                    sinking, ships_zero, dying, soldiers_zero);
    }

    if (!args.flag("--no-save")) {
        auto t = Clock::now();
        const std::uint64_t hash = simulation.state_hash();
        const double hash_ms = ms_since(t);
        t = Clock::now();
        auto save = sim::write_game(*world, tables.buildings, &simulation);
        const double save_ms = ms_since(t);
        if (!save) {
            std::printf("SAVE FAILED: %s\n", save.error().message.c_str());
        } else {
            const auto info = sim::save_info(*save);
            t = Clock::now();
            std::vector<std::string> load_warnings;
            auto loaded = sim::read_game(*save, tables.buildings, &load_warnings, &tables.figures);
            const double load_ms = ms_since(t);
            std::printf(
                "state hash %.1f ms; savegame %zu bytes (%.2f MiB, sections %.2f MiB unpacked) written in "
                "%.1f ms, read in %.1f ms\n",
                hash_ms, save->size(), static_cast<double>(save->size()) / 1048576.0,
                info ? static_cast<double>(info->size) / 1048576.0 : 0.0, save_ms, load_ms);
            std::printf("resync snapshot: %.2f MiB of kMaxSnapshotBytes %.0f MiB (%.1f%%)%s\n",
                        static_cast<double>(save->size()) / 1048576.0,
                        static_cast<double>(net::kMaxSnapshotBytes) / 1048576.0,
                        100.0 * static_cast<double>(save->size()) / static_cast<double>(net::kMaxSnapshotBytes),
                        save->size() > net::kMaxSnapshotBytes ? " -- TOO LARGE: nobody can join or resync" : "");
            if (!loaded) {
                std::printf("ROUND TRIP FAILED: %s\n", loaded.error().message.c_str());
            } else {
                sim::Simulation again(*loaded, tables.buildings, tables.figures, rules);
                const Counts a = count(*world, simulation);
                const Counts b = count(*loaded, again);
                const std::uint64_t hash2 = again.state_hash();
                std::printf(
                    "round trip: islands %zu/%zu, cities %zu/%zu, houses %zu/%zu, ships %zu/%zu, soldiers "
                    "%zu/%zu, state hash %s%s\n",
                    a.islands, b.islands, a.cities, b.cities, a.houses, b.houses, a.ships, b.ships, a.soldiers,
                    b.soldiers, hash == hash2 ? "equal" : "DIFFERS", again.state_damaged() ? " (state damaged)" : "");
                if (hash != hash2) {
                    // Both games unpacked, for finding what the round trip changed (cmp -l a b).
                    const sim::SaveOptions raw{false};
                    auto a_bytes = sim::write_game(*world, tables.buildings, &simulation, raw);
                    auto b_bytes = sim::write_game(*loaded, tables.buildings, &again, raw);
                    if (a_bytes && b_bytes) {
                        std::size_t at = 0;
                        while (at < a_bytes->size() && at < b_bytes->size() && (*a_bytes)[at] == (*b_bytes)[at]) {
                            ++at;
                        }
                        std::printf("round trip: unpacked saves of %zu and %zu bytes differ first at byte %zu\n",
                                    a_bytes->size(), b_bytes->size(), at);
                        if (const auto dir = args.value("--diff-dir")) {
                            (void)core::write_file(std::filesystem::path(*dir) / "before.o1602", *a_bytes);
                            (void)core::write_file(std::filesystem::path(*dir) / "after.o1602", *b_bytes);
                        }
                    }
                }
                for (const auto& w : load_warnings) {
                    std::printf("load warning: %s\n", w.c_str());
                }
            }
            if (const auto out = args.value("--out")) {
                if (auto st = core::write_file(std::filesystem::path(*out), *save); !st) {
                    return fail(st.error().message);
                }
                std::printf("-> %s\n", out->c_str());
            }
        }
    }
    const Memory m = memory();
    std::printf("memory at the end %.0f MB, peak %.0f MB\n", m.rss_mb, m.peak_mb);
    return 0;
}

}  // namespace
}  // namespace o1602::stress

int main(int argc, char** argv) {
    const auto args = o1602::stress::parse(argc, argv);
    if (!args) {
        return o1602::stress::usage();
    }
    return o1602::stress::run(*args);
}
