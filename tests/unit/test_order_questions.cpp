// SPDX-License-Identifier: GPL-3.0-or-later
// The questions before orders that cannot be taken back (engine/app/src/order_questions.hpp).

#include <string>

#include "../test_framework.hpp"
#include "order_questions.hpp"

#include "o1602/sim/ship.hpp"
#include "o1602/sim/soldier.hpp"
#include "o1602/ui/localization.hpp"

using namespace o1602;

namespace {

constexpr sim::PlayerId kMe = 0;
constexpr sim::PlayerId kOther = 1;
constexpr sim::PlayerId kTrader = 2;

/// Three players: this one, another one and the free trader, each with a ship; both players
/// have a soldier.
struct Game {
    sim::World world{64, 64};
    assets::FigureTable figures;
    std::uint32_t my_ship = 0;
    std::uint32_t other_ship = 0;
    std::uint32_t trader_ship = 0;
    std::uint32_t my_soldier = 0;
    std::uint32_t other_soldier = 0;

    Game() {
        std::vector<sim::Player> players(3);
        players[kMe].role = sim::PlayerRole::Human;
        players[kOther].role = sim::PlayerRole::Computer;
        players[kOther].name = "Rot";
        players[kTrader].role = sim::PlayerRole::Trader;
        players[kTrader].name = "Trader";
        world.set_players(players);
        world.add_ship(sim::make_ship("Mine", 10, 10, 21, kMe));
        world.add_ship(sim::make_ship("Theirs", 20, 10, 21, kOther));
        world.add_ship(sim::make_ship("Trade", 30, 10, 21, kTrader));
        my_ship = world.ships()[0].id;
        other_ship = world.ships()[1].id;
        trader_ship = world.ships()[2].id;
        world.add_soldier(sim::make_soldier(5, 5, 1, kMe));
        world.add_soldier(sim::make_soldier(6, 5, 1, kOther));
        my_soldier = world.soldiers()[0].id;
        other_soldier = world.soldiers()[1].id;
        set_peace(sim::Treaty::Signed);
    }

    void set_peace(sim::Treaty t) {
        world.diplomacy().relation(kMe, kOther).peace = t;
        world.diplomacy().relation(kOther, kMe).peace = t;
    }

    [[nodiscard]] std::optional<std::string> ask(const sim::Command& c) const {
        return app::order_question(world, figures, kMe, c);
    }
};

bool mentions(const std::optional<std::string>& q, const std::string& what) {
    return q.has_value() && q->find(what) != std::string::npos;
}

}  // namespace

TEST_CASE("order questions: the owner of a target, or nobody") {
    const Game g;
    CHECK(app::target_owner(g.world, sim::Target::other_ship(g.other_ship)) == kOther);
    CHECK(app::target_owner(g.world, sim::Target::soldier(g.my_soldier)) == kMe);
    CHECK(app::target_owner(g.world, sim::Target::other_ship(9999)) == sim::kNoPlayer);
    CHECK(app::target_owner(g.world, sim::Target::building(7, 1, 1)) == sim::kNoPlayer);  // no such island
    CHECK(app::target_owner(g.world, sim::Target::position(3, 3)) == sim::kNoPlayer);
}

TEST_CASE("order questions: scuttling and disbanding ask, other unit commands do not") {
    const Game g;
    const auto ship_command = [&](sim::ShipCommand::Op op) {
        sim::ShipCommand c;
        c.player = kMe;
        c.ship = g.my_ship;
        c.op = op;
        return c;
    };
    CHECK(mentions(g.ask(ship_command(sim::ShipCommand::Op::Scuttle)), "Mine"));
    CHECK(!g.ask(ship_command(sim::ShipCommand::Op::Hold)).has_value());
    CHECK(mentions(g.ask(sim::SoldierCommand{kMe, g.my_soldier, sim::SoldierCommand::Op::Disband, 0, {}}), "0"));
    CHECK(
        !g.ask(sim::SoldierCommand{kMe, g.my_soldier, sim::SoldierCommand::Op::SetWaypoint, 0, sim::Target::spot(4, 4)})
             .has_value());
}

TEST_CASE("order questions: breaking a signed peace asks, other diplomacy does not") {
    Game g;
    sim::DiplomacyOrder breaking;
    breaking.player = kMe;
    breaking.partner = kOther;
    breaking.kind = sim::DiplomacyOrder::Kind::Peace;
    breaking.value = sim::Treaty::None;
    CHECK(mentions(g.ask(breaking), "Rot"));
    sim::DiplomacyOrder offer = breaking;
    offer.value = sim::Treaty::Offered;
    CHECK(!g.ask(offer).has_value());
    g.set_peace(sim::Treaty::None);
    CHECK(!g.ask(breaking).has_value());  // no treaty to break
}

TEST_CASE("order questions: an attack asks only when it means war") {
    Game g;
    const auto attack = [&](sim::Target t) {
        sim::ShipOrder o;
        o.player = kMe;
        o.ship = g.my_ship;
        o.target = t;
        o.attack = true;
        return g.ask(o);
    };
    CHECK(mentions(attack(sim::Target::other_ship(g.other_ship)), "Rot"));
    CHECK(mentions(attack(sim::Target::soldier(g.other_soldier)), "Rot"));
    CHECK(!attack(sim::Target::other_ship(g.trader_ship)).has_value());  // the free trader: no war
    CHECK(!attack(sim::Target::soldier(g.my_soldier)).has_value());      // one's own
    CHECK(!attack(sim::Target::position(3, 3)).has_value());

    sim::ShipOrder move;
    move.player = kMe;
    move.ship = g.my_ship;
    move.target = sim::Target::other_ship(g.other_ship);
    CHECK(!g.ask(move).has_value());  // following is no attack

    g.set_peace(sim::Treaty::None);
    CHECK(!attack(sim::Target::other_ship(g.other_ship)).has_value());  // at war already
}
