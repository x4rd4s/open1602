// SPDX-License-Identifier: GPL-3.0-or-later
// An improved computer in debt (ai_planner.cpp): it keeps what it sold within the hour its goods
// statistics show, and it switches farms with fields off instead of tearing them down.

#include <cstdint>
#include <vector>

#include "../../engine/sim/src/ai/ai_engine.hpp"
#include "../test_framework.hpp"

#include "o1602/assets/game_data.hpp"
#include "o1602/sim/goods_ledger.hpp"
#include "o1602/sim/houses.hpp"
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

TEST_CASE("ai luxury: a crop on half-fertile land only as the last resort") {
    // Gate f26, g4-202 P3: its tobacco goods came only by ship from partners; when none offered
    // them its citizens left (420 to 133). Its second island grows tobacco half. Merlin: only when
    // no fertile island is open and trade is not enough.
    CHECK(Engine::plants_on(128, false, true, true, false));   // fully fertile: as before
    CHECK(Engine::plants_on(64, true, false, false, true));    // half: lacking, no trade, no colony, paid
    CHECK(!Engine::plants_on(64, true, true, false, true));    // trade covers it: bought
    CHECK(!Engine::plants_on(64, true, false, true, true));    // a fertile island is open: a colony
    CHECK(!Engine::plants_on(64, false, false, false, true));  // not lacking
    CHECK(!Engine::plants_on(0, true, false, false, true));    // it does not grow there
    // Gate f27: one it cannot pay now waited in need() on every planner step, for hours.
    CHECK(!Engine::plants_on(64, true, false, false, false));
}

TEST_CASE("ai war plan: a crop island is a war motive only when the freed island can be settled") {
    CHECK(Engine::crop_war_motive(true, true, false));    // lacking, could settle, no free island
    CHECK(!Engine::crop_war_motive(false, true, false));  // not lacking
    CHECK(!Engine::crop_war_motive(true, true, true));    // a free island grows it: found there
    // Gate f29b, x3 g4-404-p0: the budget refused the colony, so the island went to a third player.
    CHECK(!Engine::crop_war_motive(true, false, false));
}

TEST_CASE("ai war plan: a crop counts as lacking only where none of its islands grows it and a city has none") {
    CHECK(Engine::crop_lacking(true, false, true));
    CHECK(!Engine::crop_lacking(false, false, true));  // it makes what it uses
    // Gate f43: 6 of 14 crop wars for crops its own islands grew already.
    CHECK(!Engine::crop_lacking(true, true, true));
    // Gate f43, x3 o4-808-p0: P0 declared with 68 t of alcohol in store, and P3 fell.
    CHECK(!Engine::crop_lacking(true, false, false));
}

TEST_CASE("ai ships: a ship under the white flag takes no order while a pirate is near") {
    // Gate f45: three ships sunk 2 to 33 s after a new order struck the flag beside the pirate.
    CHECK(Engine::flag_keeps(true, true, true));
    CHECK(!Engine::flag_keeps(true, true, false));  // the pirate has sailed off
    CHECK(!Engine::flag_keeps(true, false, true));  // no flag up
    CHECK(!Engine::flag_keeps(false, true, true));  // the original's computers: as before
}

TEST_CASE("ai settle: an island chosen as contested is landed on by the same rule") {
    // The original: beside a city of a player with two cities or more that has no people yet.
    CHECK(Engine::leaves_room(false, false, false, true, false));
    CHECK(!Engine::leaves_room(false, false, true, true, false));  // a small owner
    CHECK(!Engine::leaves_room(false, false, false, true, true));  // the city has people
    CHECK(!Engine::leaves_room(true, false, false, true, false));  // an island the original keeps off
    // Feedback 16, K2: the ship turned back at a contested island and came back again and again.
    CHECK(Engine::leaves_room(false, true, true, true, true));
    CHECK(!Engine::leaves_room(false, true, true, false, true));  // too little free land
}

TEST_CASE("ai ships: a ship with soldiers aboard is not free for supply runs") {
    // Feedback 16, K4: a ship sailed ore to a Kontor with its troops aboard.
    CHECK(Engine::troops_aboard_idle(true, 4));
    CHECK(!Engine::troops_aboard_idle(true, 0));
    CHECK(!Engine::troops_aboard_idle(false, 4));  // the original's computers: as before
}

TEST_CASE("ai war plan: a war only when the attacker could settle what it razes") {
    CHECK(Engine::war_settles(true, true));
    // Gate f30: 13 of 16 razed cities gained the attacker nothing.
    CHECK(!Engine::war_settles(false, true));  // at its city limit
    CHECK(!Engine::war_settles(true, false));  // its budget refuses the colony
}

