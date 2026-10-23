// SPDX-License-Identifier: GPL-3.0-or-later

#include "content.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>

#include "o1602/assets/cod.hpp"
#include "o1602/assets/game_data.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/diplomacy.hpp"
#include "o1602/sim/faction.hpp"
#include "o1602/sim/island_map.hpp"
#include "o1602/sim/volcanoes.hpp"

namespace o1602::stress {

namespace {

// Building records. Gfx values keep the sprites of the records apart (a record covers
// size_x * size_y sprites), so that by_sprite() finds every one of them.
constexpr const char* kBuildingsCod =
    "Nahrung: 1.3\n"
    "Objekt: BGRUPPE\n"
    "  Nummer: 0\n  Prozent: 0\n  Maxwohn: 2\n  Steuer: 1.4\n"
    "  Nummer: 1\n  Prozent: 47\n  Maxwohn: 6\n  Steuer: 1.6\n"
    "  Nummer: 2\n  Prozent: 60\n  Maxwohn: 15\n  Steuer: 1.8\n"
    "EndObj\n"
    "Objekt: HAUS\n"
    "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n  Posoffs: 20\n  Wegspeed: 145, 120, 170, 100\n"
    "  @Nummer: 1\n  Id: 21201\n  Kind: 19\n  Gfx: 1\n  Size: 1, 1\n"
    "  @Nummer: 2\n  Id: 20501\n  Kind: 23\n  Gfx: 2\n  Size: 1, 1\n  Posoffs: 20\n  Destroyflg: 1\n"
    "  @Nummer: 3\n  Id: 20202\n  Kind: 10\n  Gfx: 3\n  Size: 1, 1\n  Posoffs: 20\n  Wegspeed: 145, 120, 170, 100\n"
    "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 53\n  EndObj\n"
    // Roads: stone and dirt, each a family of straight, curve, T and cross.
    "  @Nummer: 4\n  Id: 20401\n  Kind: 1\n  Gfx: 4\n  Size: 1, 1\n  KreuzBase: 4\n  Posoffs: 20\n"
    "  Wegspeed: 100, 100, 100, 100\n"
    "  Objekt: HAUS_BAUKOST\n    Money: 5\n  EndObj\n"
    "  @Nummer: 5\n  Id: 20402\n  Kind: 1\n  Gfx: 5\n  Size: 1, 1\n  KreuzBase: 4\n  Posoffs: 20\n"
    "  @Nummer: 6\n  Id: 20403\n  Kind: 1\n  Gfx: 6\n  Size: 1, 1\n  KreuzBase: 4\n  Posoffs: 20\n"
    "  @Nummer: 7\n  Id: 20404\n  Kind: 1\n  Gfx: 7\n  Size: 1, 1\n  KreuzBase: 4\n  Posoffs: 20\n"
    "  @Nummer: 8\n  Id: 20411\n  Kind: 1\n  Gfx: 8\n  Size: 1, 1\n  KreuzBase: 8\n  Posoffs: 20\n"
    "  Objekt: HAUS_BAUKOST\n    Money: 2\n  EndObj\n"
    "  @Nummer: 9\n  Id: 20412\n  Kind: 1\n  Gfx: 9\n  Size: 1, 1\n  KreuzBase: 8\n  Posoffs: 20\n"
    "  @Nummer: 10\n  Id: 20413\n  Kind: 1\n  Gfx: 10\n  Size: 1, 1\n  KreuzBase: 8\n  Posoffs: 20\n"
    "  @Nummer: 11\n  Id: 20414\n  Kind: 1\n  Gfx: 11\n  Size: 1, 1\n  KreuzBase: 8\n  Posoffs: 20\n"
    // Kontor I at the beach; a large radius so that a city covers its whole island.
    "  @Nummer: 12\n  Id: 22101\n  Kind: 35\n  Gfx: 20\n  Size: 2, 3\n  Strandflg: 1\n  PlaceFlg: 1\n  Posoffs: 20\n"
    "  KreuzBase: 12\n  Ausbauflg: 1\n  Maxenergy: 20\n  Ruinenr: 0\n"
    "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 40\n    Maxlager: 30\n  EndObj\n"
    "  Objekt: HAUS_BAUKOST\n    Money: 100\n  EndObj\n"
    // Houses of three classes.
    "  @Nummer: 13\n  Id: 20605\n  Kind: 14\n  Gfx: 30\n  Size: 2, 2\n  Ruinenr: 0\n  Posoffs: 20\n  Maxenergy: 20\n"
    "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 0\n    Radius: 6\n  EndObj\n"
    "  Objekt: HAUS_BAUKOST\n    Money: 10\n  EndObj\n"
    "  @Nummer: 14\n  Id: 20606\n  Kind: 14\n  Gfx: 34\n  Size: 2, 2\n  Ruinenr: 0\n  Posoffs: 20\n  Maxenergy: 20\n"
    "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 1\n    Radius: 6\n  EndObj\n"
    "  Objekt: HAUS_BAUKOST\n    Money: 10\n  EndObj\n"
    "  @Nummer: 15\n  Id: 20640\n  Kind: 14\n  Gfx: 38\n  Size: 2, 2\n  Ruinenr: 0\n  Posoffs: 20\n  Maxenergy: 20\n"
    "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 2\n    Radius: 6\n  EndObj\n"
    "  Objekt: HAUS_BAUKOST\n    Money: 10\n  EndObj\n"
    // A workshop (Webstube: wool to cloth) with a carrier.
    "  @Nummer: 16\n  Id: 20507\n  Kind: 14\n  Gfx: 42\n  Size: 2, 2\n  Posoffs: 20\n  Maxenergy: 30\n"
    "  Wegspeed: 120, 120, 120, 100\n"
    "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Ware: 19\n    Rohstoff: 4\n    Rohmenge: 1\n"
    "    Prodmenge: 1\n    Interval: 20\n    Figurnr: 80\n    Radius: 8\n  EndObj\n"
    "  Objekt: HAUS_BAUKOST\n    Money: 50\n  EndObj\n"
    // Market with carts.
    "  @Nummer: 17\n  Id: 20809\n  Kind: 14\n  Gfx: 46\n  Size: 4, 3\n  Posoffs: 20\n  Maxenergy: 40\n"
    "  Objekt: HAUS_PRODTYP\n    Kind: 7\n    Radius: 16\n    Figurnr: 81\n    Maxlager: 30\n  EndObj\n"
    "  Objekt: HAUS_BAUKOST\n    Money: 100\n  EndObj\n"
    // Doctor and fire brigade.
    "  @Nummer: 18\n  Id: 20817\n  Kind: 14\n  Gfx: 60\n  Size: 2, 2\n  Posoffs: 20\n  Maxenergy: 30\n"
    "  Objekt: HAUS_PRODTYP\n    Kind: 22\n    Radius: 16\n    Figurnr: 84\n  EndObj\n"
    "  Objekt: HAUS_BAUKOST\n    Money: 100\n  EndObj\n"
    "  @Nummer: 19\n  Id: 20811\n  Kind: 14\n  Gfx: 64\n  Size: 2, 2\n  Posoffs: 20\n  Maxenergy: 30\n"
    "  Objekt: HAUS_PRODTYP\n    Kind: 26\n    Radius: 15\n    Figurnr: 85\n  EndObj\n"
    "  Objekt: HAUS_BAUKOST\n    Money: 100\n  EndObj\n"
    // Ruin, tower, volcano mountain.
    "  @Nummer: 20\n  Id: 22601\n  Kind: 12\n  Gfx: 70\n  Size: 1, 1\n  Posoffs: 20\n"
    "  @Nummer: 21\n  Id: 21405\n  Kind: 6\n  Gfx: 72\n  Size: 1, 1\n  Posoffs: 20\n  Maxenergy: 55\n"
    "  Objekt: HAUS_PRODTYP\n    Kind: 16\n    Figurnr: 38\n  EndObj\n"
    "  Objekt: HAUS_BAUKOST\n    Money: 100\n  EndObj\n"
    "  @Nummer: 22\n  Id: 21108\n  Kind: 15\n  Gfx: 80\n  Size: 4, 4\n  Posoffs: 20\n"
    // Meadow: what demolished forest becomes (construction.cpp kMeadowId). Without it a demolition on
    // former forest leaves the tile as it was, and the computers loop on it.
    "  @Nummer: 23\n  Id: 22722\n  Kind: 11\n  Gfx: 96\n  Size: 1, 1\n  Posoffs: 20\n  Wegspeed: 145, 120, 170, 100\n"
    "EndObj\n";

// Figures: two land units, two ships, the tower gun, carrier, cart, doctor, fireman, volcano and lava.
constexpr const char* kFiguresCod =
    "Objekt: FIGUR\n"
    "  Nummer: 1\n  Kind: 1\n  Gfx: 0\n  Blocknr: 1\n  Speed: 260\n  Shotradius: 0.75\n  Maxstepcnt: 1\n"
    "  Rohstoff: 11\n  Rohmenge: 3\n  Worktime: 0.8\n  Shottime: 0.6\n  Maxenergy: 20\n  Hitpoint: 1.0\n"
    "  Nummer: 9\n  Kind: 3\n  Gfx: 0\n  Blocknr: 1\n  Speed: 210\n  Shotradius: 4\n  Maxstepcnt: 2\n"
    "  Worktime: 2.0\n  Shottime: 0.7\n  Maxenergy: 15\n  Hitpoint: 2.4\n"
    "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 4\n  Maxkanon: 6\n  Speed: 500\n"
    "  Drehtime: 0.13\n  Maxenergy: 50\n  Hitpoint: 2.0\n  Shotradius: 7\n  Shottime: 1.0\n  Worktime: 5.0\n"
    "  Stirbtime: 3.0\n  Preis: 1000\n  Rohstoff: 23\n  Rohmenge: 25\n"
    "  Nummer: 25\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 3\n  Maxkanon: 8\n  Speed: 550\n"
    "  Drehtime: 0.13\n  Maxenergy: 65\n  Hitpoint: 2.0\n  Shotradius: 7\n  Shottime: 1.0\n  Worktime: 5.0\n"
    "  Stirbtime: 4.0\n"
    "  Nummer: 38\n  Gfx: 0\n  Blocknr: 1\n  Shotradius: 7.5\n  Worktime: 3.0\n  Shottime: 0.6\n"
    "  Maxenergy: 24\n  Hitpoint: 4.0\n"
    "  Nummer: 80\n  Blocknr: 3\n  Speed: 220\n  Rotate: 8\n  Gfx: 0\n  Maxtrag: 6\n  Worktime: 3\n"
    "  Nummer: 81\n  Blocknr: 3\n  Speed: 220\n  Rotate: 8\n  Gfx: 0\n  Maxtrag: 6\n  Worktime: 3\n"
    "  Nummer: 84\n  Blocknr: 3\n  Speed: 220\n  Rotate: 8\n  Gfx: 0\n  Maxtrag: 6\n  Worktime: 3\n"
    "  Nummer: 85\n  Blocknr: 3\n  Speed: 300\n  Rotate: 8\n  Gfx: 64\n  Maxtrag: 4\n  Worktime: 3\n"
    "  Nummer: 68\n  Blocknr: 3\n  Gfx: 0\n"
    "  Nummer: 69\n  Blocknr: 3\n  Gfx: 0\n  Speed: 600\n"
    "EndObj\n";

bool walkable_kind(std::int32_t kind) {
    return kind == assets::kind::kGround || kind == assets::kind::kForest;
}

}  // namespace

core::Result<Tables> synthetic_tables() {
    const assets::CodPredefinedSymbol house_types[] = {{"HAUS", 1}, {"BGRUPPE", 5}, {"BGRUPPE_WARE", 6}};
    auto buildings = assets::BuildingTable::from_cod(assets::cod::parse(kBuildingsCod, house_types));
    if (!buildings) {
        return buildings.error();
    }
    const assets::CodPredefinedSymbol figure_types[] = {{"FIGUR", 1}};
    auto figures = assets::FigureTable::from_cod(assets::cod::parse(kFiguresCod, figure_types));
    if (!figures) {
        return figures.error();
    }
    return Tables{std::move(buildings).value(), std::move(figures).value(), true};
}

std::vector<sim::Player> make_players(std::size_t players, bool humans) {
    const std::size_t slots = players <= sim::kOriginalPlayers ? 7 : sim::player_slot(players - 1) + 1U;
    std::vector<sim::Player> out(slots);
    for (std::size_t n = 0; n < players; ++n) {
        sim::Player& p = out[sim::player_slot(n)];
        p.role = humans ? sim::PlayerRole::Human : sim::PlayerRole::Computer;
        p.money = 1'000'000;
        p.banner = static_cast<sim::BannerId>(n < sim::kOriginalPlayers ? n : sim::kFirstExtraBanner + n - 4);
        p.name = "P" + std::to_string(n + 1);
    }
    sim::make_neutral(out[4], sim::faction::kTrader);
    sim::make_neutral(out[5], sim::faction::kPirates);
    sim::make_neutral(out[6], sim::faction::kNatives);
    return out;
}

std::vector<sim::PlayerId> start_players(const sim::World& world) {
    std::vector<sim::PlayerId> out;
    for (std::size_t n = 0; n < sim::kMaxPlayerCount; ++n) {
        const sim::PlayerId id = sim::player_slot(n);
        const sim::Player* p = world.player(id);
        if (p == nullptr) {
            break;
        }
        if (p->role == sim::PlayerRole::Human || p->role == sim::PlayerRole::Computer) {
            out.push_back(id);
        }
    }
    return out;
}

core::Status build_archipelago(sim::World& world, const Tables& tables, const Archipelago& a) {
    const assets::BuildingTable& b = tables.buildings;
    const assets::BuildingType* ground = b.by_id(kGround);
    const assets::BuildingType* sea = b.by_id(kSea);
    const assets::BuildingType* beach = b.by_id(kBeach);
    const assets::BuildingType* tree = b.by_id(kTree);
    if (ground == nullptr || sea == nullptr || beach == nullptr || tree == nullptr) {
        return core::make_error(core::ErrorCode::Unsupported, "the archipelago needs the synthetic terrain records");
    }
    const std::uint32_t s = a.side;
    // One template, copied for every island.
    sim::IslandMap map(s, s);
    for (std::uint32_t y = 0; y < s; ++y) {
        for (std::uint32_t x = 0; x < s; ++x) {
            const assets::BuildingType* t = ground;
            std::uint8_t rotation = 0;
            if (x + 4 > s - 1) {
                t = sea;
            } else if (x + 4 == s - 1) {
                t = beach;
                rotation = 3;
            } else if (x >= s / 2 && x < s / 2 + std::max<std::uint32_t>(s / 6, 2) && y > 6) {
                t = tree;
            }
            if (auto st = map.place(*t, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), rotation, 0); !st) {
                return st;
            }
        }
    }
    if (a.volcanoes && b.by_id(kVolcano) != nullptr && s >= 16) {
        if (auto st = map.place(*b.by_id(kVolcano), 1, 1, 0, 0); !st) {
            return st;
        }
    }
    map.capture_ground();
    const std::uint32_t pitch = s + a.gap;
    std::size_t count = 0;
    for (std::uint32_t y = a.gap / 2; y + s + a.gap / 2 <= world.height(); y += pitch) {
        for (std::uint32_t x = a.gap / 2; x + s + a.gap / 2 <= world.width(); x += pitch) {
            if (a.max_islands != 0 && count >= a.max_islands) {
                return core::Ok{};
            }
            sim::WorldIsland island{assets::IslandHeader{}, map, x, y};
            // The header keeps the original's 8-bit fields; the map has the real size.
            island.header.number = static_cast<std::uint8_t>(world.islands().size());
            island.header.width = static_cast<std::uint8_t>(std::min<std::uint32_t>(s, 255));
            island.header.height = static_cast<std::uint8_t>(std::min<std::uint32_t>(s, 255));
            island.header.fertility = 0xFFFFFFFFU;
            if (a.volcanoes) {
                island.craters = sim::find_craters(island.map, b);
            }
            if (auto st = world.add_island(std::move(island)); !st) {
                return st;
            }
            ++count;
        }
    }
    return core::Ok{};
}

