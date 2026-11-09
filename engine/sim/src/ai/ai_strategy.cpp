// SPDX-License-Identifier: GPL-3.0-or-later
//
// Open1602's strategy planner for the improved computers (not Rules::bugs.classic_ai): a character
// drawn once per game, and every half to whole minute a look at every rival, human or computer,
// which gives a stance towards each and the computer's goal for now. The goal and the character
// steer the army target, the gold a colony waits for and the peace offers among computers.
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#include "ai_engine.hpp"

namespace o1602::sim::ai {

namespace {

constexpr std::uint32_t kNearSeconds = 240;  // a rival whose city lies within four minutes at sea is near
constexpr std::int32_t kPartnerMood = 20;    // a friendlier mood makes a partner
constexpr std::int32_t kHostileMood = -60;   // an angrier one an enemy
constexpr std::uint32_t kMinStrategyMs = 10'000;
constexpr std::uint64_t kWarCheckMs = 600'000;   // a war plan is looked at every ten minutes
constexpr std::uint8_t kWarFails = 3;            // ... and given up after three looks without progress
constexpr std::uint64_t kWarRestMs = 1'800'000;  // half an hour of peace before the next war
constexpr std::uint64_t kGoalHoldMs = 600'000;   // a goal holds ten minutes, unless debt or a threat end it

const char* goal_name(std::uint8_t g) {
    switch (g) {
        case goal::kMilitary:
            return "military";
        case goal::kGold:
            return "gold";
        case goal::kLand:
            return "land";
        default:
            return "economy";
    }
}

}  // namespace

std::uint32_t Engine::strength(PlayerId owner) const {
    // The armies and the fleets' cannons, as stronger() compares them.
    const auto w = unit_weights();
    std::uint32_t cannons = 0;
    if (const Census* c = census(); c != nullptr && owner < c->players) {
        cannons = c->cannons[owner];
    } else {
        for (const Ship& s : m_world.ships()) {
            cannons += s.owner == owner && !s.sinking ? s.cannons : 0U;
        }
    }
    return weighted_army(owner) + cannons * w[3];
}

std::uint32_t Engine::threat_of(PlayerId owner) const {
    // On the harder levels a human counts as stronger than its army: the humans are the most
    // dangerous rivals. Nothing on the two easier levels.
    std::uint32_t s = strength(owner);
    const Player* pl = m_world.player(owner);
    if (pl != nullptr && pl->role == PlayerRole::Human && difficulty() >= 2) {
        const std::uint64_t pct = 100U + std::uint64_t{m_world.balance().ai_human_threat_percent} * (difficulty() - 1U);
        s = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(std::uint64_t{s} * pct / 100U, std::numeric_limits<std::uint32_t>::max()));
    }
    return s;
}

std::uint8_t Engine::stance_of(const AiPlayer& p, PlayerId other) const {
    return other < p.strategy.stances.size() ? p.strategy.stances[other] : stance::kNeutral;
}

std::int32_t Engine::colony_money(const AiPlayer& p) const {
    const std::uint8_t g = p.strategy.goal;
    if (rivals() && (g == goal::kLand || g == goal::kGold)) {
        return static_cast<std::int32_t>(std::min<std::uint32_t>(m_world.balance().ai_land_colony_money, 3000));
    }
    return 3000;
}

std::uint32_t Engine::army_percent(const AiPlayer& p) const {
    // The share of the rivals' armies and of its inhabitants' army a computer keeps: more while it
    // arms or as a conqueror, less as a trader. In debt it builds up its economy first.
    if (!rivals()) {
        return 100;
    }
    const Balance& bal = m_world.balance();
    std::uint32_t pct = 100;
    if (p.strategy.character == character::kConqueror) {
        pct = bal.ai_conqueror_army_percent;
    } else if (p.strategy.character == character::kTrader) {
        pct = bal.ai_trader_army_percent;
    }
    if (p.strategy.goal == goal::kMilitary) {
        pct = std::max(pct, bal.ai_military_army_percent);
    }
    return pct;
}

