// SPDX-License-Identifier: GPL-3.0-or-later
// The bytes of Open1602's own savegame sections (docs/formats/o1602-save.md, o1602-chunk.md) stay
// the same: a world with every saved field set to a value of its own is written as the own
// savegame and as the chunk in a .GAM, and every section must keep the size and CRC-32 recorded
// here. Reading those bytes and writing them again gives the same bytes, so each reader takes
// every field from where its writer put it.
//
// A change of the format on purpose gives the section a new version and updates the tables; the
// test prints the new ones.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/chunk_writer.hpp"
#include "o1602/assets/cod_parser.hpp"
#include "o1602/core/compress.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/savegame_extension.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kRoad = 20401;
constexpr std::uint32_t kHouse = 20501;
constexpr std::size_t kFileHeader = 32;   ///< own savegame, in front of the sections
constexpr std::size_t kChunkHeader = 20;  ///< chunk in a .GAM: magic, version, flags, fingerprint
constexpr std::size_t kPlayers = 4;

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

/// Values that differ from field to field and fill the whole width of each, so that two fields
/// that swap places or a field that changes its width change the bytes.
class Values {
public:
    template <class T>
    T next() {
        std::uint64_t z = (m_n += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        z ^= z >> 31U;
        if constexpr (std::is_same_v<T, bool>) {
            return (z & 1U) != 0;
        } else {
            return static_cast<T>(z);
        }
    }
    template <class T>
    void operator()(T& v) {
        v = next<T>();
    }
    /// A value below `n`.
    std::uint32_t below(std::uint32_t n) { return next<std::uint32_t>() % n; }
    sim::PlayerId player() { return static_cast<sim::PlayerId>(below(kPlayers)); }
    std::string text() { return "text " + std::to_string(next<std::uint16_t>()); }

private:
    std::uint64_t m_n = 0;
};

sim::Target target(Values& v) {
    sim::Target t;
    t.kind = static_cast<sim::Target::Kind>(v.below(7));
    v(t.island);
    v(t.city);
    v(t.x);
    v(t.y);
    v(t.ship);
    v(t.raw);
    return t;
}

/// Islands of 12 x 10 tiles with ground, a road and a house; `wide` adds one of 260 x 3 tiles with
/// a crater beyond 255 (version 2 of ISLE and VOLC).
void add_islands(sim::World& world, const assets::BuildingTable& buildings, bool wide) {
    struct Shape {
        std::uint32_t x, y, w, h;
    };
    std::vector<Shape> shapes = {{20, 40, 12, 10}, {100, 40, 12, 10}};
    if (wide) {
        shapes.push_back({20, 200, 260, 3});
    }
    Values v;
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        const Shape& s = shapes[i];
        sim::IslandMap map(s.w, s.h);
        for (std::uint32_t y = 0; y < s.h; ++y) {
            for (std::uint32_t x = 0; x < s.w; ++x) {
                REQUIRE(map.place(*buildings.by_id(kGround), static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
                                  0, 0)
                            .ok());
            }
        }
        map.capture_ground();
        REQUIRE(map.build(*buildings.by_id(kRoad), 3, 1, 0, 0, 1, 0).ok());
        REQUIRE(map.build(*buildings.by_id(kHouse), 5, 1, 1, 2, 1, 0).ok());
        for (std::uint32_t y = 0; y < s.h; ++y) {
            for (std::uint32_t x = 0; x < s.w; ++x) {
                sim::MapCell top = map.top(x, y);
                top.rotation = static_cast<std::uint8_t>(v.below(4));
                top.anim_phase = static_cast<std::uint8_t>(v.below(16));
                const std::uint32_t owner = v.below(kPlayers + 1);
                top.owner = owner == kPlayers ? sim::kNoPlayer : static_cast<sim::PlayerId>(owner);
                v(top.city);
                v(top.random);
                map.set_top_cell(x, y, top);
                sim::MapCell ground = map.ground(x, y);
                ground.random = static_cast<std::uint8_t>(v.below(32));
                map.set_ground_cell(x, y, ground);
            }
        }
        assets::IslandHeader header;
        header.chunk_name = "INSEL5";
        header.number = static_cast<std::uint8_t>(i == 1 ? 7 : i);  // numbers need not follow the order
        header.width = static_cast<std::uint8_t>(s.w);
        header.height = static_cast<std::uint8_t>(s.h);
        v(header.flags);
        v(header.pos_x);
        v(header.pos_y);
        v(header.template_number);
        v(header.template_size);
        v(header.climate);
        v(header.stored_complete);
        REQUIRE(world.add_island(sim::WorldIsland{header, std::move(map), s.x, s.y}).ok());
    }
}

