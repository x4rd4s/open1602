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

/// What a computer's supply runs go by (ai_logistics.cpp), for one ware in one city. Rates are 1/32 t
/// a minute, stocks 1/32 t, cover 1/256 minute.
struct Supply {
    static constexpr std::uint64_t kNever = UINT64_MAX;
    /// The minutes until a ship from a source `seconds` away is back with a ware: there and back,
    /// and a minute at each end for the loading.
    [[nodiscard]] static constexpr std::uint32_t resupply_minutes(std::uint32_t seconds) noexcept {
        return static_cast<std::uint32_t>((2 * std::uint64_t{seconds} + 120 + 59) / 60);
    }
    /// How long a stock lasts at what the city uses beyond what it makes; kNever when it makes
    /// enough or uses none.
    [[nodiscard]] static constexpr std::uint64_t cover(std::uint32_t stock, std::uint32_t used,
                                                       std::uint32_t made) noexcept {
        return used == 0 || made >= used ? kNever : std::uint64_t{stock} * 256 / (used - made);
    }
    /// The stock a run fills a city up to: what it uses beyond what it makes until the next run can
    /// be back, and what one run brings (`load`), so that a run does not arrive at the level the next
    /// one is due at; never beyond what its Kontor holds.
    [[nodiscard]] static constexpr std::uint32_t target(std::uint32_t used, std::uint32_t made, std::uint32_t minutes,
                                                        std::uint32_t load, std::uint32_t capacity) noexcept {
        const std::uint64_t net = used > made ? used - made : 0;
        const std::uint64_t want = net * minutes + load;
        return static_cast<std::uint32_t>(want < capacity ? want : capacity);
    }
    /// Whether a rise into a class needs a source for one of its goods: the city's use now (`use`)
    /// and what this rise adds (`added`, 0 while nobody lives in the class below) exceed what it makes.
    /// The next rises of the other classes do not count: asked a source for them, the gate of a
    /// class nobody could rise into yet stayed shut for the whole game.
    [[nodiscard]] static constexpr bool rise_needs_source(std::uint32_t use, std::uint32_t added,
                                                          std::uint32_t made) noexcept {
        return std::uint64_t{use} + added > made;
    }
    /// The stock of such a good the city holds before the rise: what it will use beyond what it makes
    /// until a ship from its source (`minutes` away there and back) could bring more.
    [[nodiscard]] static constexpr std::uint64_t rise_stock(std::uint32_t use, std::uint32_t added, std::uint32_t made,
                                                            std::uint32_t minutes) noexcept {
        const std::uint64_t after = std::uint64_t{use} + added;
        return after > made ? (after - made) * minutes : 0;
    }
    /// How much sooner the stock runs out than a run takes, 1/256 minute: below 0 a run is due.
    [[nodiscard]] static constexpr std::int64_t slack(std::uint64_t cover, std::uint32_t minutes) noexcept {
        const std::uint64_t c = cover < (std::uint64_t{1} << 62) ? cover : (std::uint64_t{1} << 62);
        return static_cast<std::int64_t>(c) - static_cast<std::int64_t>(std::uint64_t{minutes} * 256);
    }
};

}  // namespace o1602::sim::ai
