// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's settings and clocks: RULE, WRLD and EPOC.
#include <algorithm>
#include <array>
#include <utility>

#include "section_io.hpp"

#include "o1602/sim/simulation.hpp"

namespace o1602::sim::save {

namespace {

// Keys of the rules (RULE): a key per setting, so settings can be added and old files keep theirs.
enum RuleKey : std::uint16_t {
    kRefund = 1,
    kRefundPercent = 2,
    kMaxShips = 3,
    kGuaranteeResources = 4,
    kMaxGameSpeed = 5,
    kDroughts = 6,
    kVolcanoRule = 7,
    kTraderPrices = 8,
    kMaxArmy = 9,
};

template <class A, class R>
void io_random(A& a, R& r) {
    std::uint64_t state = r.state();
    std::uint64_t increment = r.increment();
    a.u64(state);
    a.u64(increment);
    if constexpr (A::kReading) {
        r = Random::from_state(state, increment);
    }
}

template <class A, class W>
void io_world(A& a, W& w) {
    a.u64(w.time_ms);
    if constexpr (A::kReading) {
        if (w.time_ms % Simulation::kTickMs != 0) {
            a.fail();  // game time moves in ticks of 50 ms; the 5 s steps (diplomacy, players) rely on it
        }
    }
    a.u32(w.city_name_index);
    a.u8(w.settings.difficulty);
    a.u32(w.settings.flags);
    a.u64(w.next_drought_ms);
    // The drought seed: whether there is one, and its value (0 without one).
    bool seeded = w.drought_seed.has_value();
    std::uint8_t seed = w.drought_seed.value_or(0);
    a.boolean(seeded);
    a.u8(seed);
    if constexpr (A::kReading) {
        if (seeded) {
            w.drought_seed = seed;
        }
    }
    a.list(w.randoms, 16, [&](auto& r) { io_random(a, r); });
    for (std::size_t g = 0; g < HouseClocks::kGroups; ++g) {
        a.u32(w.house_clocks.time_ms[g]);
        a.u8(w.house_clocks.epoch[g]);
    }
    for (std::size_t c = 0; c < GrowthState::kClasses; ++c) {
        a.u32(w.growth_ms[c]);
        a.u8(w.growth_counter[c]);
    }
    a.u32(w.next_disaster_id);
}

template <class A, class E>
void io_epochs(A& a, E& e) {
    a.u8_masked(e.phase, 7U);
    a.list(e.islands, 2, [&](auto& item) {
        a.u8(item.first);  // island number
        a.u8_masked(item.second, 7U);
    });
}

}  // namespace

// RULE is a list of keys and values rather than a record: the reader keeps a setting of a later
// version out (unknown key) and clamps the values into what the setting allows.
void write_rules(Writer& out, const Rules& r) {
    const std::array<std::pair<RuleKey, std::uint32_t>, 9> values = {{
        {kRefund, static_cast<std::uint32_t>(r.demolish_refund)},
        {kRefundPercent, r.refund_percent},
        {kMaxShips, r.max_ships_per_player},
        {kGuaranteeResources, r.guarantee_resources ? 1U : 0U},
        {kMaxGameSpeed, r.max_game_speed},
        {kDroughts, static_cast<std::uint32_t>(r.droughts)},
        {kVolcanoRule, static_cast<std::uint32_t>(r.volcanoes)},
        {kTraderPrices, static_cast<std::uint32_t>(r.trader_prices)},
        {kMaxArmy, r.max_army},
    }};
    out.list(values, 6, [&](const auto& item) {
        out.u16(item.first);
        out.u32(item.second);
    });
}

Rules read_rules(Reader& in) {
    Rules r;
    const std::uint32_t n = in.in().count(6);
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint16_t key = in.in().u16();
        const std::uint32_t v = in.in().u32();
        switch (key) {
            case kRefund:
                r.demolish_refund = static_cast<Refund>(std::min<std::uint32_t>(v, 2));
                break;
            case kRefundPercent:
                r.refund_percent = static_cast<std::uint8_t>(std::min<std::uint32_t>(v, 100));
                break;
            case kMaxShips:
                r.max_ships_per_player = v;
                break;
            case kGuaranteeResources:
                r.guarantee_resources = v != 0;
                break;
            case kMaxGameSpeed:
                r.max_game_speed = v;
                break;
            case kDroughts:
                r.droughts = static_cast<Droughts>(std::min<std::uint32_t>(v, 4));
                break;
            case kVolcanoRule:
                r.volcanoes = static_cast<Volcanoes>(std::min<std::uint32_t>(v, 2));
                break;
            case kTraderPrices:
                r.trader_prices = static_cast<TraderPrices>(std::min<std::uint32_t>(v, 1));
                break;
            case kMaxArmy:
                r.max_army = v;
                break;
            default:
                break;  // a setting of a later version
        }
    }
    return r;
}

void write_world(Writer& out, const World& w) {
    Decoded::WorldPart part;
    part.time_ms = w.time_ms();
    part.city_name_index = w.city_name_index();
    part.settings = w.game_settings();
    part.next_drought_ms = w.next_drought_ms();
    part.drought_seed = w.drought_seed();
    for (std::size_t s = 0; s < static_cast<std::size_t>(RandomStream::Count); ++s) {
        part.randoms.push_back(w.random(static_cast<RandomStream>(s)));
    }
    part.house_clocks = w.house_clocks();
    part.growth_ms = w.growth().time_ms;
    part.growth_counter = w.growth().counter;
    part.next_disaster_id = w.peek_disaster_id();
    io_world(out, std::as_const(part));
}

Decoded::WorldPart read_world(Reader& in) {
    Decoded::WorldPart part;
    io_world(in, part);
    return part;
}

void write_epochs(Writer& out, const World& w) {
    Decoded::EpochPart part;
    part.phase = w.island_phase();
    for (const WorldIsland& i : w.islands()) {
        part.islands.emplace_back(i.header.number, i.last_phase);
    }
    io_epochs(out, std::as_const(part));
}

Decoded::EpochPart read_epochs(Reader& in) {
    Decoded::EpochPart part;
    io_epochs(in, part);
    return part;
}

}  // namespace o1602::sim::save