/// A world with every field the sections save set to a value of its own (within what the readers
/// accept and validate() allows).
sim::World make_world(const assets::BuildingTable& buildings, bool wide) {
    sim::World world(400, 300);
    add_islands(world, buildings, wide);
    Values v;

    for (std::size_t i = 0; i < world.islands().size(); ++i) {
        sim::WorldIsland& island = world.island(i);
        const auto w = static_cast<std::uint16_t>(island.map.width());
        const auto h = static_cast<std::uint16_t>(island.map.height());
        for (sim::DeerSpot& s : island.deer_spots) {
            v(s.used);
            s.x = static_cast<std::uint16_t>(v.below(w));
            s.y = static_cast<std::uint16_t>(v.below(h));
            v(s.respawn_ms);
        }
        island.deer_phase = static_cast<std::uint8_t>(v.below(4));
        island.last_phase = static_cast<std::uint8_t>(v.below(8));
        v(island.drought.strength);
        v(island.drought.end_ms);
        v(island.drought.curse_pending);
        for (int k = 0; k < 2; ++k) {
            sim::Crater c;
            c.x = static_cast<std::uint16_t>(v.below(std::min<std::uint16_t>(w, 256)));
            c.y = static_cast<std::uint16_t>(v.below(h));
            v(c.steps_left);
            island.craters.push_back(c);
        }
        if (w > 256) {
            island.craters[1].x = 258;
        }
        v(island.volcano_erupted);
        for (std::size_t p = 0; p < kPlayers + 1; ++p) {
            island.discovered.push_back(v.next<bool>());
            island.explored.push_back(v.next<bool>());
        }
        for (int k = 0; k < 2; ++k) {
            assets::IslandHeader::Deposit d;
            v(d.ware);
            v(d.x);
            v(d.y);
            v(d.mask);
            v(d.kind);
            v(d.amount);
            island.header.deposits.push_back(d);
        }
        v(island.header.fertility);
    }

    std::vector<sim::Player> players(kPlayers);
    for (std::size_t p = 0; p < players.size(); ++p) {
        sim::Player& pl = players[p];
        pl.role = p == 0 ? sim::PlayerRole::Human : static_cast<sim::PlayerRole>(1 + v.below(4));
        v(pl.banner);
        v(pl.money);
        pl.name = v.text();
        v(pl.infra_mask);
        v(pl.castles_built);
        v(pl.cathedrals_built);
        v(pl.arches_allowed);
        v(pl.arches_built);
        v(pl.monuments_allowed);
        v(pl.monuments_built);
        pl.mission = p == 3 ? sim::kNoMission : static_cast<sim::MissionId>(v.below(2));
        pl.status = static_cast<sim::PlayerStatus>(v.below(3));
        v(pl.won);
        v(pl.opponents_defeated);
        v(pl.soldiers_killed);
        v(pl.soldiers_lost);
        v(pl.ships_sunk);
        v(pl.ships_lost);
        v(pl.bankrupt_checks);
        v(pl.empty_checks);
        v(pl.purchases);
        v(pl.sales);
    }
    world.set_players(players);

    std::vector<sim::Mission> missions(2);
    for (sim::Mission& m : missions) {
        m.goals = v.next<std::uint32_t>() & (sim::goal::kKnown | sim::goal::kComputers);
        m.losses = v.next<std::uint32_t>() & sim::loss::kKnown;
        m.target = v.player();
        v(m.min_defeated);
        m.defeat = {v.player(), v.player(), v.player()};
        v(m.min_cities);
        v(m.min_inhabitants);
        v(m.min_balance);
        v(m.min_trade_balance);
        v(m.protected_min_cities);
        v(m.success_message);
        m.text = v.text();
        for (sim::Mission::GoodsGoal& g : m.goods) {
            v(g.ware);
            v(g.amount);
        }
        for (sim::Mission::IslandGoal& g : m.islands) {
            v(g.resource);
            g.islands = {static_cast<std::uint16_t>(v.below(2)), static_cast<std::uint16_t>(v.below(2))};
        }
        for (sim::Mission::CityGoal* g :
             {&m.cities[0], &m.cities[1], &m.cities[2], &m.every_city, &m.target_city, &m.protected_city}) {
            v(g->inhabitants);
            v(g->resident_class);
            v(g->count);
        }
        v(m.unused_48);
        v(m.unused_58);
    }
    world.set_missions(missions);
    world.set_game_end(sim::World::GameEnd{v.next<std::uint64_t>(), v.player()});

    for (std::uint8_t c = 0; c < 2; ++c) {
        sim::City city;
        city.island = c;
        city.index = static_cast<std::uint8_t>(c + 1);
        city.owner = v.player();
        city.name = v.text();
        for (int k = 0; k < 5; ++k) {
            city.goods.push_back(v.next<std::uint32_t>());
            city.reserved.push_back(v.next<std::uint32_t>());
            city.incoming.push_back(v.next<std::uint32_t>());
        }
        if (c == 0) {
            city.warehouse_x = v.next<std::uint16_t>();
            city.warehouse_y = v.next<std::uint16_t>();
        }
        v(city.capacity);
        for (int k = 0; k < 3; ++k) {
            city.inhabitants.push_back(v.next<std::uint32_t>());
            city.satisfaction.push_back(v.next<std::uint8_t>());
            city.tax_rate.push_back(v.next<std::uint8_t>());
            sim::City::Supply s;
            v(s.demand);
            v(s.supplied);
            v(s.ratio);
            for (std::uint8_t& x : s.history) {
                v(x);
            }
            city.supply.push_back(s);
            sim::City::UpgradeSlot u;
            v(u.inhabitants);
            v(u.x);
            v(u.y);
            city.upgrade_queue.push_back(u);
            sim::City::Trade t;
            v(t.sell);
            v(t.buy);
            v(t.sell_price);
            v(t.buy_price);
            v(t.keep);
            v(t.limit);
            city.trade.push_back(t);
            city.goodwill.push_back(v.next<std::uint32_t>());
        }
        v(city.food_ratio);
        v(city.satisfaction_bonus);
        v(city.epoch);
        v(city.material_lock);
        v(city.worst_good);
        v(city.next_event_ms);
        v(city.house_growth);
        v(city.growing_since_ms);
        v(city.trader_inbound);
        v(city.last_attacked_ms);
        v(city.cursed);
        v(city.purchases);
        v(city.sales);
        world.add_city(city);
    }

    // A place on island i (12 x 10 tiles, or the first 12 x 3 of the wide one).
    const auto at = [&](std::uint16_t& island, std::uint16_t& x, std::uint16_t& y) {
        island = static_cast<std::uint16_t>(v.below(static_cast<std::uint32_t>(world.islands().size())));
        x = static_cast<std::uint16_t>(v.below(12));
        y = static_cast<std::uint16_t>(v.below(3));
    };

    std::vector<sim::Production> productions(4);
    for (std::size_t k = 0; k < productions.size(); ++k) {
        sim::Production& p = productions[k];
        at(p.island, p.x, p.y);
        p.used = k != 2;
        p.enabled = p.used && v.next<bool>();  // a free slot never runs
        v(p.rate);
        v(p.anim_phase);
        v(p.idle_cycles);
        v(p.flags);
        v(p.countdown);
        v(p.raw);
        v(p.raw2);
        v(p.stock);
        v(p.produced);
        v(p.elapsed);
        v(p.reserved);
    }
    world.set_productions(productions, {2});

    std::vector<sim::House> houses(3);
    for (sim::House& h : houses) {
        at(h.island, h.x, h.y);
        v(h.city);
        v(h.resident_class);
        v(h.inhabitants64);
        v(h.timer_group);
        v(h.epoch);
        v(h.queue_epoch);
        v(h.market_distance);
        v(h.market);
        v(h.services_complete);
        v(h.services);
    }
    world.set_houses(houses);

    for (int k = 0; k < 3; ++k) {
        sim::GrowthEntry e;
        at(e.island, e.x, e.y);
        v(e.growth_class);
        v(e.seen);
        v(e.stage);
        world.growth().entries.push_back(e);
    }
    for (std::size_t c = 0; c < sim::GrowthState::kClasses; ++c) {
        v(world.growth().time_ms[c]);
        v(world.growth().counter[c]);
    }
    for (std::size_t g = 0; g < sim::HouseClocks::kGroups; ++g) {
        v(world.house_clocks().time_ms[g]);
        v(world.house_clocks().epoch[g]);
    }

    for (int k = 0; k < 2; ++k) {
        sim::CostBuilding b;
        b.kind = k == 0 ? sim::CostBuilding::Kind::Shipyard : sim::CostBuilding::Kind::Castle;
        at(b.island, b.x, b.y);
        v(b.enabled);
        v(b.order);
        v(b.progress);
        v(b.wood);
        v(b.cloth);
        v(b.rate);
        v(b.countdown);
        v(b.repair_ship);
        v(b.idle_cycles);
        v(b.fetching);
        for (std::uint32_t& weapon : b.weapons) {
            v(weapon);
        }
        for (int s = 0; s < 2; ++s) {
            sim::CastleSlot slot;
            v(slot.figure);
            v(slot.training);
            v(slot.armed);
            v(slot.health);
            v(slot.progress);
            b.slots.push_back(slot);
        }
        world.add_cost_building(b);
    }

    for (std::uint32_t k = 0; k < 3; ++k) {
        sim::Ship s;
        s.place(150 + 7 * k, 100 + k);
        s.pos_x += static_cast<std::int32_t>(v.below(sim::kShipUnit / 2));
        s.pos_y += static_cast<std::int32_t>(v.below(sim::kShipUnit / 2));
        s.name = v.text();
        v(s.figure);
        s.owner = v.player();
        s.direction = static_cast<std::uint8_t>(v.below(8));
        v(s.health);
        v(s.cannons);
        for (int c = 0; c < 4; ++c) {
            sim::CargoSlot slot;
            slot.ware = c == 2 ? 0 : static_cast<std::uint8_t>(1 + v.below(60));  // one empty hold
            v(slot.amount);
            v(slot.price);
            s.cargo.push_back(slot);
        }
        for (sim::RouteStop& stop : s.route.stops) {
            stop.target = target(v);
            for (sim::RouteEntry& e : stop.entries) {
                v(e.ware);
                v(e.unload);
                v(e.amount);
            }
        }
        s.route.current = static_cast<std::uint8_t>(v.below(sim::TradeRoute::kStops));
        v(s.route.active);
        v(s.route.one_way);
        v(s.route.problem);
        v(s.route.problem_rounds);
        s.target = target(v);
        s.order = target(v);
        s.previous = target(v);
        if (k != 1) {
            s.queue = {target(v), target(v)};
        }
        s.attack_move = k == 0;
        s.hold = k != 1;
        v(s.held);
        v(s.sinking);
        v(s.next_shot_ms);
        v(s.repairing);
        v(s.for_sale);
        v(s.sale_factor);
        v(s.trader);
        for (int g = 0; g < 2; ++g) {
            sim::TraderGood good;
            v(good.price);
            v(good.age);
            v(good.amount);
            s.trader_goods.push_back(good);
        }
        v(s.trader_tools_ms);
        v(s.trader_ore_ms);
        v(s.near_island);
        v(s.source_slot);
        s.motion.phase = static_cast<sim::ShipMotion::Phase>(v.below(5));
        v(s.motion.count);
        v(s.motion.elapsed_us);
        v(s.motion.duration_us);
        v(s.motion.from_x);
        v(s.motion.from_y);
        s.motion.path = {v.next<std::uint8_t>(), v.next<std::uint8_t>(), v.next<std::uint8_t>()};
        v(s.motion.path_index);
        v(s.motion.radius);
        v(s.motion.failures);
        v(s.motion.turn_to);
        v(s.motion.next_follow_us);
        world.add_ship(s);
    }
    world.set_next_ship_id(9);

    for (std::uint32_t k = 0; k < 3; ++k) {
        sim::Soldier s;
        s.pos_x = static_cast<std::int32_t>((22 + k) * 4096 + v.below(4096));
        s.pos_y = static_cast<std::int32_t>(41 * 4096 + v.below(4096));
        v(s.figure);
        s.owner = v.player();
        s.direction = static_cast<std::uint8_t>(v.below(8));
        v(s.anim);
        s.island = k == 2 ? 0xFFFF : static_cast<std::uint16_t>(k);
        v(s.health);
        s.home = target(v);
        s.target = target(v);
        v(s.radius);
        v(s.failures);
        v(s.next_attack_ms);
        v(s.next_follow_ms);
        v(s.last_attack_ms);
        v(s.patrol.active);
        v(s.patrol.one_way);
        s.patrol.index = static_cast<std::uint8_t>(v.below(2));
        s.patrol.points = {target(v), target(v)};
        if (k != 0) {
            s.queue = {target(v)};
        }
        s.attack_move = k == 1;
        s.hold = k != 0;
        v(s.disbanded);
        v(s.source_slot);
        s.motion.phase = static_cast<sim::SoldierMotion::Phase>(v.below(5));
        s.motion.direction = static_cast<std::uint8_t>(v.below(8));
        v(s.motion.count);
        v(s.motion.elapsed_us);
        v(s.motion.duration_us);
        v(s.motion.from_x);
        v(s.motion.from_y);
        s.motion.path = {v.next<std::uint8_t>(), v.next<std::uint8_t>()};
        v(s.motion.path_index);
        world.add_soldier(s);
    }
    world.set_next_soldier_id(12);

    for (int k = 0; k < 2; ++k) {
        sim::Tower t;
        at(t.island, t.x, t.y);
        t.owner = v.player();
        v(t.figure);
        t.direction = static_cast<std::uint8_t>(v.below(8));
        v(t.next_shot_ms);
        v(t.next_think_us);
        v(t.source_slot);
        world.towers().push_back(t);

        sim::BuildingDamage d;
        at(d.island, d.x, d.y);
        v(d.damage);
        world.building_damage().push_back(d);

        sim::PendingHit hit;
        v(hit.due_ms);
        hit.shooter = target(v);
        hit.attacker = v.player();
        v(hit.cannon);
        v(hit.damage);
        hit.target = target(v);
        v(hit.from_x);
        v(hit.from_y);
        world.pending_hits().push_back(hit);

        sim::Animal a;
        at(a.island, a.x, a.y);
        v(a.figure);
        world.add_animal(a);

        sim::Disaster dis;
        dis.kind = k == 0 ? sim::Disaster::Kind::Plague : sim::Disaster::Kind::Fire;
        at(dis.island, dis.x, dis.y);
        v(dis.elapsed);
        v(dis.duration);
        v(dis.id);
        world.disasters().push_back(dis);
    }

    sim::Diplomacy& dip = world.diplomacy();
    for (sim::PlayerId a = 0; a < kPlayers; ++a) {
        for (sim::PlayerId b = 0; b < kPlayers; ++b) {
            sim::Relation& r = dip.relation(a, b);
            r.peace = static_cast<sim::Treaty>(v.below(4));
            r.trade = static_cast<sim::Treaty>(v.below(4));
            v(r.war_time_ms);
            v(r.tribute);
            v(r.trade_mood);
            v(r.damage);
        }
        std::array<sim::DiplomacyEvent, sim::Diplomacy::kEvents> events{};
        for (sim::DiplomacyEvent& e : events) {
            e.type = static_cast<sim::DiplomacyEventType>(v.below(14));
            e.partner = v.player();
            v(e.time_ms);
        }
        dip.set_events(a, events);
        dip.set_blamed(a, v.player());
    }

    sim::VolcanoState& volcanoes = world.volcanoes();
    v(volcanoes.next_ms);
    const std::uint16_t crater_island = wide ? 2 : 1;
    const sim::Crater& crater = world.islands()[crater_island].craters[1];
    volcanoes.last = sim::CraterRef{crater_island, crater.x, crater.y};
    sim::Eruption e;
    e.crater = sim::CraterRef{0, world.islands()[0].craters[0].x, world.islands()[0].craters[0].y};
    v(e.seed);
    v(e.step);
    v(e.next_us);
    volcanoes.eruptions.push_back(e);
    sim::LavaLump lump;
    lump.island = 1;
    for (std::int64_t* q : {&lump.x, &lump.y, &lump.z, &lump.dx, &lump.dy, &lump.dz, &lump.speed}) {
        v(*q);
    }
    v(lump.launch_us);
    v(lump.leg_start_us);
    v(lump.leg_end_us);
    v(lump.heading);
    v(lump.tilt);
    v(lump.grounded);
    v(lump.anim);
    volcanoes.lava.push_back(lump);

    world.set_time_ms(v.next<std::uint32_t>() / 50 * 50);  // game time moves in ticks of 50 ms
    world.set_city_name_index(v.next<std::uint32_t>());
    sim::World::GameSettings settings;
    v(settings.difficulty);
    v(settings.flags);
    v(settings.pirate_interval_s);
    world.set_game_settings(settings);
    world.set_next_drought_ms(v.next<std::uint64_t>());
    world.set_drought_seed(v.next<std::uint8_t>());
    for (std::size_t s = 0; s < static_cast<std::size_t>(sim::RandomStream::Count); ++s) {
        const auto state = v.next<std::uint64_t>();
        world.random(static_cast<sim::RandomStream>(s)) = sim::Random::from_state(state, v.next<std::uint64_t>());
    }
    world.set_next_disaster_id(v.next<std::uint32_t>());
    world.set_island_phase(v.next<std::uint8_t>());

    sim::Rules rules;
    rules.demolish_refund = sim::Refund::Partial;
    rules.refund_percent = 37;
    v(rules.max_ships_per_player);
    rules.guarantee_resources = false;
    v(rules.max_game_speed);
    rules.droughts = sim::Droughts::Frequent;
    rules.volcanoes = sim::Volcanoes::On;
    rules.trader_prices = sim::TraderPrices::Market;
    v(rules.max_army);
    world.set_saved_rules(rules);

    // The original's file the game came from (ORIG keeps every chunk but the island tiles).
    assets::ChunkWriter chunks;
    const std::vector<std::uint8_t> payload = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    chunks.add("INSEL5", payload);
    chunks.add("INSELHAUS", payload);
    chunks.add("SHIP4", std::span<const std::uint8_t>(payload).first(5));
    auto source = assets::ChunkFile::from_bytes(chunks.take());
    REQUIRE(source.ok());
    world.set_source(std::make_shared<const assets::ChunkFile>(std::move(source).value()));

    std::vector<std::uint8_t> state(40);
    for (std::uint8_t& b : state) {
        v(b);
    }
    world.set_simulation_state(state);
    return world;
}