void Engine::plan_strategy(AiPlayer& p) {
    const Balance& bal = m_world.balance();
    Strategy& st = p.strategy;
    if (st.character == character::kNone) {
        // The characters are dealt out: one of those the fewest other computers have, so that four
        // computers are not all of a kind.
        std::array<std::uint32_t, character::kCount + 1> taken{};
        for (const AiPlayer& o : m_state.players) {
            if (&o != &p && !o.pirates) {
                ++taken[o.strategy.character];
            }
        }
        const std::uint32_t fewest = *std::min_element(taken.begin() + 1, taken.end());
        std::vector<std::uint8_t> open;
        for (std::uint8_t c = 1; c <= character::kCount; ++c) {
            if (taken[c] == fewest) {
                open.push_back(c);
            }
        }
        // Drawn from a copy of the computers' generator, which stays where it was: the games run as
        // before until the strategy acts.
        Random draw = rng();
        for (PlayerId i = 0; i <= p.player; ++i) {
            draw.next();
        }
        st.character = open[draw.below(static_cast<std::uint32_t>(open.size()))];
        log(p, "character %u", st.character);
    }
    // Faster on harder levels: a minute on the easiest, ten seconds less per level.
    const std::uint64_t level_ms = std::uint64_t{bal.ai_strategy_level_seconds} * 1000U * difficulty();
    const std::uint64_t every_ms = std::uint64_t{bal.ai_strategy_seconds} * 1000U;
    st.next_ms = now_ms() + std::max<std::uint64_t>(kMinStrategyMs, every_ms > level_ms ? every_ms - level_ms : 0);

    const std::size_t players = m_world.players().size();
    st.stances.resize(players, stance::kNeutral);
    const Diplomacy& d = m_world.diplomacy();
    const std::uint32_t own = strength(p.player);
    const City* home = p.home_valid ? m_world.city(p.home_island, p.home_city) : nullptr;
    const auto home_at = home != nullptr ? city_point(*home) : std::pair<std::int32_t, std::int32_t>{0, 0};
    // A conqueror presses earlier, a trader later; harder levels press earlier too.
    std::uint32_t press = bal.ai_pressure_percent;
    if (st.character == character::kConqueror) {
        press = std::max<std::uint32_t>(100, press - 25);
    } else if (st.character == character::kTrader) {
        press += 50;
    }
    press = std::max<std::uint32_t>(100, press - std::min<std::uint32_t>(press - 100, 10U * difficulty()));
    bool threat = false;
    bool pressing = false;
    for (std::size_t bi = 0; bi < players; ++bi) {
        const auto b = static_cast<PlayerId>(bi);
        const Player& pl = m_world.players()[bi];
        if (b == p.player || pl.status != PlayerStatus::Playing ||
            (pl.role != PlayerRole::Human && pl.role != PlayerRole::Computer) ||
            m_world.behaviour(pl) != FactionBehaviour::Settlers) {
            st.stances[bi] = stance::kNeutral;
            continue;
        }
        const Relation& rel = d.relation(p.player, b);
        const std::int32_t mood = d.mood(m_world, p.player, b, 0, now_ms());
        const std::uint32_t theirs = threat_of(b);
        bool near = false;
        if (home != nullptr) {
            for (const City& c : m_world.cities()) {
                if (c.owner == b &&
                    (c.island == home->island || sail_seconds(p, home_at, city_point(c)) <= kNearSeconds)) {
                    near = true;
                    break;
                }
            }
        }
        std::uint8_t s = stance::kNeutral;
        if (mood < kHostileMood) {
            s = stance::kHostile;
        } else if (mood > kPartnerMood ||
                   (rel.trade == Treaty::Signed && mood >= 0 && st.character == character::kTrader)) {
            s = stance::kPartner;
        } else if (near && mood <= 0 && theirs != 0 && std::uint64_t{own} * 100U >= std::uint64_t{theirs} * press) {
            // Only a rival it does not like: a friendly neighbour is left in peace.
            s = stance::kPressure;
        }
        if (s != st.stances[bi]) {
            log(p, "stance towards player %u: %u (mood %d, strength %u against %u)", b, s, mood, own, theirs);
        }
        st.stances[bi] = s;
        // An angry rival, or one at war with it, that is stronger and near is a threat; a peace
        // can end, but arming against every stronger neighbour at peace would set off a race.
        threat = threat || ((s == stance::kHostile || rel.peace == Treaty::None) && near && theirs > own);
        pressing = pressing || s == stance::kPressure;
    }

    // The goal: the economy first in debt; arms against a stronger enemy nearby or, as a conqueror,
    // against a weaker one; gold once the aristocrats are near without a gold mine; land as a
    // settler, or with gold to spare and few cities; else the economy.
    std::uint8_t g = goal::kEconomy;
    const std::int32_t money = player(p).money;
    bool aristocrats_near = false;
    for (const CityPlan& r : p.plans) {
        aristocrats_near = aristocrats_near || r.stage >= stage::kMerchants2;
    }
    // In debt, or losing gold with little left.
    const bool broke = money < 0 || (net_income(p) < 0 && money < 2000);
    war_plan(p, broke);
    if (broke) {
        g = goal::kEconomy;
    } else if (threat || st.war_target != kNoPlayer || st.war_arming) {
        g = goal::kMilitary;
    } else if (aristocrats_near && !p.present[kSlotGoldMine]) {
        g = goal::kGold;
    } else if (p.cities < city_limit() && (st.character == character::kSettler || (money >= 8000 && p.cities < 3)) &&
               p.max_stage >= stage::kMain2) {
        g = goal::kLand;
    }
    // A goal holds a while, so that the computer does not change its mind with every look; debt
    // and a threat change it at once.
    if (g != st.goal && !broke && g != goal::kMilitary && st.goal_ms != 0 && now_ms() - st.goal_ms < kGoalHoldMs) {
        return;
    }
    if (g != st.goal) {
        log(p, "goal: %s", goal_name(g));
        st.goal_ms = now_ms();
    }
    st.goal = g;
}

