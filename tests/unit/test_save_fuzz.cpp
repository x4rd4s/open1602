// SPDX-License-Identifier: GPL-3.0-or-later
// Open1602's own savegame against damaged and foreign data (docs/formats/o1602-save.md): mutated
// sections with a correct checksum, so that every change reaches the section readers, either load
// nothing or a world that holds together and runs; sections of a later version are reported, and
// the required ones refuse the file.
//
// O1602_FUZZ_ITERATIONS sets the number of mutated files (default 400), O1602_FUZZ_SEED the first
// seed; a failure prints the seed and the mutation that caused it.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/core/compress.hpp"
#include "o1602/sim/random.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/savegame_extension.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/validate.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kRoad = 20401;
constexpr std::uint32_t kHouse = 20501;
constexpr std::size_t kHeader = 32;         ///< file header in front of the sections
constexpr std::size_t kSectionHeader = 12;  ///< tag, version, flags, size

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "  @Nummer: 1\n  Id: 20401\n  Kind: 1\n  Gfx: 1\n  Size: 1, 1\n"
        "  @Nummer: 2\n  Id: 20501\n  Kind: 14\n  Gfx: 10\n  Size: 2, 2\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A small world with something in every list: two islands with roads and a Kontor, players, a
/// city, ships with orders and units.
sim::World make_world(const assets::BuildingTable& buildings) {
    sim::World world(160, 120);
    for (std::uint16_t i = 0; i < 2; ++i) {
        sim::IslandMap map(12, 10);
        for (std::int32_t y = 0; y < 10; ++y) {
            for (std::int32_t x = 0; x < 12; ++x) {
                REQUIRE(map.place(*buildings.by_id(kGround), x, y, 0, 0).ok());
            }
        }
        map.capture_ground();
        REQUIRE(map.build(*buildings.by_id(kRoad), 3, 4, 0, 0, 1, 0).ok());
        REQUIRE(map.build(*buildings.by_id(kHouse), 5, 4, 1, 2, 1, 0).ok());
        assets::IslandHeader header;
        header.number = static_cast<std::uint8_t>(i);
        header.width = 12;
        header.height = 10;
        header.deposits.push_back({2, 7, 8, 0x101, 0, 900});
        REQUIRE(world.add_island(sim::WorldIsland{header, std::move(map), 20U + 80U * i, 40}).ok());
    }
    std::vector<sim::Player> players(4);
    for (std::size_t p = 0; p < players.size(); ++p) {
        players[p].role = p == 0 ? sim::PlayerRole::Human : sim::PlayerRole::Computer;
        players[p].banner = static_cast<sim::BannerId>(p);
        players[p].money = 5000;
        players[p].name = "Player " + std::to_string(p);
    }
    world.set_players(players);
    sim::City city;
    city.island = 0;
    city.index = 0;
    city.owner = 1;
    city.name = "Port";
    city.warehouse_x = 5;
    city.warehouse_y = 4;
    city.goods.assign(30, 64);
    world.add_city(city);
    for (std::uint32_t k = 0; k < 8; ++k) {
        sim::Ship ship;
        ship.x = static_cast<std::uint16_t>(10 + 3 * k);
        ship.y = static_cast<std::uint16_t>(80 + k % 3);
        ship.owner = static_cast<sim::PlayerId>(k % 4);
        ship.name = "Ship " + std::to_string(k);
        ship.cargo.resize(4);
        ship.cargo[0].ware = 23;
        ship.cargo[0].amount = 32 * k;
        if (k == 3) {
            ship.target = sim::Target::position(60, 100);
            ship.queue = {sim::Target::position(70, 100)};
            ship.attack_move = true;
        }
        world.add_ship(ship);
    }
    for (std::uint32_t k = 0; k < 12; ++k) {
        sim::Soldier soldier;
        soldier.pos_x = static_cast<std::int32_t>((20 + k % 10) * 4096);
        soldier.pos_y = 41 * 4096;
        soldier.island = 0;
        soldier.owner = static_cast<sim::PlayerId>(k % 4);
        soldier.figure = 1;
        if (k == 2) {
            soldier.queue = {sim::Target::spot(50, 90)};
            soldier.hold = true;
        }
        world.add_soldier(soldier);
    }
    world.set_time_ms(1'000'000);
    return world;
}