/// What the tables record of a section: tag, version, flags, size and CRC-32 of the data.
struct Golden {
    const char* tag;
    std::uint16_t version;
    std::uint16_t flags;
    std::uint32_t size;
    std::uint32_t crc;
};

/// Compares the sections with a table; prints the table the bytes give if they differ.
bool matches(const char* name, std::span<const std::uint8_t> data, std::span<const Golden> expected) {
    struct Section {
        std::string tag;
        std::uint16_t version = 0;
        std::uint16_t flags = 0;
        std::uint32_t size = 0;
        std::uint32_t crc = 0;
    };
    std::vector<Section> got;
    std::size_t at = 0;
    const auto u16 = [&](std::size_t i) { return static_cast<std::uint16_t>(data[i] | data[i + 1] << 8U); };
    while (at + 12 <= data.size()) {
        Section s;
        s.tag.assign(reinterpret_cast<const char*>(data.data() + at), 4);
        s.version = u16(at + 4);
        s.flags = u16(at + 6);
        s.size = u16(at + 8) | static_cast<std::uint32_t>(u16(at + 10)) << 16U;
        REQUIRE(s.size <= data.size() - at - 12);
        s.crc = core::crc32(data.subspan(at + 12, s.size));
        got.push_back(s);
        at += 12 + s.size;
    }
    bool same = at == data.size() && got.size() == expected.size();
    for (std::size_t i = 0; same && i < got.size(); ++i) {
        const Section& a = got[i];
        const Golden& b = expected[i];
        same = a.tag == b.tag && a.version == b.version && a.flags == b.flags && a.size == b.size && a.crc == b.crc;
    }
    if (!same) {
        std::printf("  %s: the sections give\n", name);
        for (const Section& s : got) {
            std::printf("    {\"%s\", %u, %u, %u, 0x%08X},\n", s.tag.c_str(), s.version, s.flags, s.size, s.crc);
        }
    }
    return same;
}

