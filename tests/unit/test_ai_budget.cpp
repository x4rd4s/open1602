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

TEST_CASE("ai budget: the colony plan takes the passing candidate of the highest rank") {
    const auto c = [](std::uint8_t crop, bool passes, std::int64_t rank) {
        ColonyCandidate k;
        k.crop = crop;
        k.passes = passes;
        k.rank = rank;
        return k;
    };
    // The higher rank wins, wherever it stands in the original's order.
    CHECK(pick_colony({c(47, true, 100), c(50, true, 300), c(46, true, 200)}) == std::optional<std::size_t>{1});
    // On equal ranks the original's order decides.
    CHECK(pick_colony({c(47, true, 300), c(50, true, 300)}) == std::optional<std::size_t>{0});
    // A candidate the budget does not carry is never taken, however high it ranks.
    CHECK(pick_colony({c(47, false, 900), c(50, true, -50)}) == std::optional<std::size_t>{1});
    CHECK(!pick_colony({c(47, false, 900)}).has_value());
    CHECK(!pick_colony({}).has_value());
}

}  // namespace o1602::sim::ai