sim::Ship& add_ship(sim::World& world, const Tables& tables, std::uint32_t x, std::uint32_t y, std::uint16_t figure,
                    sim::PlayerId owner) {
    sim::Ship s = sim::make_ship("S", x, y, figure, owner, 2);
    const assets::FigureType* type = tables.figures.get(figure);
    s.cargo.resize(type != nullptr && type->ship.cargo_slots > 0 ? type->ship.cargo_slots : 4);
    // As a shipyard makes it: full health (0 would mean "full" only to a Fleet made after it).
    s.health = type != nullptr ? type->ship.max_health : 0;
    world.add_ship(std::move(s));
    return world.ships().back();
}

std::size_t found_cities(sim::Simulation& simulation, sim::World& world, const Tables& tables,
                         const std::vector<sim::PlayerId>& owners, std::size_t per_island, std::string* why_not) {
    if (owners.empty()) {
        return 0;
    }
    std::size_t founded = 0;
    std::size_t next_owner = 0;
    for (std::size_t i = 0; i < world.islands().size(); ++i) {
        const sim::IslandMap& map = world.islands()[i].map;
        // Kontor spots along the east coast, spread over its length.
        std::size_t here = 0;
        const auto step = std::max<std::uint32_t>(4, map.height() / static_cast<std::uint32_t>(per_island));
        for (std::uint32_t y = 2; y + 4 < map.height() && here < per_island; y += step) {
            const sim::PlayerId owner = owners[next_owner % owners.size()];
            bool ok = false;
            for (std::int32_t dx = 0; dx < 6 && !ok; ++dx) {
                const auto x = static_cast<std::int32_t>(map.width()) - 7 + dx - 2;
                sim::BuildOrder order{owner,
                                      static_cast<std::uint16_t>(i),
                                      {sim::BuildItem{kKontor, x, static_cast<std::int32_t>(y)}},
                                      std::nullopt,
                                      true};
                const sim::BuildPlan plan = sim::plan_build(world, tables.buildings, order);
                if (plan.placed == 1) {
                    const sim::CommandResult r = simulation.execute(order);
                    ok = r.build && r.build->placed == 1;
                } else if (why_not != nullptr && why_not->empty() && !plan.items.empty()) {
                    *why_not = sim::refusal_name(plan.items.front().refusal);
                }
            }
            if (ok) {
                ++founded;
                ++here;
                ++next_owner;
            }
        }
    }
    (void)tables;
    return founded;
}