// clang-format off
constexpr Golden kSaveNarrow[] = {
    {"META", 1, 0, 37, 0x5A2EBCE3},
    {"MAPW", 1, 1, 4892, 0xE5B4A6F5},
    {"RULE", 1, 1, 58, 0xF921B686},
    {"WRLD", 1, 1, 419, 0xC3B8D9A4},
    {"WRL2", 1, 1, 4, 0x5ADE7D82},
    {"ISLE", 1, 1, 274, 0x6FCE0641},
    {"PLAY", 1, 1, 143, 0xFF1BD500},
    {"PSTA", 1, 1, 96, 0xAE93529B},
    {"MISN", 1, 1, 302, 0xD6CFC995},
    {"CITY", 1, 1, 474, 0x37ECCCBD},
    {"CSTA", 1, 1, 42, 0x2AD09545},
    {"SCOR", 1, 1, 70, 0x9488157B},
    {"PROD", 1, 1, 116, 0x24D09C54},
    {"HOUS", 1, 1, 64, 0x8D864860},
    {"GROW", 1, 1, 31, 0x1C17DE55},
    {"COST", 1, 1, 20, 0xFACEC437},
    {"ANIM", 1, 1, 20, 0x86AEF97B},
    {"DISA", 1, 1, 30, 0x5BCDD397},
    {"VOLC", 1, 1, 122, 0x1FEE9F6C},
    {"FLEE", 1, 1, 1289, 0xDB13FE9E},
    {"EPOC", 1, 1, 9, 0xD322D3E6},
    {"ARMY", 1, 1, 842, 0x9D932289},
    {"DIPL", 1, 1, 1640, 0xE500956A},
    {"XPLR", 1, 1, 46, 0xC4686738},
    {"QUEU", 1, 1, 148, 0x52E1525C},
    {"STNC", 1, 1, 28, 0x6CE6EFC4},
    {"ORIG", 1, 0, 54, 0x7AEE655B},
    {"SIMU", 1, 1, 40, 0xB72D21DD},
};