TEST_CASE("ai settle: improved pirates pass over an island they turned back from") {
    // Gate f33: the pirates' search skipped left_islands and the blocker, so their ship sailed back
    // to the same island every 510 s.
    CHECK(Engine::pirate_island_skipped(true, true, false));
    CHECK(Engine::pirate_island_skipped(true, false, true));
    CHECK(!Engine::pirate_island_skipped(true, false, false));
    CHECK(!Engine::pirate_island_skipped(false, true, true));  // the original's pirates: as before
}

TEST_CASE("ai settle: above its city limit a computer settles only an island its war freed") {
    // Merlin (2026-11-24): a player does not leave the island his war freed to the others.
    CHECK(Engine::beyond_limit(true, false, false));   // below the limit: any island
    CHECK(Engine::beyond_limit(false, true, true));    // above it: the freed island
    CHECK(!Engine::beyond_limit(false, true, false));  // but no other
    CHECK(!Engine::beyond_limit(false, false, false));
}

TEST_CASE("ai war plan: a shortfall the stock covers for the war's span does not hold a war back") {
    // Lever S: war_supplied counted only the rates; full warehouses held wars back.
    CHECK(Engine::stock_covers(0, 0, 30));          // no shortfall
    CHECK(Engine::stock_covers(32, 32 * 30, 30));   // a ton a minute, thirty tons held
    CHECK(!Engine::stock_covers(32, 32 * 29, 30));  // a ton short
}

TEST_CASE("ai settle: more cities than the limit only where the victory asks for them") {
    // Merlin (2026-11-24): the islands share counts as victory.cpp does (rounded up), and only when
    // computers can win; a mission's city count counts for its player.
    CHECK(Engine::goal_city_count(18, 30, true, 0) == 6);   // 5.4 islands: six cities
    CHECK(Engine::goal_city_count(20, 30, true, 0) == 6);   // exactly six
    CHECK(Engine::goal_city_count(18, 30, false, 0) == 0);  // computers cannot win: no goal
    CHECK(Engine::goal_city_count(18, 0, true, 7) == 7);    // a mission's count
    CHECK(Engine::goal_city_count(18, 50, true, 7) == 9);   // whichever is more
    CHECK(Engine::goal_city_count(18, 0, true, -1) == 0);
}

TEST_CASE("ai supply: a route stop books only what it can still load") {
    // C2: counted at the full amount, runs that had loaded kept a Kontor's spare booked.
    CHECK(Engine::still_loads(64, 0, false) == 64);
    CHECK(Engine::still_loads(64, 40, false) == 24);  // partly loaded
    CHECK(Engine::still_loads(64, 64, false) == 0);   // full
    CHECK(Engine::still_loads(64, 80, false) == 0);
    CHECK(Engine::still_loads(64, 0, true) == 0);  // a one-way run past the stop
}

TEST_CASE("ai yard: the build waits while a damaged ship is called in") {
    // P1: the yard's build and the city's construction used up the wood, and the repairs stopped.
    CHECK(!Engine::yard_builds(true, true, false, true));  // paused for the ship on its way
    CHECK(Engine::yard_builds(true, true, true, true));    // a repair runs: the yard is on anyway
    CHECK(Engine::yard_builds(true, true, false, false));  // nobody called: the build goes on
    CHECK(Engine::yard_builds(false, true, false, true));  // the original's computers
}

TEST_CASE("ai yard: after a stall a repair waits for material in the yard itself") {
    // f43: with only the Kontor's stock a called ship started and was released ten seconds later.
    CHECK(Engine::repair_may_start(true, false, false));  // no stall yet
    CHECK(!Engine::repair_may_start(true, true, false));  // stalled, the yard holds nothing
    CHECK(Engine::repair_may_start(true, true, true));    // the material has arrived
    CHECK(Engine::repair_may_start(false, true, false));  // the original's computers
}

TEST_CASE("ai doctor: an improved computer builds the next doctor where houses the plague can reach lack one") {
    // p48: 92 % of the plague losses fell in cities with one doctor that never got the next.
    CHECK(Engine::checks_coverage(true, sim::ai::kSlotKlinik, false));
    CHECK(!Engine::checks_coverage(false, sim::ai::kSlotKlinik, false));  // the original's gold flag
    CHECK(Engine::checks_coverage(false, sim::ai::kSlotKapelle, false));
    CHECK(!Engine::checks_coverage(true, sim::ai::kSlotBadehaus, false));
    sim::City c;
    c.island = 2;
    c.index = 1;
    sim::House h;
    h.island = 2;
    h.city = 1;
    h.resident_class = 2;  // citizens
    CHECK(Engine::lacks_doctor(h, c));
    h.services = sim::house_service::kKlinik;
    CHECK(!Engine::lacks_doctor(h, c));
    h.services = 0;
    h.resident_class = 1;  // settlers: no plague breaks out there
    CHECK(!Engine::lacks_doctor(h, c));
    h.resident_class = 2;
    h.city = 0;  // another city's
    CHECK(!Engine::lacks_doctor(h, c));
}

