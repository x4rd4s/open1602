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

#include "o1602/sim/consumption.hpp"
#include "o1602/sim/mission.hpp"

namespace o1602::sim::ai {

namespace {

constexpr std::uint32_t kNearSeconds = 240;  // a rival whose city lies within four minutes at sea is near
constexpr std::int32_t kPartnerMood = 20;    // a friendlier mood makes a partner
constexpr std::int32_t kHostileMood = -60;   // an angrier one an enemy
constexpr std::uint32_t kMinStrategyMs = 10'000;
constexpr std::uint64_t kWarCheckMs = 600'000;   // a war plan is looked at every ten minutes
constexpr std::uint8_t kWarFails = 3;            // ... and given up after three looks without progress
constexpr std::uint64_t kWarRestMs = 1'800'000;  // half an hour of peace before the next war
// Alliances: offered from this mood on (accepted from half of it), ended below kAllianceEndMood;
// two allies at most; an ally joins a war declared on its partner within half an hour.
constexpr std::int32_t kAllianceMood = 40;
constexpr std::int32_t kAllianceEndMood = -10;
constexpr std::uint32_t kMaxAllies = 2;
constexpr std::uint64_t kHelpMs = 1'800'000;
constexpr std::uint64_t kGoalHoldMs = 600'000;  // a goal holds ten minutes, unless debt or a threat end it
// Deeper in debt than kRestructureMoney, a city that saves kRestructureSaving a minute or more is
// given up, a quarter of an hour after the last.
constexpr std::int32_t kRestructureMoney = -5000;
constexpr std::int32_t kRestructureSaving = 50;
constexpr std::uint64_t kRestructurePauseMs = 900'000;
// The war economy of an improved computer: in peace its army may cost a third of what its cities
// earn with their trade (taxes less running costs, sales less purchases); at war, arming or under
// threat three quarters of it, and besides a share of its gold above a small reserve: what it
// pays for an hour and a half under threat, three hours in a war of its own, six while it arms.
// At war it lets its gold fall to this debt before it gives up.
constexpr std::int32_t kArmyPeaceShare = 33;
constexpr std::int32_t kArmyWarShare = 75;
constexpr std::int64_t kWarReserve = 1000;
constexpr std::int64_t kWarSupplyReserve = 5;      // improved computers: gold for a war, in kWarReserve
constexpr std::int64_t kPeaceRunwayMinutes = 720;  // in peace twelve hours of its gold
constexpr std::int64_t kThreatRunwayMinutes = 90;
constexpr std::int64_t kWarRunwayMinutes = 180;
constexpr std::int64_t kArmingRunwayMinutes = 360;
constexpr std::int64_t kWarDebt = -2000;

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

std::uint32_t Engine::strength(PlayerId viewer, PlayerId owner) const {
    // The armies and the fleets' cannons, as stronger() compares them.
    return seen_army(viewer, owner) + seen_cannons(viewer, owner) * unit_weights()[3];
}

std::uint32_t Engine::threat_of(PlayerId viewer, PlayerId owner) const {
    // On the harder levels a human counts as stronger than its army: the humans are the most
    // dangerous rivals. Nothing on the two easier levels.
    std::uint32_t s = strength(viewer, owner);
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

    if (player(p).money < 0) {
        // In debt: how each city fares, for the log only.
        for (const City& c : m_world.cities()) {
            if (c.owner != p.player) {
                continue;
            }
            std::string line;
            for (std::size_t k = 0; k < c.inhabitants.size(); ++k) {
                if (c.inhabitants[k] != 0) {
                    line += " class " + std::to_string(k) + ": " + std::to_string(c.inhabitants[k]) + " at " +
                            std::to_string(k < c.satisfaction.size() ? c.satisfaction[k] : 0) + "% tax " +
                            std::to_string(k < c.tax_rate.size() ? c.tax_rate[k] : 0) + ";";
                }
            }
            log(p, "in debt, city %u/%u:%s stock food %u cloth %u alcohol %u tobacco %u spices %u", c.island, c.index,
                line.c_str(), c.stock(ware::kFood) / kTon, c.stock(ware::kCloth) / kTon, c.stock(ware::kAlcohol) / kTon,
                c.stock(ware::kTobaccoGoods) / kTon, c.stock(ware::kSpices) / kTon);
        }
    }
    const std::size_t players = m_world.players().size();
    st.stances.resize(players, stance::kNeutral);
    const Diplomacy& d = m_world.diplomacy();
    const std::uint32_t own = strength(p.player, p.player);
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
        const std::uint32_t theirs = threat_of(p.player, b);
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
        const auto presses = [&](std::uint32_t t) {
            return near && mood <= 0 && t != 0 && std::uint64_t{own} * 100U >= std::uint64_t{t} * press;
        };
        std::uint8_t s = stance::kNeutral;
        if (d.allied(p.player, b)) {
            s = stance::kPartner;  // an ally is neither pressed nor fought
        } else if (mood < kHostileMood) {
            s = stance::kHostile;
        } else if (mood > kPartnerMood ||
                   (rel.trade == Treaty::Signed && mood >= 0 && st.character == character::kTrader)) {
            s = stance::kPartner;
        } else if (presses(theirs)) {
            // Only a rival it does not like: a friendly neighbour is left in peace.
            s = stance::kPressure;
        }
        if (s != st.stances[bi]) {
            log(p, "stance towards player %u: %u (mood %d, strength %u against %u)", b, s, mood, own, theirs);
        }
        if (logging() && !sees_all()) {
            // The measurement of the information contract: the stance and the threat the whole
            // census would give (b's own view of itself is the whole).
            const std::uint32_t all = threat_of(b, b);
            const bool press_seen = s == stance::kPressure;
            const bool press_all = (s == stance::kNeutral || s == stance::kPressure) && presses(all);
            const bool threat_seen = (s == stance::kHostile || rel.peace == Treaty::None) && near && theirs > own;
            const bool threat_all = (s == stance::kHostile || rel.peace == Treaty::None) && near && all > own;
            if (all != theirs && (press_seen != press_all || threat_seen != threat_all)) {
                log(p, "seen: player %u strength %u (census %u), pressure %d (census %d), threat %d (census %d)", b,
                    theirs, all, press_seen, press_all, threat_seen, threat_all);
            }
        }
        st.stances[bi] = s;
        // An angry rival, or one at war with it, that is stronger and near is a threat; a peace
        // can end, but arming against every stronger neighbour at peace would set off a race.
        threat = threat || ((s == stance::kHostile || rel.peace == Treaty::None) && near && theirs > own);
        pressing = pressing || s == stance::kPressure;
    }