constexpr Golden kSaveWide[] = {
    {"META", 1, 0, 38, 0xB89CD646},
    {"MAPW", 1, 1, 20532, 0x262A247C},
    {"RULE", 1, 1, 58, 0xE2443C4C},
    {"WRLD", 1, 1, 419, 0x3DED0795},
    {"WRL2", 1, 1, 4, 0x637B2D3C},
    {"ISLE", 2, 1, 469, 0x3F8E764F},
    {"PLAY", 1, 1, 143, 0x177E459C},
    {"PSTA", 1, 1, 96, 0x96C3346A},
    {"MISN", 1, 1, 302, 0x93D421B5},
    {"CITY", 1, 1, 473, 0x2E7EAC9E},
    {"CSTA", 1, 1, 42, 0x70987BC5},
    {"SCOR", 1, 1, 70, 0xC46D7A86},
    {"PROD", 1, 1, 116, 0x4814F74A},
    {"HOUS", 1, 1, 64, 0x15190742},
    {"GROW", 1, 1, 31, 0xA45C7F9C},
    {"COST", 1, 1, 20, 0x5E0C98CB},
    {"ANIM", 1, 1, 20, 0x45A8858E},
    {"DISA", 1, 1, 30, 0x3FF2904C},
    {"VOLC", 2, 1, 126, 0x3DB4583E},
    {"FLEE", 1, 1, 1299, 0xF206C9B1},
    {"EPOC", 1, 1, 11, 0xFD024085},
    {"ARMY", 1, 1, 842, 0xC84ED2BD},
    {"DIPL", 1, 1, 1640, 0xE77D58A7},
    {"XPLR", 1, 1, 67, 0x743711C9},
    {"QUEU", 1, 1, 148, 0x58163040},
    {"STNC", 1, 1, 28, 0x6CE6EFC4},
    {"ORIG", 1, 0, 54, 0x7AEE655B},
    {"SIMU", 1, 1, 40, 0xFA8DCE43},
};