struct Section {
    std::size_t at = 0;  ///< offset of its header in the file
    std::string tag;
    std::uint16_t version = 0;
    std::uint16_t flags = 0;
    std::uint32_t size = 0;
};

std::uint32_t get_u32(const core::Bytes& b, std::size_t at) {
    return static_cast<std::uint32_t>(b[at]) | static_cast<std::uint32_t>(b[at + 1]) << 8U |
           static_cast<std::uint32_t>(b[at + 2]) << 16U | static_cast<std::uint32_t>(b[at + 3]) << 24U;
}
void put_u16(core::Bytes& b, std::size_t at, std::uint16_t v) {
    b[at] = static_cast<std::uint8_t>(v);
    b[at + 1] = static_cast<std::uint8_t>(v >> 8U);
}
void put_u32(core::Bytes& b, std::size_t at, std::uint32_t v) {
    for (std::size_t i = 0; i < 4; ++i) {
        b[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
    }
}

/// The sections of an uncompressed savegame, as far as their headers hold.
std::vector<Section> sections_of(const core::Bytes& file) {
    std::vector<Section> out;
    std::size_t at = kHeader;
    while (at + kSectionHeader <= file.size()) {
        Section s;
        s.at = at;
        s.tag.assign(reinterpret_cast<const char*>(file.data() + at), 4);
        s.version = static_cast<std::uint16_t>(file[at + 4] | file[at + 5] << 8U);
        s.flags = static_cast<std::uint16_t>(file[at + 6] | file[at + 7] << 8U);
        s.size = get_u32(file, at + 8);
        out.push_back(s);
        if (s.size > file.size() - at - kSectionHeader) {
            break;
        }
        at += kSectionHeader + s.size;
    }
    return out;
}

const Section* find(const std::vector<Section>& sections, std::string_view tag) {
    const auto it = std::find_if(sections.begin(), sections.end(), [&](const Section& s) { return s.tag == tag; });
    return it == sections.end() ? nullptr : &*it;
}

/// Makes the header fit the (changed) uncompressed sections again: size and checksum.
void reseal(core::Bytes& file) {
    const auto body = std::span<const std::uint8_t>(file).subspan(kHeader);
    for (std::size_t i = 0; i < 8; ++i) {
        file[16 + i] = static_cast<std::uint8_t>(static_cast<std::uint64_t>(body.size()) >> (8 * i));
    }
    put_u32(file, 24, core::crc32(body));
    put_u32(file, 28, static_cast<std::uint32_t>(body.size()));
}

/// A section of its own: tag, version, flags, size, body.
core::Bytes make_section(std::string_view tag, std::uint16_t version, std::uint16_t flags,
                         const std::vector<std::uint8_t>& body) {
    core::ByteWriter out;
    out.bytes(std::span(reinterpret_cast<const std::uint8_t*>(tag.data()), 4));
    out.u16(version);
    out.u16(flags);
    out.u32(static_cast<std::uint32_t>(body.size()));
    out.bytes(body);
    return out.take();
}

/// The game of the tests, run for a moment and saved uncompressed with the state of the running game.
struct Game {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures;
    sim::World world = make_world(buildings);
    core::Bytes file;

    Game() {
        sim::Simulation simulation(world, buildings, figures);
        for (int t = 0; t < 20; ++t) {
            simulation.tick();
        }
        auto bytes = sim::write_game(world, buildings, &simulation, sim::SaveOptions{false});
        REQUIRE(bytes.ok());
        file = std::move(bytes).value();
    }
};

std::uint32_t env_or(const char* name, std::uint32_t fallback) {
    const char* value = std::getenv(name);
    return value != nullptr ? static_cast<std::uint32_t>(std::strtoul(value, nullptr, 10)) : fallback;
}

/// One random change of the sections; returns what it did, for the report of a failure.
std::string mutate(core::Bytes& file, sim::Random& random) {
    const std::vector<Section> sections = sections_of(file);
    REQUIRE(!sections.empty());
    const Section& s = sections[random.below(static_cast<std::uint32_t>(sections.size()))];
    const std::size_t body = s.at + kSectionHeader;
    // An earlier change may have left a size beyond the end.
    const auto size = static_cast<std::uint32_t>(std::min<std::size_t>(s.size, file.size() - body));
    const auto pick = [&](std::uint32_t n) { return n == 0 ? 0U : random.below(n); };
    switch (random.below(9)) {
        case 0: {  // a bit
            if (size == 0) {
                return "nothing";
            }
            const std::size_t at = body + pick(size);
            const auto bit = static_cast<std::uint8_t>(1U << random.below(8));
            file[at] ^= bit;
            return s.tag + ": bit at +" + std::to_string(at - body);
        }
        case 1: {  // a byte to a value readers have to watch for
            if (size == 0) {
                return "nothing";
            }
            constexpr std::uint8_t kValues[] = {0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF};
            const std::size_t at = body + pick(size);
            file[at] = kValues[random.below(6)];
            return s.tag + ": byte at +" + std::to_string(at - body);
        }
        case 2: {  // a count or index made huge
            if (size < 4) {
                return "nothing";
            }
            constexpr std::uint32_t kValues[] = {0xFFFFFFFFU, 0x7FFFFFFFU, 0x80000000U, 0x00010000U, 0x0000FFFFU};
            const std::size_t at = body + pick(size - 3);
            put_u32(file, at, kValues[random.below(5)]);
            return s.tag + ": u32 at +" + std::to_string(at - body);
        }
        case 3: {  // the section cut short (its size follows)
            if (size == 0) {
                return "nothing";
            }
            const std::uint32_t keep = pick(size);
            file.erase(file.begin() + static_cast<std::ptrdiff_t>(body + keep),
                       file.begin() + static_cast<std::ptrdiff_t>(body + size));
            put_u32(file, s.at + 8, keep);
            return s.tag + ": cut to " + std::to_string(keep);
        }
        case 4: {  // the size in the header wrong
            const std::uint32_t wrong = random.below(2) == 0 ? size + 1 + pick(64) : pick(size + 1);
            put_u32(file, s.at + 8, wrong);
            return s.tag + ": size " + std::to_string(wrong);
        }
        case 5: {  // the section left out
            file.erase(file.begin() + static_cast<std::ptrdiff_t>(s.at),
                       file.begin() + static_cast<std::ptrdiff_t>(body + size));
            return s.tag + ": left out";
        }
        case 6: {  // the section twice
            const core::Bytes copy(file.begin() + static_cast<std::ptrdiff_t>(s.at),
                                   file.begin() + static_cast<std::ptrdiff_t>(body + size));
            file.insert(file.end(), copy.begin(), copy.end());
            return s.tag + ": twice";
        }
        case 7: {  // the section moved to the end
            const core::Bytes copy(file.begin() + static_cast<std::ptrdiff_t>(s.at),
                                   file.begin() + static_cast<std::ptrdiff_t>(body + size));
            file.erase(file.begin() + static_cast<std::ptrdiff_t>(s.at),
                       file.begin() + static_cast<std::ptrdiff_t>(body + size));
            file.insert(file.end(), copy.begin(), copy.end());
            return s.tag + ": moved to the end";
        }
        default: {  // the whole stream cut off
            const std::size_t keep = kHeader + pick(static_cast<std::uint32_t>(file.size() - kHeader));
            file.resize(keep);
            return "cut at " + std::to_string(keep);
        }
    }
}

}  // namespace

