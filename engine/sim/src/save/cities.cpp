// SPDX-License-Identifier: GPL-3.0-or-later
//
// The cities and their buildings: CITY, CSTA (trade statistics), PROD (the production list with its
// free slots), HOUS, GROW and COST (shipyards and castles).
#include <utility>

#include "section_io.hpp"

namespace o1602::sim::save {

namespace {

template <class A, class C>
void io_city(A& a, C& c) {
    a.u16(c.island);
    a.u8(c.index);
    a.u8(c.owner);
    a.string(c.name);
    a.u32s(c.goods);
    // The warehouse: whether the city has one, and its position (0, 0 without one).
    bool store = c.warehouse_x.has_value() && c.warehouse_y.has_value();
    std::uint16_t x = c.warehouse_x.value_or(0);
    std::uint16_t y = c.warehouse_y.value_or(0);
    a.boolean(store);
    a.u16(x);
    a.u16(y);
    if constexpr (A::kReading) {
        if (store) {
            c.warehouse_x = x;
            c.warehouse_y = y;
        }
    }
    a.u32s(c.reserved);
    a.u32s(c.incoming);
    a.u32(c.capacity);
    a.u32s(c.inhabitants);
    a.u8s(c.satisfaction);
    a.u8s(c.tax_rate);
    a.list(c.supply, 20, [&](auto& s) {
        a.u64(s.demand);
        a.u64(s.supplied);
        a.u8(s.ratio);
        for (auto& h : s.history) {
            a.u8(h);
        }
    });
    a.u8(c.food_ratio);
    a.u16(c.satisfaction_bonus);
    a.list(c.upgrade_queue, 8, [&](auto& u) {
        a.u32(u.inhabitants);
        a.u16(u.x);
        a.u16(u.y);
    });
    a.u8(c.epoch);
    a.boolean(c.material_lock);
    a.u8(c.worst_good);
    a.u64(c.next_event_ms);
}

/// CSTA: the statistics of a City (writing) or of Decoded::CityStats (reading), which has the same
/// names.
template <class A, class C>
void io_city_stats(A& a, C& c) {
    a.u16(c.island);
    a.u8(c.index);
    a.i64(c.purchases);
    a.i64(c.sales);
}

template <class A, class P>
void io_production(A& a, P& p) {
    a.u16(p.island);
    a.u16(p.x);
    a.u16(p.y);
    a.boolean(p.used);
    a.boolean(p.enabled);
    a.u8(p.rate);
    a.u8(p.anim_phase);
    a.u8(p.idle_cycles);
    a.u8(p.flags);
    a.u16(p.countdown);
    a.u16(p.raw);
    a.u16(p.raw2);
    a.u16(p.stock);
    a.u16(p.produced);
    a.u16(p.elapsed);
    a.u16(p.reserved);
    if constexpr (A::kReading) {
        if (!p.used) {
            p.enabled = false;  // a free slot never runs (World::remove_production switches it off)
        }
    }
}

/// The production list and its free slots; a free slot must name an unused entry.
template <class A, class L, class F>
void io_productions(A& a, L& list, F& free) {
    a.list(list, 26, [&](auto& p) { io_production(a, p); });
    a.u32s(free);
    if constexpr (A::kReading) {
        for (const std::uint32_t f : free) {
            if (f >= list.size() || list[f].used) {
                a.fail();
            }
        }
    }
}

template <class A, class H>
void io_house(A& a, H& h) {
    a.u16(h.island);
    a.u16(h.x);
    a.u16(h.y);
    a.u8(h.city);
    a.u8(h.resident_class);
    a.u32(h.inhabitants64);
    a.u8(h.timer_group);
    a.u8(h.epoch);
    a.u8(h.queue_epoch);
    a.u8(h.market_distance);
    a.boolean(h.market);
    a.boolean(h.services_complete);
    a.u16(h.services);
}

template <class A, class E>
void io_growth(A& a, E& e) {
    a.u16(e.island);
    a.u16(e.x);
    a.u16(e.y);
    a.u8(e.growth_class);
    a.u8(e.seen);
    a.u8(e.stage);
}

/// COST holds where the buildings stand; FLEE and ARMY hold their state in the same order.
template <class A, class B>
void io_cost_building(A& a, B& b) {
    a.u8(b.kind);
    a.u16(b.island);
    a.u16(b.x);
    a.u16(b.y);
    a.boolean(b.enabled);
}

}  // namespace

void write_cities(Writer& out, const World& w) {
    out.list(w.cities(), 16, [&](const City& c) { io_city(out, c); });
}

std::vector<City> read_cities(Reader& in) {
    std::vector<City> list;
    in.list(list, 16, [&](City& c) { io_city(in, c); });
    return list;
}

void write_city_state(Writer& out, const World& w) {
    out.list(w.cities(), 19, [&](const City& c) { io_city_stats(out, c); });
}

std::vector<Decoded::CityStats> read_city_state(Reader& in) {
    std::vector<Decoded::CityStats> list;
    in.list(list, 19, [&](Decoded::CityStats& c) { io_city_stats(in, c); });
    return list;
}

void write_productions(Writer& out, const World& w) {
    io_productions(out, w.productions(), w.free_productions());
}

Decoded::ProductionPart read_productions(Reader& in) {
    Decoded::ProductionPart part;
    io_productions(in, part.list, part.free);
    return part;
}

void write_houses(Writer& out, const World& w) {
    out.list(w.houses(), 20, [&](const House& h) { io_house(out, h); });
}

std::vector<House> read_houses(Reader& in) {
    std::vector<House> list;
    in.list(list, 20, [&](House& h) { io_house(in, h); });
    return list;
}

void write_growth(Writer& out, const World& w) {
    out.list(w.growth().entries, 9, [&](const GrowthEntry& e) { io_growth(out, e); });
}

std::vector<GrowthEntry> read_growth(Reader& in) {
    std::vector<GrowthEntry> list;
    in.list(list, 9, [&](GrowthEntry& e) { io_growth(in, e); });
    return list;
}

void write_costs(Writer& out, const World& w) {
    out.list(w.cost_buildings(), 8, [&](const CostBuilding& b) { io_cost_building(out, b); });
}

std::vector<CostBuilding> read_costs(Reader& in) {
    std::vector<CostBuilding> list;
    in.list(list, 8, [&](CostBuilding& b) { io_cost_building(in, b); });
    return list;
}

}  // namespace o1602::sim::save