constexpr Golden kChunkNarrow[] = {
    {"RULE", 1, 1, 58, 0xF921B686},
    {"WRLD", 1, 1, 419, 0xC3B8D9A4},
    {"ISLE", 1, 1, 274, 0x6FCE0641},
    {"TILE", 1, 1, 8164, 0x769F7EC8},
    {"PLAY", 1, 1, 143, 0xFF1BD500},
    {"PSTA", 1, 1, 96, 0xAE93529B},
    {"MISN", 1, 1, 302, 0xD6CFC995},
    {"CITY", 1, 1, 474, 0x37ECCCBD},
    {"CSTA", 1, 1, 42, 0x2AD09545},
    {"SCOR", 1, 1, 70, 0x9488157B},
    {"PROD", 1, 1, 116, 0x24D09C54},
    {"HOUS", 1, 1, 64, 0x8D864860},
    {"GROW", 1, 1, 31, 0x1C17DE55},
    {"COST", 1, 1, 20, 0xFACEC437},
    {"SHIP", 1, 0, 127, 0xDA9C5944},
    {"ANIM", 1, 1, 20, 0x86AEF97B},
    {"DISA", 1, 1, 30, 0x5BCDD397},
    {"VOLC", 1, 1, 122, 0x1FEE9F6C},
    {"FLEE", 1, 1, 1289, 0xDB13FE9E},
    {"EPOC", 1, 1, 9, 0xD322D3E6},
    {"ARMY", 1, 1, 842, 0x9D932289},
    {"DIPL", 1, 1, 1640, 0xE500956A},
    {"XPLR", 1, 1, 46, 0xC4686738},
    {"QUEU", 1, 1, 148, 0x52E1525C},
    {"STNC", 1, 1, 28, 0x6CE6EFC4},
    {"SIMU", 1, 1, 40, 0xB72D21DD},
};