TEST_CASE("save fuzz: mutated sections with a correct checksum load nothing or a sound world that runs") {
    Game game;
    const std::uint32_t iterations = env_or("O1602_FUZZ_ITERATIONS", 400);
    const std::uint32_t first_seed = env_or("O1602_FUZZ_SEED", 1);
    std::uint32_t loaded_count = 0;
    for (std::uint32_t i = 0; i < iterations; ++i) {
        const std::uint32_t seed = first_seed + i;
        sim::Random random(seed, 0xF022);
        core::Bytes file = game.file;
        std::string what = mutate(file, random);
        if (random.below(4) == 0) {
            what += ", " + mutate(file, random);
        }
        reseal(file);
        std::vector<std::string> warnings;
        auto loaded = sim::read_game(file, game.buildings, &warnings, &game.figures);
        if (!loaded) {
            continue;
        }
        ++loaded_count;
        const auto fail = [&](const char* expr) {
            std::fprintf(stderr, "  seed %u (%s): %s\n", seed, what.c_str(), expr);
        };
        // What loads holds together, saves again and runs.
        if (!sim::validate(*loaded).ok()) {
            fail("loaded world does not validate");
            CHECK(false);
            continue;
        }
        auto again = sim::write_game(*loaded, game.buildings, nullptr, sim::SaveOptions{false});
        if (!again || !sim::read_game(*again, game.buildings, nullptr, &game.figures).ok()) {
            fail("loaded world does not save and load again");
            CHECK(false);
            continue;
        }
        sim::Simulation simulation(*loaded, game.buildings, game.figures);
        for (int t = 0; t < 10; ++t) {
            simulation.tick();
        }
        CHECK(sim::validate(*loaded).ok());
    }
    // Most single changes are caught, but not all: a changed amount of goods is still a sound game.
    CHECK(loaded_count > 0);
    CHECK(loaded_count < iterations);
}