TEST_CASE("ai doctor: the doctor opens again only for as many houses as its site must cover") {
    // p49: gaps of one to seven houses had the search fail again and again (min_houses 8).
    CHECK(!Engine::doctor_gap(7, 8));
    CHECK(Engine::doctor_gap(8, 8));
    CHECK(!Engine::doctor_gap(0, 0));
}

TEST_CASE("ai doctor: with no site, a house makes way for a doctor where it reaches the most houses") {
    // Merlin, 2026-11-25: the people of the house pulled down are soon made up by the doctor.
    CHECK(Engine::swap_pays(8, 8 * 20 * 64, 40 * 64, 8));
    CHECK(!Engine::swap_pays(7, 7 * 20 * 64, 40 * 64, 8));  // fewer than its site must cover
    CHECK(!Engine::swap_pays(8, 40 * 64, 40 * 64, 8));      // no more people covered than lost
    // Coverage first, then the fewer people lost; no class is spared (a noble's house may go).
    CHECK(Engine::swap_prefers(12, 40 * 64, 10, 5 * 64));
    CHECK(Engine::swap_prefers(10, 5 * 64, 10, 40 * 64));
    CHECK(!Engine::swap_prefers(10, 40 * 64, 10, 5 * 64));
}

TEST_CASE("ai doctor: a long road does not open the doctor again after a house went for none") {
    // t70: the road reset the slot 5 s after the error, and a second house went.
    CHECK(!Engine::road_resets(true, sim::ai::kSlotKlinik, 2));
    CHECK(Engine::road_resets(true, sim::ai::kSlotKlinik, 1));
    CHECK(!Engine::road_resets(true, sim::ai::kSlotIronMine, 2));
    CHECK(!Engine::road_resets(true, sim::ai::kSlotWoodTower, 0));
    CHECK(Engine::road_resets(true, sim::ai::kSlotHouses, 2));
    CHECK(Engine::road_resets(false, sim::ai::kSlotKlinik, 2));  // the original resets every slot
    CHECK(Engine::road_resets(false, sim::ai::kSlotWoodTower, 0));
}

TEST_CASE("ai stage: citizens move on with an income, without the gold in store") {
    // f39 o4-707-p0 P1: a poor computer never held the gold and waited 3.6 hours at stage citizens.
    CHECK(Engine::citizens_rise(true, true, false));
    CHECK(Engine::citizens_rise(true, false, true));    // earning: moves on
    CHECK(!Engine::citizens_rise(true, false, false));  // neither gold nor income
    CHECK(!Engine::citizens_rise(false, true, true));   // the citizens first
}

TEST_CASE("ai defence: a first castle when a near rival not at peace shows the stronger army") {
    // t64: 69 landings on improved computers without a soldier; behind() looks only at humans.
    CHECK(Engine::outmatched_by(true, false, 12, 0));
    CHECK(!Engine::outmatched_by(false, false, 12, 0));  // far away
    CHECK(!Engine::outmatched_by(true, true, 12, 0));    // at peace
    CHECK(!Engine::outmatched_by(true, false, 4, 4));    // not stronger
}

TEST_CASE("ai stage: an improved computer leaves a citizens' stage on gold alone only once citizens come") {
    // The rich exits citizens -> citizens2 and citizens2 -> merchants. The original's computers
    // leave on the gold alone.
    CHECK(Engine::gold_exit(false, true, false));
    CHECK(Engine::gold_exit(false, true, true));
    // p59, x3 g4-404-p1 P1: citizens to merchants within seconds at 5,700 gold, no citizen.
    CHECK(!Engine::gold_exit(true, true, false));
    CHECK(Engine::gold_exit(true, true, true));
    CHECK(!Engine::gold_exit(true, false, true));  // not rich
    CHECK(!Engine::gold_exit(false, false, true));
}

TEST_CASE("ai luxury: an improved computer weighs its crops against its sales as well as its taxes") {
    // t76b: taxes below 25 a city held the colony's plantation for an hour, sales of 177 a minute.
    CHECK(Engine::luxury_income(true, -40, 177) == 177);
    CHECK(Engine::luxury_income(true, 300, -20) == -20);   // purchases count against it too
    CHECK(Engine::luxury_income(false, -40, 177) == -40);  // the original looks at its taxes only
}