constexpr Golden kChunkWide[] = {
    {"RULE", 1, 1, 58, 0xE2443C4C},
    {"WRLD", 1, 1, 419, 0x3DED0795},
    {"ISLE", 2, 1, 469, 0x3F8E764F},
    {"TILE", 1, 1, 34684, 0x3991F216},
    {"PLAY", 1, 1, 143, 0x177E459C},
    {"PSTA", 1, 1, 96, 0x96C3346A},
    {"MISN", 1, 1, 302, 0x93D421B5},
    {"CITY", 1, 1, 473, 0x2E7EAC9E},
    {"CSTA", 1, 1, 42, 0x70987BC5},
    {"SCOR", 1, 1, 70, 0xC46D7A86},
    {"PROD", 1, 1, 116, 0x4814F74A},
    {"HOUS", 1, 1, 64, 0x15190742},
    {"GROW", 1, 1, 31, 0xA45C7F9C},
    {"COST", 1, 1, 20, 0x5E0C98CB},
    {"SHIP", 1, 0, 127, 0x0B22D35F},
    {"ANIM", 1, 1, 20, 0x45A8858E},
    {"DISA", 1, 1, 30, 0x3FF2904C},
    {"VOLC", 2, 1, 126, 0x3DB4583E},
    {"FLEE", 1, 1, 1299, 0xF206C9B1},
    {"EPOC", 1, 1, 11, 0xFD024085},
    {"ARMY", 1, 1, 842, 0xC84ED2BD},
    {"DIPL", 1, 1, 1640, 0xE77D58A7},
    {"XPLR", 1, 1, 67, 0x743711C9},
    {"QUEU", 1, 1, 148, 0x58163040},
    {"STNC", 1, 1, 28, 0x6CE6EFC4},
    {"SIMU", 1, 1, 40, 0xFA8DCE43},
};
// clang-format on