TEST_CASE("save fuzz: random damage to a packed file never gets past the checks") {
    if (!core::has_zlib()) {
        SKIP("no zlib");
    }
    Game game;
    sim::World world = game.world;
    auto packed = sim::write_game(world, game.buildings);
    REQUIRE(packed.ok());
    const auto unpack = [](const core::Bytes& file) {
        const auto size = static_cast<std::size_t>(get_u32(file, 16));
        return core::zlib_inflate(std::span<const std::uint8_t>(file).subspan(kHeader), size);
    };
    const auto sections = unpack(*packed);
    REQUIRE(sections.ok());
    sim::Random random(7, 0xF022);
    std::uint32_t refused = 0;
    for (std::uint32_t i = 0; i < 200; ++i) {
        core::Bytes file = *packed;
        const std::uint32_t n = 1 + random.below(4);
        for (std::uint32_t k = 0; k < n; ++k) {
            const auto value = static_cast<std::uint8_t>(1 + random.below(255));
            file[kHeader + random.below(static_cast<std::uint32_t>(file.size() - kHeader))] ^= value;
        }
        if (!sim::read_game(file, game.buildings, nullptr, &game.figures).ok()) {
            ++refused;
            continue;
        }
        // Deflate has bits that do not change what it unpacks to: a match that reaches back to an
        // equal run, the padding after the last block. Which bytes those are depends on the zlib
        // that packed the file (zlib and zlib-ng pack differently). Such damage changed nothing,
        // so loading it is right; anything else must have been refused.
        const auto unpacked = unpack(file);
        if (!unpacked.ok() || *unpacked != *sections) {
            std::fprintf(stderr, "  damage %u got past the checks\n", i);
            CHECK(false);
        }
    }
    CHECK(refused > 190);
}

TEST_CASE("save fuzz: the writer marks the sections the game cannot do without as required") {
    Game game;
    const std::vector<Section> sections = sections_of(game.file);
    REQUIRE(find(sections, "MAPW") != nullptr);
    for (const Section& s : sections) {
        CHECK(s.version == 1);
        const bool optional = s.tag == "META" || s.tag == "ORIG";
        CHECK((s.flags == 0) == optional);
    }
    CHECK(find(sections, "SIMU") != nullptr);
    CHECK(find(sections, "SIMU")->flags == 1);
}

TEST_CASE("save fuzz: a section of a later version is skipped and reported, a required one refuses the file") {
    Game game;
    const std::vector<Section> sections = sections_of(game.file);
    const auto load = [&](const core::Bytes& file, std::vector<std::string>* warnings) {
        return sim::read_game(file, game.buildings, warnings, &game.figures);
    };
    const auto mentions = [](const std::vector<std::string>& warnings, std::string_view text) {
        return std::any_of(warnings.begin(), warnings.end(),
                           [&](const std::string& w) { return w.find(text) != std::string::npos; });
    };

    // An unknown optional section: loaded, reported.
    core::Bytes extra = game.file;
    const core::Bytes xtra = make_section("XTRA", 1, 0, {1, 2, 3, 4});
    extra.insert(extra.end(), xtra.begin(), xtra.end());
    reseal(extra);
    std::vector<std::string> warnings;
    auto loaded = load(extra, &warnings);
    REQUIRE(loaded.ok());
    CHECK(mentions(warnings, "XTRA"));
    CHECK(loaded->ships().size() == game.world.ships().size());

    // The same section required: nothing loaded, the message names it.
    core::Bytes required = game.file;
    const core::Bytes xtra_required = make_section("XTRA", 1, 1, {1, 2, 3, 4});
    required.insert(required.end(), xtra_required.begin(), xtra_required.end());
    reseal(required);
    auto refused = load(required, nullptr);
    REQUIRE(!refused.ok());
    CHECK(refused.error().code == core::ErrorCode::Unsupported);
    CHECK(refused.error().message.find("XTRA") != std::string::npos);

    // A known section in a later version that is not required: skipped, the rest loads.
    const Section* stances = find(sections, "STNC");
    REQUIRE(stances != nullptr);
    core::Bytes later = game.file;
    put_u16(later, stances->at + 4, 2);
    put_u16(later, stances->at + 6, 0);
    reseal(later);
    warnings.clear();
    loaded = load(later, &warnings);
    REQUIRE(loaded.ok());
    CHECK(mentions(warnings, "STNC (version 2)"));
    CHECK(game.world.ships()[3].attack_move);
    CHECK(!loaded->ships()[3].attack_move);  // the stances stayed behind

    // The same, required (as the writer marks it): refused.
    put_u16(later, stances->at + 6, 1);
    reseal(later);
    refused = load(later, nullptr);
    REQUIRE(!refused.ok());
    CHECK(refused.error().code == core::ErrorCode::Unsupported);
    CHECK(refused.error().message.find("STNC") != std::string::npos);

    // Files written before the flags existed have 0 there: they load as before, without warnings.
    core::Bytes old = game.file;
    for (const Section& s : sections) {
        put_u16(old, s.at + 6, 0);
    }
    reseal(old);
    warnings.clear();
    loaded = load(old, &warnings);
    REQUIRE(loaded.ok());
    CHECK(warnings.empty());
    CHECK(loaded->ships()[3].attack_move);
}

