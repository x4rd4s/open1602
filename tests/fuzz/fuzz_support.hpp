// SPDX-License-Identifier: GPL-3.0-or-later
// Shared pieces of the libFuzzer targets (tests/fuzz): a small building table, a small world with
// something in every list (as tests/unit/test_save_fuzz.cpp builds it), and checks that abort, so
// that libFuzzer records a broken invariant as a crash with its input.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/core/compress.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::fuzz {

/// Aborts with the message: libFuzzer then keeps the input as a crash.
[[noreturn]] inline void fail(const char* what) {
    std::fprintf(stderr, "fuzz invariant broken: %s\n", what);
    std::abort();
}

inline void require(bool ok, const char* what) {
    if (!ok) {
        fail(what);
    }
}

inline std::string_view as_text(std::span<const std::uint8_t> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// Building ids of the small table.
inline constexpr std::uint32_t kGround = 20101;
inline constexpr std::uint32_t kRoad = 20401;
inline constexpr std::uint32_t kHouse = 20501;
inline constexpr std::uint32_t kKontor = 20601;

/// Ground, a road, a 2x2 house and a Kontor.
inline const assets::BuildingTable& buildings() {
    static const assets::BuildingTable table = [] {
        const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
        const auto doc = assets::cod::parse(
            "Objekt: HAUS\n"
            "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
            "  @Nummer: 1\n  Id: 20401\n  Kind: 1\n  Gfx: 1\n  Size: 1, 1\n"
            "  @Nummer: 2\n  Id: 20501\n  Kind: 14\n  Gfx: 10\n  Size: 2, 2\n"
            "  @Nummer: 3\n  Id: 20601\n  Kind: 2\n  Gfx: 20\n  Size: 3, 3\n"
            "EndObj\n",
            types);
        auto t = assets::BuildingTable::from_cod(doc);
        require(t.ok(), "the fuzz building table loads");
        return std::move(t).value();
    }();
    return table;
}

inline const assets::FigureTable& figures() {
    static const assets::FigureTable table;
    return table;
}

/// A small world with something in every list: two islands with roads and houses, four players, a
/// city, ships with orders and soldiers.
inline sim::World make_world() {
    const assets::BuildingTable& b = buildings();
    sim::World world(160, 120);
    for (std::uint16_t i = 0; i < 2; ++i) {
        sim::IslandMap map(12, 10);
        for (std::int32_t y = 0; y < 10; ++y) {
            for (std::int32_t x = 0; x < 12; ++x) {
                require(map.place(*b.by_id(kGround), x, y, 0, 0).ok(), "ground");
            }
        }
        map.capture_ground();
        require(map.build(*b.by_id(kRoad), 3, 4, 0, 0, 1, 0).ok(), "road");
        require(map.build(*b.by_id(kHouse), 5, 4, 1, 2, 1, 0).ok(), "house");
        assets::IslandHeader header;
        header.number = static_cast<std::uint8_t>(i);
        header.width = 12;
        header.height = 10;
        header.deposits.push_back({2, 7, 8, 0x101, 0, 900});
        require(world.add_island(sim::WorldIsland{header, std::move(map), 20U + 80U * i, 40}).ok(), "island");
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

/// Open1602 savegame header (docs/formats/o1602-save.md).
inline constexpr std::size_t kSaveHeader = 32;

/// Makes the header of an uncompressed .o1602 file fit its sections again (sizes and CRC-32), so
/// that mutations reach the section readers instead of failing at the checksum. Compressed files
/// are left as they are.
inline void reseal_save(std::vector<std::uint8_t>& file) {
    if (file.size() < kSaveHeader || (file[10] & 1U) != 0) {
        return;
    }
    const auto body = std::span<const std::uint8_t>(file).subspan(kSaveHeader);
    for (std::size_t i = 0; i < 8; ++i) {
        file[16 + i] = static_cast<std::uint8_t>(static_cast<std::uint64_t>(body.size()) >> (8 * i));
    }
    const std::uint32_t crc = core::crc32(body);
    const auto size = static_cast<std::uint32_t>(body.size());
    for (std::size_t i = 0; i < 4; ++i) {
        file[24 + i] = static_cast<std::uint8_t>(crc >> (8 * i));
        file[28 + i] = static_cast<std::uint8_t>(size >> (8 * i));
    }
}

}  // namespace o1602::fuzz
