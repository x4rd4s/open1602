// SPDX-License-Identifier: GPL-3.0-or-later
// The improved computers' budget (ai_overview.hpp, ai_budget.cpp): a plan that costs gold passes
// only while the gold, less what is set aside, stays at the cushion now, when the plan begins to
// pay and at the horizon.

#include "../../engine/sim/src/ai/ai_overview.hpp"
#include "../test_framework.hpp"

namespace o1602::sim::ai {
namespace {

Budget budget(std::int64_t cash, std::int64_t net, std::int64_t outflow) {
    Budget b;
    b.cash = cash;
    b.net = net;
    b.outflow = outflow;
    b.horizon = 30;
    b.cushion_minutes = 5;
    return b;
}

}  // namespace

TEST_CASE("ai budget: the cushion is minutes of what the computer pays out, never less than nothing") {
    CHECK(budget(0, 0, 200).cushion() == 1000);
    CHECK(budget(0, 0, -50).cushion() == 0);
    // A computer that buys nothing still keeps its running costs and upkeep.
    CHECK(budget(0, 100, 300).cushion() == 1500);
}

TEST_CASE("ai budget: the cushion covers only what the outflow takes beyond the reliable taxes") {
    Budget b = budget(0, 100, 300);
    b.reliable = 200;
    CHECK(b.cushion() == 500);
    // A city whose taxes pay all it pays out keeps no cushion.
    b.reliable = 400;
    CHECK(b.cushion() == 0);
}

TEST_CASE("ai budget: a building is paid now and costs its running costs over the horizon") {
    Commitment church;
    church.cost = 2000;
    church.upkeep = 40;
    // 2500 less 2000 leaves 500, less than five minutes of 200: refused now.
    CHECK(!budget(2500, 100, 200).allows(church));
    // 4000 less 2000 leaves 2000 now; 30 minutes at 100 - 40 add 1800.
    CHECK(budget(4000, 100, 200).allows(church));
    // A computer that loses 50 a minute with all it buys: 3200 - 2000 - 30 * 90 is below 1000.
    CHECK(!budget(3200, -50, 200).allows(church));
}

TEST_CASE("ai budget: what is set aside for a founding under way is not spent twice") {
    Commitment ship;
    ship.cost = 1500;
    Budget b = budget(5000, 50, 100);
    CHECK(b.allows(ship));
    b.reserved = 3200;
    CHECK(!b.allows(ship));
}

TEST_CASE("ai budget: a colony that lives on purchases with its gold near nothing builds nothing costly") {
    // The spiral of a long game: the colony's goods are bought, the gold is near 0, and a costly
    // building would leave nothing for the purchases.
    Commitment yard;
    yard.cost = 900;
    yard.upkeep = 25;
    CHECK(!budget(1200, -10, 180).allows(yard));
}

TEST_CASE("ai budget: a colony that opens a class is founded on what it brings once it delivers") {
    Commitment colony;
    colony.cost = 3000;
    colony.upkeep = 120;
    colony.delay = 15;
    // Earning 30 a minute, the colony's costs alone take the gold below the cushion at the horizon.
    const Budget b = budget(6000, 30, 150);
    CHECK(!b.allows(colony));
    // The taxes of the inhabitants it lets rise carry it.
    colony.value = 200;
    CHECK(b.allows(colony));
}

TEST_CASE("ai budget: the low point is checked where the plan begins to pay, not only now and at the end") {
    Commitment colony;
    colony.cost = 1000;
    colony.upkeep = 300;
    colony.value = 1000;
    colony.delay = 20;
    const Budget b = budget(6000, 0, 0);
    // Now 5000, at minute 20 5000 - 6000 < 0, at minute 30 -1000 + 10 * 700 = 6000.
    CHECK(b.at(colony, 0) == 5000);
    CHECK(b.at(colony, 20) == -1000);
    CHECK(b.at(colony, 30) == 6000);
    CHECK(!b.allows(colony));
}

// The improved computers' supply runs (ai_overview.hpp Supply, ai_logistics.cpp): a ware is due when
// its stock runs out before a ship from its best source could be back, and a run fills it up to that
// and what one run brings.

TEST_CASE("ai supply: a run takes the way there and back and a minute at each end") {
    CHECK(Supply::resupply_minutes(0) == 2);
    CHECK(Supply::resupply_minutes(60) == 4);
    // 2 * 100 + 120 seconds are five minutes and a third: the ship is back in the sixth.
    CHECK(Supply::resupply_minutes(100) == 6);
}

TEST_CASE("ai supply: a stock lasts by what the city uses beyond what it makes") {
    constexpr std::uint32_t kT = 32;
    // 40 t at 4 t used and 2 t made a minute last 20 minutes.
    CHECK(Supply::cover(40 * kT, 4 * kT, 2 * kT) == 20 * 256);
    // A city that makes as much as it uses, or uses none, never runs out.
    CHECK(Supply::cover(0, 4 * kT, 4 * kT) == Supply::kNever);
    CHECK(Supply::cover(0, 0, 0) == Supply::kNever);
    CHECK(Supply::cover(0, 4 * kT, 0) == 0);
}

TEST_CASE("ai supply: a run fills up to what lasts until the next can be back, and one load more") {
    constexpr std::uint32_t kT = 32;
    // 2 t short a minute, ten minutes for a run, 20 t a load: 40 t.
    CHECK(Supply::target(4 * kT, 2 * kT, 10, 20 * kT, 200 * kT) == 40 * kT);
    // A city that makes enough is filled by one load only.
    CHECK(Supply::target(2 * kT, 4 * kT, 10, 20 * kT, 200 * kT) == 20 * kT);
    // Never beyond the Kontor.
    CHECK(Supply::target(20 * kT, 0, 30, 20 * kT, 100 * kT) == 100 * kT);
}

TEST_CASE("ai supply: a run is due when the stock lasts less than the run takes") {
    CHECK(Supply::slack(10 * 256, 6) == 4 * 256);
    CHECK(Supply::slack(5 * 256, 6) == -256);
    CHECK(Supply::slack(Supply::kNever, 6) > 0);
}

}  // namespace o1602::sim::ai