    if (logging() && !sees_all()) {
        // The measurement of the information contract: the cannons and the army target the whole
        // census would give.
        for (std::size_t bi = 0; bi < players; ++bi) {
            const auto b = static_cast<PlayerId>(bi);
            const std::uint32_t seen = seen_cannons(p.player, b);
            const std::uint32_t all = seen_cannons(b, b);
            if (b != p.player && seen != all) {
                log(p, "seen: player %u cannons %u (census %u)", b, seen, all);
            }
        }
        if (const std::uint32_t seen = army_target(p), all = army_target(p, true); seen != all) {
            log(p, "seen: army target %u (census %u)", seen, all);
        }
        std::uint32_t mounted = 0;
        std::uint32_t most = 0;
        own_cannons(p.player, mounted, most);
        log(p, "seen: own warships carry %u of %u cannons, %zu ships seen in a fight", mounted, most,
            m_state.sightings.size());
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
    // In debt, or losing gold with little left. Open1602: a war that gets somewhere, or a threat,
    // is worth a deficit for a while and a small debt (war_economy).
    const bool broke = war_economy(p) ? money < kWarDebt : money < 0 || (net_income(p) < 0 && money < 2000);
    war_plan(p, broke);
    alliances(p, st.next_ms - now_ms(), broke);
    if (broke) {
        restructure(p);
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

void Engine::restructure(AiPlayer& p) {
    // Open1602: an improved computer deep in debt and still losing gold gives up a city whose
    // running costs are well above its taxes: its buildings cost whether anybody lives there or not,
    // and a colony that cannot pay for itself drags the whole economy down for good. Never the home
    // city or its largest one, nor a city that makes what another of its cities lacks; only one
    // whose loss covers a good part of the deficit, and one at a time with a pause between, so that
    // the last one shows in the income first.
    if (!rivals() || p.pirates || player(p).money >= kRestructureMoney) {
        return;
    }
    const std::int32_t deficit = -net_income(p);
    if (deficit <= 0) {
        return;
    }
    const std::uint64_t now = now_ms();
    for (const auto& [island, at] : p.left_islands) {
        if (at <= now && now - at < kRestructurePauseMs) {
            return;
        }
    }
    for (const CityPlan& r : p.plans) {
        if ((r.flags & city_flag::kAbandon) != 0) {
            return;  // one is being given up already
        }
    }
    const City* home = home_city(p);
    const City* largest = nullptr;
    std::vector<const City*> own;
    for (const City& c : m_world.cities()) {
        if (c.owner != p.player) {
            continue;
        }
        own.push_back(&c);
        if (largest == nullptr || population(c) > population(*largest)) {
            largest = &c;
        }
    }
    // Whether city c makes a ware that another inhabited city of the computer wants and lacks.
    const auto& classes = m_buildings.resident_classes();
    const std::uint8_t food = m_buildings.food_ware();
    const auto supplies = [&](const City& c) {
        for (const City* o : own) {
            if (o == &c || population(*o) == 0) {
                continue;
            }
            std::vector<std::uint8_t> wares{food};
            for (std::size_t k = 0; k < classes.size() && k < o->inhabitants.size(); ++k) {
                if (o->inhabitants[k] != 0) {
                    for (const assets::ResidentClass::Need& n : classes[k].needs) {
                        wares.push_back(n.ware);
                    }
                }
            }
            for (const std::uint8_t w : wares) {
                if (supply(*o, w) < demand(*o, w) && supply(c, w) > 0) {
                    return true;
                }
            }
        }
        return false;
    };
    const std::vector<std::int32_t> costs = running_costs(m_world, m_buildings);
    const std::int32_t enough = std::max(kRestructureSaving, deficit / 4);
    CityPlan* worst = nullptr;
    std::int32_t worst_net = 0;
    for (CityPlan& r : p.plans) {
        const City* c = m_world.city(r.island, r.city);
        if (c == nullptr || c == home || c == largest || c->owner != p.player) {
            continue;
        }
        const auto i = static_cast<std::size_t>(c - m_world.cities().data());
        const std::int32_t net = tax_income(*c, m_buildings) - costs[i];
        if (-net >= enough && net < worst_net && !supplies(*c)) {
            worst = &r;
            worst_net = net;
        }
    }
    if (worst == nullptr) {
        return;
    }
    worst->flags |= city_flag::kAbandon;
    p.left_islands.emplace_back(worst->island, std::max<std::uint64_t>(now, 1));
    log(p, "in debt (gold %d, income %d): gives up city %u/%u, which costs %d a minute", player(p).money, -deficit,
        worst->island, worst->city, -worst_net);
}

bool Engine::war_economy(const AiPlayer& p) const {
    // A war of its own that has not failed a look yet, or a stronger enemy nearby (the goal
    // military without a war plan or arming for one).
    if (!rivals() || p.pirates || military_cost(p) == 0) {
        return false;
    }
    const Strategy& st = p.strategy;
    if (st.war_target != kNoPlayer) {
        return st.war_fails == 0;
    }
    return st.goal == goal::kMilitary && !st.war_arming;
}

std::int32_t Engine::army_budget(const AiPlayer& p) const {
    const Strategy& st = p.strategy;
    if (st.war_arming && st.war_target == kNoPlayer) {
        return arming_budget(p);
    }
    const std::int64_t earned = std::max<std::int64_t>(0, std::int64_t{civil_income(p)} + trade_balance(player(p)));
    if (st.war_target == kNoPlayer && st.goal != goal::kMilitary) {
        const std::int64_t saved = std::max<std::int64_t>(0, player(p).money - kWarReserve) / kPeaceRunwayMinutes;
        return static_cast<std::int32_t>(
            std::min<std::int64_t>(earned * kArmyPeaceShare / 100 + saved, std::numeric_limits<std::int32_t>::max()));
    }
    const std::int64_t civil = earned * kArmyWarShare / 100;
    const std::int64_t minutes = st.war_target != kNoPlayer ? kWarRunwayMinutes : kThreatRunwayMinutes;
    const std::int64_t runway = std::max<std::int64_t>(0, player(p).money - kWarReserve) / minutes;
    return static_cast<std::int32_t>(std::min<std::int64_t>(civil + runway, std::numeric_limits<std::int32_t>::max()));
}

std::int32_t Engine::arming_budget(const AiPlayer& p) const {
    // Arming for a war of its own: its civil income and a small share of its gold, as the war is
    // not won yet.
    const std::int64_t earned = std::max<std::int64_t>(0, std::int64_t{civil_income(p)} + trade_balance(player(p)));
    const std::int64_t civil = earned * kArmyWarShare / 100;
    const std::int64_t runway = std::max<std::int64_t>(0, player(p).money - kWarReserve) / kArmingRunwayMinutes;
    return static_cast<std::int32_t>(std::min<std::int64_t>(civil + runway, std::numeric_limits<std::int32_t>::max()));
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
    if (!willing || broke || p.max_stage < stage::kMain || !war_supplied(p) ||
        (st.war_ended_ms != 0 && now_ms() - st.war_ended_ms < kWarRestMs) ||
        player(p).scenario.has(scenario_flag::kPassive)) {
        return;
    }
    const Balance& bal = m_world.balance();
    const std::uint32_t own = strength(p.player, p.player);
    PlayerId best = kNoPlayer;
    std::uint64_t best_ratio = 0;
    for (std::size_t bi = 0; bi < st.stances.size(); ++bi) {
        const auto b = static_cast<PlayerId>(bi);
        const Player* pl = m_world.player(b);
        if (st.stances[bi] != stance::kPressure || pl == nullptr || (pl->role == PlayerRole::Human && level == 0)) {
            continue;
        }
        const std::uint64_t ratio = std::uint64_t{own} * 1000U / std::max<std::uint32_t>(1, threat_of(p.player, b));
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
        // Open1602: only if its economy carries the army it needs, with the gold it may spend on it.
        if (pays_upkeep(m_world, player(p))) {
            const std::int64_t per_unit = unit_upkeep_estimate(p);
            if (per_unit * std::max<std::uint32_t>(units, bal.ai_war_units) > arming_budget(p)) {
                return;
            }
        }
        st.war_arming = true;
        return;
    }
    // Open1602: only with an economy that carries the army at war: cities that earn more than they
    // cost, and the army's upkeep within what the war may cost.
    if (pays_upkeep(m_world, player(p)) && (civil_income(p) < 0 || military_cost(p) > arming_budget(p))) {
        return;
    }
    log(p, "declares war on player %u (war plan, %u soldiers, strength %u against %u)", best, units, own,
        threat_of(p.player, best));
    start_war(p, best);
}

bool Engine::war_supplied(AiPlayer& p) {
    // Open1602: an improved computer starts or joins a war only while its own supply holds: its
    // cities make the alcohol and cloth they use, its home city grows the food it eats, and it
    // keeps five times its war reserve in gold. Of the computers that fell into debt on their own
    // in long games, nearly half had started wars with their supply already failing.
    if (!rivals()) {
        return true;
    }
    const City* home = home_city(p);
    if (home == nullptr) {
        return false;
    }
    const std::uint8_t food = m_buildings.food_ware();
    return critical_short(p.player, ware::kAlcohol) == 0 && critical_short(p.player, ware::kCloth) == 0 &&
           supply(*home, food) >= demand(*home, food) && player(p).money >= kWarReserve * kWarSupplyReserve;
}

void Engine::start_war(AiPlayer& p, PlayerId target) {
    Strategy& st = p.strategy;
    st.war_target = target;
    st.war_since_ms = now_ms();
    st.war_check_ms = now_ms() + kWarCheckMs;
    st.war_fails = 0;
    st.war_measure = war_measure(target);
    if (m_world.diplomacy().relation(p.player, target).peace != Treaty::None) {
        DiplomacyOrder o;
        o.player = p.player;
        o.partner = target;
        o.kind = DiplomacyOrder::Kind::Peace;
        o.value = Treaty::None;
        execute(o);
    }
}

std::string Engine::event_text(PlayerId a, PlayerId b) const {
    // For the log: what a remembers of b, as type:value pairs.
    std::string out;
    const std::uint64_t now = now_ms();
    for (const DiplomacyEvent& e : m_world.diplomacy().events(a)) {
        if (e.type == DiplomacyEventType::None || e.partner != b) {
            continue;
        }
        const std::int32_t v = diplomacy_event_value(e.type, now >= e.time_ms ? now - e.time_ms : 0);
        out += (out.empty() ? "" : " ") + std::to_string(static_cast<int>(e.type)) + ":" + std::to_string(v);
    }
    return out.empty() ? "none" : out;
}

void Engine::alliances(AiPlayer& p, std::uint64_t every_ms, bool broke) {
    // Alliances with a partner it likes well, two at most; one ends when the mood falls. Attacked,
    // it asks its allies for help; an ally joins a war declared on its partner unless it fights
    // one already, is short of gold or is allied with the enemy too.
    Strategy& st = p.strategy;
    const Diplomacy& d = m_world.diplomacy();
    const std::uint64_t now = now_ms();
    const auto order = [&](PlayerId b, DiplomacyOrder::Kind kind, Treaty value, PlayerId target) {
        DiplomacyOrder o;
        o.player = p.player;
        o.partner = b;
        o.kind = kind;
        o.value = value;
        o.target = target;
        execute(o);
    };
    const auto settler = [&](PlayerId b) {
        const Player* pl = m_world.player(b);
        return pl != nullptr && pl->status == PlayerStatus::Playing &&
               (pl->role == PlayerRole::Human || pl->role == PlayerRole::Computer) &&
               m_world.behaviour(*pl) == FactionBehaviour::Settlers;
    };
    // Whether x declared war on a within `within` and the war goes on.
    const auto declared_on = [&](PlayerId a, PlayerId x, std::uint64_t within) {
        if (d.relation(a, x).peace != Treaty::None) {
            return false;
        }
        for (const DiplomacyEvent& e : d.events(a)) {
            if (e.type == DiplomacyEventType::WarDeclared && e.partner == x && e.time_ms <= now &&
                now - e.time_ms < within) {
                return true;
            }
        }
        return false;
    };
    const bool resting = st.war_ended_ms != 0 && now - st.war_ended_ms < kWarRestMs;
    const std::size_t players = m_world.players().size();
    std::uint32_t allies = 0;
    for (std::size_t bi = 0; bi < players; ++bi) {
        allies += d.allied(p.player, static_cast<PlayerId>(bi)) ? 1U : 0U;
    }
    for (std::size_t bi = 0; bi < players; ++bi) {
        const auto b = static_cast<PlayerId>(bi);
        if (b == p.player || !settler(b)) {
            continue;
        }
        const Relation& rel = d.relation(p.player, b);
        const std::int32_t mood = d.mood(m_world, p.player, b, 0, now);
        switch (rel.alliance) {
            case Treaty::Signed:
                if (mood < kAllianceEndMood || st.war_target == b) {
                    log(p, "ends the alliance with player %u (mood %d, damage %u, tribute %u, events %s)", b, mood,
                        rel.damage, rel.tribute, event_text(p.player, b).c_str());
                    order(b, DiplomacyOrder::Kind::Alliance, Treaty::None, kNoPlayer);
                    --allies;
                }
                break;
            case Treaty::Offered:
                if (mood < kAllianceMood / 2) {
                    order(b, DiplomacyOrder::Kind::Alliance, Treaty::None, kNoPlayer);  // withdrawn
                }
                break;
            case Treaty::Received:
                if (allies < kMaxAllies && mood >= kAllianceMood / 2 && st.war_target != b) {
                    log(p, "accepts an alliance with player %u (mood %d)", b, mood);
                    order(b, DiplomacyOrder::Kind::Alliance, Treaty::Signed, kNoPlayer);
                    ++allies;
                }
                break;
            case Treaty::None:
                if (allies < kMaxAllies && stance_of(p, b) == stance::kPartner && mood >= kAllianceMood &&
                    rel.peace == Treaty::Signed && st.war_target != b) {
                    log(p, "offers an alliance to player %u (mood %d)", b, mood);
                    order(b, DiplomacyOrder::Kind::Alliance, Treaty::Offered, kNoPlayer);
                }
                break;
        }
    }
    for (std::size_t ai = 0; ai < players; ++ai) {
        const auto a = static_cast<PlayerId>(ai);
        if (!d.allied(p.player, a)) {
            continue;
        }
        for (std::size_t xi = 0; xi < players; ++xi) {
            const auto x = static_cast<PlayerId>(xi);
            if (x == p.player || x == a || !settler(x)) {
                continue;
            }
            // Attacked since its last look: it asks each ally once.
            if (declared_on(p.player, x, every_ms)) {
                log(p, "asks its ally %u for help against player %u", a, x);
                order(a, DiplomacyOrder::Kind::AskHelp, Treaty::None, x);
            }
            // Its ally was attacked within the last half hour: it joins that war, unless it rests
            // after a war of its own (as before a war plan).
            if (st.war_target == kNoPlayer && !broke && !resting && !d.allied(p.player, x) &&
                d.relation(p.player, x).peace != Treaty::None && declared_on(a, x, kHelpMs) && war_supplied(p)) {
                log(p, "joins its ally %u in the war on player %u", a, x);
                start_war(p, x);
            }
        }
    }
}

}  // namespace o1602::sim::ai