std::uint32_t Engine::war_measure(PlayerId target) const {
    // What a war takes from its target: its inhabitants, and a city counts as fifty more.
    return total_population(target) + 50U * own_city_count(target);
}

void Engine::end_war(AiPlayer& p, const char* why) {
    Strategy& st = p.strategy;
    const PlayerId b = st.war_target;
    log(p, "ends the war on player %u: %s", b, why);
    st.war_target = kNoPlayer;
    st.war_ended_ms = now_ms();
    // Ships on their way against it are called back.
    for (ShipNote& n : p.ships) {
        if (n.mission < 5 || n.mission > 9) {
            continue;
        }
        const City* c = city(static_cast<std::uint16_t>(n.param >> 8), static_cast<std::uint8_t>(n.param & 0xFF));
        const PlayerId against =
            n.mission <= 6 ? (c != nullptr ? c->owner : kNoPlayer) : static_cast<PlayerId>(n.param & 0xFF);
        if (against == b) {
            if (Ship* s = m_world.ship(n.id)) {
                mission_end(p, *s, n);
            }
        }
    }
    if (m_world.player(b) != nullptr && m_world.diplomacy().relation(p.player, b).peace == Treaty::None) {
        DiplomacyOrder o;
        o.player = p.player;
        o.partner = b;
        o.kind = DiplomacyOrder::Kind::Peace;
        o.value = Treaty::Offered;
        execute(o);
    }
}