core::Bytes save(const sim::World& world, const assets::BuildingTable& buildings) {
    auto bytes = sim::write_game(world, buildings, nullptr, sim::SaveOptions{false});
    REQUIRE(bytes.ok());
    return std::move(bytes).value();
}

}  // namespace

TEST_CASE("save format: the sections of the own savegame keep their bytes") {
    const auto buildings = make_buildings();
    for (const bool wide : {false, true}) {
        const core::Bytes bytes = save(make_world(buildings, wide), buildings);
        CHECK(matches(wide ? "kSaveWide" : "kSaveNarrow", std::span<const std::uint8_t>(bytes).subspan(kFileHeader),
                      wide ? std::span<const Golden>(kSaveWide) : std::span<const Golden>(kSaveNarrow)));
    }
}

TEST_CASE("save format: the sections of the chunk in a .GAM keep their bytes") {
    const auto buildings = make_buildings();
    for (const bool wide : {false, true}) {
        const core::Bytes bytes = sim::encode_extension(make_world(buildings, wide), buildings, nullptr, {});
        CHECK(matches(wide ? "kChunkWide" : "kChunkNarrow", std::span<const std::uint8_t>(bytes).subspan(kChunkHeader),
                      wide ? std::span<const Golden>(kChunkWide) : std::span<const Golden>(kChunkNarrow)));
    }
}

TEST_CASE("save format: every field read comes back where it was written") {
    const auto buildings = make_buildings();
    for (const bool wide : {false, true}) {
        // Own savegame: the whole world from the sections.
        const core::Bytes bytes = save(make_world(buildings, wide), buildings);
        std::vector<std::string> warnings;
        auto loaded = sim::read_game(bytes, buildings, &warnings);
        REQUIRE(loaded.ok());
        CHECK(warnings.empty());
        CHECK(save(*loaded, buildings) == bytes);

        // Chunk in a .GAM: applied to a world with the same islands and nothing else.
        const core::Bytes chunk = sim::encode_extension(make_world(buildings, wide), buildings, nullptr, {});
        sim::World bare(400, 300);
        add_islands(bare, buildings, wide);
        REQUIRE(sim::apply_extension(bare, buildings, chunk, {}).ok());
        CHECK(sim::encode_extension(bare, buildings, nullptr, {}) == chunk);
    }
}