std::size_t fill_cities(sim::Simulation& simulation, sim::World& world, const Tables& tables,
                        const std::vector<sim::PlayerId>& owners, std::size_t max_per_city) {
    const assets::BuildingTable& b = tables.buildings;
    const std::array<std::uint32_t, 6> special{kMarket, kWorkshop, kKlinik, kBrunnen, kWorkshop, kHouse};
    std::size_t placed = 0;
    // Every build level unlocked, as late in a game (the original's tables lock most buildings).
    const std::size_t levels = std::min<std::size_t>(b.infra_levels().size(), 32);
    const std::uint32_t all = levels >= 32 ? 0xFFFFFFFFU : (1U << levels) - 1U;
    for (const sim::PlayerId p : owners) {
        if (sim::Player* player = world.player(p)) {
            player->infra_mask |= all;
        }
    }
    // Copy: building changes the city list.
    std::vector<std::pair<std::uint16_t, std::uint8_t>> cities;
    for (const sim::City& c : world.cities()) {
        if (std::find(owners.begin(), owners.end(), c.owner) != owners.end()) {
            cities.emplace_back(c.island, c.index);
        }
    }
    for (const auto& [island, index] : cities) {
        const sim::City* city = world.city(island, index);
        if (city == nullptr) {
            continue;
        }
        const sim::PlayerId owner = city->owner;
        const sim::IslandMap& map = world.islands()[island].map;
        std::size_t here = 0;
        std::size_t slot = 0;
        // Bands of 5 rows: a road, two rows of 2x2 buildings; every seventh slot something else.
        for (std::uint32_t y = 1; y + 5 < map.height() && here < max_per_city; y += 5) {
            sim::BuildOrder road{owner, island, {}, std::nullopt, true};
            for (std::uint32_t x = 1; x + 1 < map.width(); ++x) {
                const sim::MapCell& c = map.top(x, y);
                const sim::SubTile t = map.sub_tile(b, x, y);
                if (c.city == index && t.building != nullptr && walkable_kind(t.building->kind)) {
                    road.items.push_back(
                        sim::BuildItem{kRoad, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)});
                }
            }
            if (!road.items.empty()) {
                const auto r = simulation.execute(road);
                placed += r.build ? r.build->placed : 0;
            }
            for (std::uint32_t row = 0; row < 2; ++row) {
                sim::BuildOrder order{owner, island, {}, std::nullopt, true};
                for (std::uint32_t x = 1; x + 3 < map.width() && here < max_per_city; x += 2, ++slot) {
                    // Only plots wholly in this city: an order belongs to the city under its first item.
                    const std::uint32_t py = y + 1 + 2 * row;
                    bool inside = true;
                    for (std::uint32_t dy = 0; dy < 2 && inside; ++dy) {
                        for (std::uint32_t dx = 0; dx < 2 && inside; ++dx) {
                            inside = py + dy < map.height() && map.top(x + dx, py + dy).city == index;
                        }
                    }
                    if (!inside) {
                        continue;
                    }
                    std::uint32_t id = slot % 7 == 6 ? special[(slot / 7) % special.size()] : kHouse;
                    if (b.by_id(id) == nullptr || b.by_id(id)->size_x != 2 || b.by_id(id)->size_y != 2) {
                        id = kHouse;
                    }
                    order.items.push_back(
                        sim::BuildItem{id, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y + 1 + 2 * row)});
                    ++here;
                }
                if (!order.items.empty()) {
                    const auto r = simulation.execute(order);
                    placed += r.build ? r.build->placed : 0;
                }
            }
        }
    }
    return placed;
}