void Engine::war_plan(AiPlayer& p, bool broke) {
    // A war is planned against a rival it presses: near, not liked, clearly weaker. A conqueror
    // wages one on any level, a settler from the third level on, a trader only on the hardest;
    // humans are spared on the easiest. Every ten minutes it looks whether the war gets anywhere
    // (the target lost a twentieth of its inhabitants and cities); after three looks without
    // progress, or in debt, it offers peace and keeps the peace for half an hour.
    Strategy& st = p.strategy;
    const std::uint8_t level = difficulty();
    if (st.war_target != kNoPlayer) {
        const Player* t = m_world.player(st.war_target);
        if (t == nullptr || t->status != PlayerStatus::Playing || own_city_count(st.war_target) == 0) {
            end_war(p, "the target is gone");
            return;
        }
        if (m_world.diplomacy().relation(p.player, st.war_target).peace == Treaty::Signed) {
            end_war(p, "peace was made");
            return;
        }
        if (broke) {
            end_war(p, "out of gold");
            return;
        }
        if (now_ms() >= st.war_check_ms) {
            const std::uint32_t m = war_measure(st.war_target);
            if (std::uint64_t{m} * 100U <= std::uint64_t{st.war_measure} * 95U) {
                st.war_fails = 0;
            } else {
                ++st.war_fails;
            }
            log(p, "war on player %u: measure %u (was %u), %u looks without progress", st.war_target, m, st.war_measure,
                st.war_fails);
            st.war_measure = m;
            st.war_check_ms = now_ms() + kWarCheckMs;
            if (st.war_fails >= kWarFails) {
                end_war(p, "no progress");
            }
        }
        return;
    }
    // First it arms (the goal military), from its main city on; the war comes once the army and
    // the gold are there.
    st.war_arming = false;
    const bool willing =
        st.character == character::kConqueror || (st.character == character::kSettler && level >= 2) || level >= 3;
    if (!willing || broke || p.max_stage < stage::kMain ||
        (st.war_ended_ms != 0 && now_ms() - st.war_ended_ms < kWarRestMs) ||
        player(p).scenario.has(scenario_flag::kPassive)) {
        return;
    }
    const Balance& bal = m_world.balance();
    const std::uint32_t own = strength(p.player);
    PlayerId best = kNoPlayer;
    std::uint64_t best_ratio = 0;
    for (std::size_t bi = 0; bi < st.stances.size(); ++bi) {
        const auto b = static_cast<PlayerId>(bi);
        const Player* pl = m_world.player(b);
        if (st.stances[bi] != stance::kPressure || pl == nullptr || (pl->role == PlayerRole::Human && level == 0)) {
            continue;
        }
        const std::uint64_t ratio = std::uint64_t{own} * 1000U / std::max<std::uint32_t>(1, threat_of(b));
        if (ratio > best_ratio) {
            best = b;
            best_ratio = ratio;
        }
    }
    if (best == kNoPlayer) {
        return;
    }
    // An army of soldiers, not the cannons of its ships: the war is won on land.
    const auto counts = army_counts(p.player);
    const std::uint32_t units = counts[0] + counts[1] + counts[2] + counts[3];
    if (units < bal.ai_war_units || player(p).money < static_cast<std::int64_t>(bal.ai_war_money)) {
        st.war_arming = true;
        return;
    }
    st.war_target = best;
    st.war_since_ms = now_ms();
    st.war_check_ms = now_ms() + kWarCheckMs;
    st.war_fails = 0;
    st.war_measure = war_measure(best);
    log(p, "declares war on player %u (war plan, %u soldiers, strength %u against %u)", best, units, own,
        threat_of(best));
    if (m_world.diplomacy().relation(p.player, best).peace != Treaty::None) {
        DiplomacyOrder o;
        o.player = p.player;
        o.partner = best;
        o.kind = DiplomacyOrder::Kind::Peace;
        o.value = Treaty::None;
        execute(o);
    }
}

}  // namespace o1602::sim::ai
