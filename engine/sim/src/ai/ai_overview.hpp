// SPDX-License-Identifier: GPL-3.0-or-later
//
// Improved computers: what a player reads in the statistics windows of their own cities (the
// goods balance, the stocks, the producers, the inhabitants and the finances), for one island or
// for all of them, and where those figures lead within the next hour at the trend they show.
// Only the computer's own cities: nothing a player could not see of their own.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "o1602/sim/goods_ledger.hpp"

namespace o1602::sim::ai {

/// Minutes the goods balance is read over (the window's short view).
inline constexpr std::uint32_t kViewMinutes = 15;
/// Minutes a decision looks ahead.
inline constexpr std::uint32_t kForecastMinutes = 60;
/// Minutes of history from which the finances show a trend (an hour's oldest quarter).
inline constexpr std::uint32_t kTrendMinutes = 45;

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
    Finances now;
    /// The average minute of the oldest quarter of the last hour; `now` while the cities have less
    /// than kTrendMinutes of history.
    Finances before;
    std::uint32_t history = 0;  ///< minutes of history behind `before`
    /// The net gold a minute `minutes` ahead, at the change over the last hour.
    [[nodiscard]] std::int64_t net_ahead(std::uint32_t minutes) const noexcept;
    /// The worse of the net now and `minutes` ahead: what a decision may count on.
    [[nodiscard]] std::int64_t net_safe(std::uint32_t minutes) const noexcept;
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

}  // namespace o1602::sim::ai