void top_up(sim::World& world, std::uint32_t amount) {
    for (sim::City& c : const_cast<std::vector<sim::City>&>(world.cities())) {
        c.ensure_ware(assets::ware::kBricks);
        for (std::size_t w = 2; w < c.goods.size() && w <= assets::ware::kBricks; ++w) {
            c.goods[w] = std::max(c.goods[w], std::min(amount, c.capacity > 0 ? c.capacity : amount));
        }
    }
    for (std::size_t p = 0; p < world.players().size(); ++p) {
        sim::Player* player = world.player(static_cast<sim::PlayerId>(p));
        if (player != nullptr &&
            (player->role == sim::PlayerRole::Human || player->role == sim::PlayerRole::Computer)) {
            player->money = std::max(player->money, 1'000'000);
        }
    }
}

void declare_war(sim::World& world, const std::vector<sim::PlayerId>& players) {
    for (std::size_t i = 0; i < players.size(); ++i) {
        for (std::size_t j = i + 1; j < players.size(); ++j) {
            (void)world.diplomacy().set_peace(world, players[i], players[j], sim::Treaty::None, world.time_ms());
        }
    }
}

std::vector<std::pair<std::uint32_t, std::uint32_t>> free_land(const sim::World& world, const Tables& tables,
                                                               std::uint16_t island) {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> out;
    const sim::WorldIsland& isl = world.islands()[island];
    for (std::uint32_t y = 1; y + 1 < isl.map.height(); ++y) {
        for (std::uint32_t x = 1; x + 1 < isl.map.width(); ++x) {
            const sim::SubTile t = isl.map.sub_tile(tables.buildings, x, y);
            if (t.building != nullptr && walkable_kind(t.building->kind)) {
                out.emplace_back(isl.x + x, isl.y + y);
            }
        }
    }
    return out;
}

}  // namespace o1602::stress