TEST_CASE("save fuzz: a section that does not match the size of the one it completes refuses the file") {
    Game game;
    const std::vector<Section> sections = sections_of(game.file);
    // Replaces one section by a body that names no entries (a count of 0).
    const auto emptied = [&](std::string_view tag) {
        const Section* s = find(sections, tag);
        REQUIRE(s != nullptr);
        core::Bytes file(game.file.begin(), game.file.begin() + static_cast<std::ptrdiff_t>(s->at));
        const core::Bytes empty = make_section(tag, s->version, s->flags, {0, 0, 0, 0});
        file.insert(file.end(), empty.begin(), empty.end());
        file.insert(file.end(), game.file.begin() + static_cast<std::ptrdiff_t>(s->at + kSectionHeader + s->size),
                    game.file.end());
        reseal(file);
        return file;
    };
    REQUIRE(sim::read_game(game.file, game.buildings, nullptr, &game.figures).ok());
    // Diplomacy of no players for four: once it loaded as peace between everyone.
    auto refused = sim::read_game(emptied("DIPL"), game.buildings, nullptr, &game.figures);
    REQUIRE(!refused.ok());
    CHECK(refused.error().message.find("player sections do not match") != std::string::npos);
    refused = sim::read_game(emptied("XPLR"), game.buildings, nullptr, &game.figures);
    REQUIRE(!refused.ok());
    CHECK(refused.error().message.find("explored islands do not match") != std::string::npos);
}

TEST_CASE("save fuzz: the chunk in a .GAM refuses a required section of a later version, unchanged world") {
    Game game;
    const std::vector<std::uint8_t> before = {'s', 'o', 'm', 'e', ' ', 'c', 'h', 'u', 'n', 'k', 's'};
    const core::Bytes payload = sim::encode_extension(game.world, game.buildings, nullptr, before);
    sim::World target = make_world(game.buildings);
    std::vector<std::string> warnings;
    REQUIRE(sim::apply_extension(target, game.buildings, payload, before, &warnings).ok());
    CHECK(warnings.empty());

    core::Bytes optional = payload;
    const core::Bytes xtra = make_section("XTRA", 3, 0, {9});
    optional.insert(optional.end(), xtra.begin(), xtra.end());
    REQUIRE(sim::apply_extension(target, game.buildings, optional, before, &warnings).ok());
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("XTRA (version 3)") != std::string::npos);

    core::Bytes required = payload;
    const core::Bytes xtra_required = make_section("XTRA", 3, 1, {9});
    required.insert(required.end(), xtra_required.begin(), xtra_required.end());
    sim::World untouched = make_world(game.buildings);
    untouched.ships()[0].name = "Untouched";
    const auto status = sim::apply_extension(untouched, game.buildings, required, before);
    REQUIRE(!status.ok());
    CHECK(status.error().code == core::ErrorCode::Unsupported);
    CHECK(untouched.ships()[0].name == "Untouched");
}
