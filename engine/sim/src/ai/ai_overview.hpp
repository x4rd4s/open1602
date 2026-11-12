// SPDX-License-Identifier: GPL-3.0-or-later
//
// Improved computers: what a player reads in the statistics windows of their own cities (the
// goods balance, the stocks, the producers, the inhabitants and the finances), for one island or
// for all of them. Only the computer's own cities: nothing a player could not see of their own.
// And the budget its plans that cost gold share (Budget, ai_budget.cpp).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "o1602/sim/goods_ledger.hpp"

namespace o1602::sim::ai {

/// Gold a minute, as the cities' windows show it (the tab "Finanzen"), and the army's upkeep.
struct Finances {
    std::int64_t taxes = 0;
    std::int64_t costs = 0;  ///< running costs of the buildings
    std::int64_t sales = 0;
    std::int64_t purchases = 0;
    std::int64_t military = 0;  ///< the army's upkeep (only for all cities)
    [[nodiscard]] std::int64_t net() const noexcept { return taxes + sales - costs - purchases - military; }
};

/// The figures of some of a computer's cities: those on one island or all of them.
struct AreaView {
    std::optional<std::uint16_t> island;  ///< none: all its cities
    std::uint32_t cities = 0;
    std::uint32_t peopled = 0;  ///< cities with inhabitants
    std::uint32_t inhabitants = 0;
    std::array<std::uint32_t, kCityClasses> classes{};
    Finances now;  ///< the last closed minute
};

/// One good of some cities: its stock, its balance and its producers.
struct WareView {
    std::uint64_t stock = 0;    ///< 1/32 t
    std::uint32_t minutes = 0;  ///< minutes of ledger behind the flows (0: nothing to go by yet)
    /// Per minute over those minutes, 1/32 t: made (with what a full warehouse withheld), eaten by
    /// the inhabitants, asked for by them, and the balance of everything in and out.
    std::uint64_t made = 0;
    std::uint64_t eaten = 0;
    std::uint64_t wanted = 0;
    std::int64_t balance = 0;
    std::uint32_t producers = 0;
    std::uint32_t working = 0;
    std::uint32_t short_of_raw = 0;  ///< idle for their raw material
    std::uint32_t waiting = 0;       ///< idle for a full warehouse
    std::uint32_t off = 0;           ///< switched off
    /// Minutes the stock lasts at the balance; none while it does not fall.
    [[nodiscard]] std::optional<std::uint32_t> lasts() const noexcept;
    /// The stock `after` minutes at the balance (below 0: what will be missing).
    [[nodiscard]] std::int64_t ahead(std::uint32_t after) const noexcept;
    /// Percent of what the inhabitants ask for that they get (100 when they ask for nothing).
    [[nodiscard]] std::uint32_t met() const noexcept;
};

/// A computer's cities as the statistics windows show them.
struct Overview {
    AreaView all;
    std::vector<AreaView> islands;  ///< in island order
    [[nodiscard]] const AreaView* island(std::uint16_t i) const noexcept;
};

/// A plan that costs gold: what it pays now, what it costs a minute from now on, and what it brings
/// a minute once it delivers, `delay` minutes from now.
struct Commitment {
    std::int64_t cost = 0;
    std::int64_t upkeep = 0;
    std::int64_t value = 0;
    std::uint32_t delay = 0;
};

/// What a plan of the budget is for, in the log of its refusals.
enum class Spend : std::uint8_t { Building, Ship, Colony, RoomColony, GoalColony, Unit, Tower, War, Army };
inline constexpr std::size_t kSpends = 9;

/// Which check of the budget refuses a plan: none, the gold now, when it begins to pay, at the horizon.
enum class Refusal : std::uint8_t { None, Now, Delivery, Horizon };

/// What a computer's gold does, as its finances show it: what it has, what it set aside for plans
/// under way, what it gains or loses a minute with everything it buys and sells, and what it pays
/// out a minute without the trade it could do without.
struct Budget {
    std::int64_t cash = 0;
    std::int64_t reserved = 0;
    std::int64_t net = 0;       ///< taxes and sales less running costs, upkeep and every purchase
    std::int64_t outflow = 0;   ///< running costs, the army's upkeep and the inhabitants' goods bought
    std::int64_t reliable = 0;  ///< taxes of the cities whose inhabitants have their food
    std::uint32_t horizon = 0;  ///< minutes ahead
    std::uint32_t cushion_minutes = 0;
    /// The gold kept at all times: cushion_minutes of what the outflow takes beyond the reliable
    /// taxes, the risk that the net turns negative.
    [[nodiscard]] std::int64_t cushion() const noexcept;
    /// The gold left `minute` minutes from now with the plan made now.
    [[nodiscard]] std::int64_t at(const Commitment& c, std::uint32_t minute) const noexcept;
    /// Whether the gold stays at the cushion or above with the plan: now, when it begins to deliver
    /// and at the horizon. The gold runs in straight lines between those points, so its low point is
    /// one of them.
    [[nodiscard]] bool allows(const Commitment& c) const noexcept;
    /// The first check that refuses the plan.
    [[nodiscard]] Refusal refusal(const Commitment& c) const noexcept;
};

}  // namespace o1602::sim::ai
