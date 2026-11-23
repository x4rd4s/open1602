// SPDX-License-Identifier: GPL-3.0-or-later
// An improved computer in debt (ai_planner.cpp): it keeps what it sold within the hour its goods
// statistics show, and it switches farms with fields off instead of tearing them down.

#include <cstdint>
#include <vector>

#include "../../engine/sim/src/ai/ai_engine.hpp"
#include "../test_framework.hpp"

#include "o1602/assets/game_data.hpp"
#include "o1602/sim/goods_ledger.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;
using sim::ai::Engine;

namespace {

constexpr std::uint8_t kCocoa = 17;
constexpr std::uint8_t kTobacco = 18;
constexpr sim::PlayerId kComputer = 1;
constexpr sim::PlayerId kOther = 2;

sim::City city(std::uint16_t island, sim::PlayerId owner) {
    sim::City c;
    c.island = island;
    c.index = 0;
    c.owner = owner;
    return c;
}

/// Closes `n` game minutes of the city's ledger, nothing booked in them.
void minutes(sim::City& c, std::uint32_t n) {
    const std::vector<std::uint32_t> stock(256, 0);
    for (std::uint32_t k = 0; k < n; ++k) {
        c.ledger.close_minute(stock);
    }
}

assets::BuildingType producer(std::int32_t kind, std::uint8_t ware) {
    assets::BuildingType b;
    b.prod_kind = kind;
    b.ware = ware;
    return b;
}

}  // namespace

TEST_CASE("ai debt: a ware its cities sold within the statistics' hour counts as sold") {
    // Gate f25, g4-202 P1: 18 cocoa plantations torn down in debt, a ware it had sold until five
    // minutes before; the buyer came back, the plantations did not.
    sim::World world{40, 40};
    sim::City mine = city(0, kComputer);
    mine.ledger.add(kCocoa, sim::GoodsFlow::Sold, 32);
    minutes(mine, 1);
    sim::City theirs = city(1, kOther);
    theirs.ledger.add(kTobacco, sim::GoodsFlow::Sold, 32);
    minutes(theirs, 1);
    world.add_city(mine);
    world.add_city(theirs);

    const Engine::WareSet sold = Engine::sold_wares(world, kComputer);
    CHECK(sold[kCocoa]);
    CHECK(!sold[kTobacco]);  // another player's sale is not its own
}

TEST_CASE("ai debt: a sale drops out once the statistics' hour has passed without one") {
    sim::World world{40, 40};
    sim::City mine = city(0, kComputer);
    mine.ledger.add(kCocoa, sim::GoodsFlow::Sold, 32);
    mine.ledger.add(kTobacco, sim::GoodsFlow::ShippedOut, 32);  // carried away, not sold
    minutes(mine, sim::GoodsLedger::kMinutes);                  // the minute of the sale is the oldest of the hour
    sim::World early{40, 40};
    early.add_city(mine);
    CHECK(Engine::sold_wares(early, kComputer)[kCocoa]);  // still within the hour
    CHECK(!Engine::sold_wares(early, kComputer)[kTobacco]);

    minutes(mine, 1);
    world.add_city(mine);
    CHECK(!Engine::sold_wares(world, kComputer)[kCocoa]);
}

TEST_CASE("ai debt: a farm with fields only goes off; other producers of a ware not kept go") {
    Engine::WareSet keep{};
    keep[kTobacco] = true;
    CHECK(!Engine::may_shed(producer(assets::prod::kFarm, kCocoa), keep));        // fields: off, not torn down
    CHECK(Engine::may_shed(producer(assets::prod::kWorkshop, kCocoa), keep));     // a workshop goes
    CHECK(Engine::may_shed(producer(assets::prod::kPasture, kCocoa), keep));      // a pasture has no fields
    CHECK(!Engine::may_shed(producer(assets::prod::kWorkshop, kTobacco), keep));  // kept
    CHECK(!Engine::may_shed(producer(assets::prod::kMarket, kCocoa), keep));
    CHECK(!Engine::may_shed(producer(assets::prod::kWorkshop, 0), keep));
}
