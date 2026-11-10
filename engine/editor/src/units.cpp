// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/units.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>

#include "o1602/editor/islands.hpp"
#include "o1602/editor/scenario_rules.hpp"
#include "o1602/sim/land_paths.hpp"
#include "o1602/sim/sea_paths.hpp"
#include "o1602/sim/ship.hpp"
#include "o1602/sim/ships.hpp"
#include "o1602/sim/soldier.hpp"

namespace o1602::editor {

using annokit::core::ErrorCode;
using annokit::core::make_error;

namespace {

bool on_map(const sim::World& world, std::int64_t x, std::int64_t y) {
    return x >= 0 && y >= 0 && x < std::int64_t{world.width()} && y < std::int64_t{world.height()};
}

/// Humans and computers, and the natives and pirates, who keep ships and units too.
bool owner_ok(const sim::World& world, sim::PlayerId owner) {
    const sim::Player* p = world.player(owner);
    return p != nullptr && (p->role == sim::PlayerRole::Human || p->role == sim::PlayerRole::Computer ||
                            world.acts_as(owner, sim::FactionBehaviour::Natives) ||
                            world.acts_as(owner, sim::FactionBehaviour::Pirates));
}

/// The players who keep trade carts: humans and computers.
bool cart_owner_ok(const sim::World& world, sim::PlayerId owner) {
    const sim::Player* p = world.player(owner);
    return p != nullptr && (p->role == sim::PlayerRole::Human || p->role == sim::PlayerRole::Computer);
}

/// The nearest of `owner`'s cities whose Kontor or market a cart on (x, y) reaches, if any.
const sim::City* cart_home(const sim::World& world, const assets::BuildingTable& buildings, sim::PlayerId owner,
                           std::int32_t x, std::int32_t y) {
    // Next to the building first, then within the reach of a route stop.
    for (const std::int32_t reach : {sim::kCartHandReach, sim::kCartReach}) {
        for (const std::size_t index : sim::cities_near_cart(world, buildings, x, y, reach)) {
            const sim::City& city = world.cities()[index];
            if (city.owner == owner) {
                return &city;
            }
        }
    }
    return nullptr;
}

/// Every order that names `gone` cleared.
void forget(sim::Target& t, sim::Target::Kind kind, std::uint32_t gone) {
    if (t.kind == kind && t.ship == gone) {
        t = sim::Target{};
    }
}

void forget_everywhere(sim::World& world, sim::Target::Kind kind, std::uint32_t gone) {
    const auto forget_queue = [&](std::vector<sim::Target>& queue) {
        std::erase_if(queue, [&](const sim::Target& t) { return t.kind == kind && t.ship == gone; });
    };
    for (sim::Ship& s : world.ships()) {
        forget(s.target, kind, gone);
        forget(s.order, kind, gone);
        forget(s.previous, kind, gone);
        forget_queue(s.queue);
    }
    for (sim::Soldier& s : world.soldiers()) {
        forget(s.target, kind, gone);
        forget(s.home, kind, gone);
        for (sim::Target& p : s.patrol.points) {
            forget(p, kind, gone);
        }
        forget_queue(s.queue);
    }
}

}  // namespace

UnitRefusal ship_fits(const sim::World& world, const assets::BuildingTable& buildings, std::int64_t x, std::int64_t y) {
    if (!on_map(world, x, y)) {
        return UnitRefusal::Outside;
    }
    const auto ux = static_cast<std::uint32_t>(x);
    const auto uy = static_cast<std::uint32_t>(y);
    const sim::World::Cell& cell = world.cell(ux, uy);
    if (cell.island != sim::World::kSea) {
        // Water inside an island's rectangle: what its tile shows (sim::SeaMap).
        const sim::MapCell& c = world.islands()[cell.island].map.top(cell.local_x, cell.local_y);
        const assets::BuildingType* b = c.empty() ? nullptr : buildings.by_sprite(c.sprite);
        if (b == nullptr || !sim::sailable_building(*b)) {
            return UnitRefusal::NoWater;
        }
    }
    for (const sim::Ship& s : world.ships()) {
        if (!s.cart() && std::abs(std::int64_t{s.x} - x) <= 1 && std::abs(std::int64_t{s.y} - y) <= 1) {
            return UnitRefusal::Taken;
        }
    }
    return UnitRefusal::None;
}

UnitRefusal unit_fits(const sim::World& world, const assets::BuildingTable& buildings, std::int64_t x, std::int64_t y) {
    if (!on_map(world, x, y)) {
        return UnitRefusal::Outside;
    }
    sim::LandMap land;
    land.rebuild(world, buildings);
    const auto ix = static_cast<std::int32_t>(x);
    const auto iy = static_cast<std::int32_t>(y);
    const assets::BuildingType* ground = land.building(ix, iy);
    if (land.island_at(ix, iy) == sim::World::kSea || (ground != nullptr && !sim::soldiers_walk_on(*ground))) {
        return UnitRefusal::NoGround;
    }
    for (const sim::Soldier& s : world.soldiers()) {
        if (s.pos_x / sim::kPosUnit == ix && s.pos_y / sim::kPosUnit == iy) {
            return UnitRefusal::Taken;
        }
    }
    return UnitRefusal::None;
}

UnitRefusal cart_fits(const sim::World& world, const assets::BuildingTable& buildings, sim::PlayerId owner,
                      std::int64_t x, std::int64_t y) {
    if (!on_map(world, x, y)) {
        return UnitRefusal::Outside;
    }
    if (!cart_owner_ok(world, owner)) {
        return UnitRefusal::NoOwner;
    }
    const auto ix = static_cast<std::int32_t>(x);
    const auto iy = static_cast<std::int32_t>(y);
    if (!sim::cart_drivable(world, buildings, ix, iy)) {
        return UnitRefusal::NoRoad;
    }
    if (cart_home(world, buildings, owner, ix, iy) == nullptr) {
        return UnitRefusal::NoMarket;
    }
    for (const sim::Ship& s : world.ships()) {
        if (s.cart() && s.x == ix && s.y == iy) {
            return UnitRefusal::Taken;
        }
    }
    // As many as the game sends out there (sim::Fleet::request_cart).
    const std::uint16_t island = world.cell(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)).island;
    const bool more = scenario_rules(world).value_or(sim::Rules{}).more_carts;
    const std::size_t allowed =
        more ? std::max<std::uint32_t>(1, sim::cart_places(world, buildings, owner, island)) : 1U;
    if (sim::trade_carts_of(world, owner, island).size() >= allowed) {
        return UnitRefusal::CartLimit;
    }
    return UnitRefusal::None;
}

annokit::core::Result<std::unique_ptr<Operation>> place_cart(const sim::World& world,
                                                             const assets::BuildingTable& buildings,
                                                             const assets::FigureTable& figures, sim::PlayerId owner,
                                                             std::int64_t x, std::int64_t y, std::uint8_t direction) {
    const assets::FigureType* type = figures.get(assets::figure::kTradeCart);
    const UnitRefusal refused = type == nullptr ? UnitRefusal::NoType : cart_fits(world, buildings, owner, x, y);
    if (refused != UnitRefusal::None) {
        return make_error(ErrorCode::InvalidFormat, refusal_name(refused));
    }
    const auto ix = static_cast<std::int32_t>(x);
    const auto iy = static_cast<std::int32_t>(y);
    const sim::City* home = cart_home(world, buildings, owner, ix, iy);
    sim::World after = world;
    sim::Ship cart = sim::make_ship(std::string(), static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                    assets::figure::kTradeCart, owner, static_cast<std::uint8_t>(direction & 7U));
    cart.cargo.resize(type->ship.cargo_slots != 0 ? type->ship.cargo_slots : 3U);
    cart.health = type->ship.max_health;
    cart.previous = sim::Target::city_of(home->island, home->index);
    after.add_ship(std::move(cart));
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "cart_place"));
}

annokit::core::Result<std::unique_ptr<Operation>> place_ship(const sim::World& world,
                                                             const assets::BuildingTable& buildings,
                                                             const assets::FigureTable& figures, sim::PlayerId owner,
                                                             std::uint16_t figure, std::int64_t x, std::int64_t y,
                                                             std::uint8_t direction) {
    const assets::FigureType* type = figures.get(figure);
    UnitRefusal refused = UnitRefusal::None;
    // The shipyards' ships and the packages', the pirates' ship, and the free trader's for him alone.
    const bool trader = world.acts_as(owner, sim::FactionBehaviour::Trader);
    if (type != nullptr && trader != (figure == assets::figure::kTrader)) {
        return make_error(ErrorCode::InvalidFormat, "the trader's ship is the free trader's alone");
    }
    if (type == nullptr ||
        (!figures.ship_type_of(figure) && figure != assets::figure::kPirate && figure != assets::figure::kTrader)) {
        refused = UnitRefusal::NoType;
    } else if (!trader && !owner_ok(world, owner)) {
        refused = UnitRefusal::NoOwner;
    } else {
        refused = ship_fits(world, buildings, x, y);
    }
    if (refused != UnitRefusal::None) {
        return make_error(ErrorCode::InvalidFormat, refusal_name(refused));
    }
    sim::World after = world;
    sim::Ship s = sim::make_ship(std::string(), static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), figure,
                                 owner, direction);
    s.health = type->ship.max_health;
    s.cargo.resize(std::max<std::size_t>(1, type->ship.cargo_slots));
    if (trader) {
        s.trader = true;  // sails from Kontor to Kontor when the game starts, his goods empty
        s.trader_goods.resize(world.goods().end());
    }
    after.add_ship(std::move(s));
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "ship_place"));
}

annokit::core::Result<std::unique_ptr<Operation>> place_unit(const sim::World& world,
                                                             const assets::BuildingTable& buildings,
                                                             const assets::FigureTable& figures, sim::PlayerId owner,
                                                             std::uint16_t family, std::int64_t x, std::int64_t y,
                                                             std::uint8_t direction) {
    const auto unit = figures.unit_of(family);
    const assets::FigureType* type = figures.get(family);
    // The natives' spearman is theirs alone, as in a game.
    const bool spearman = family == assets::figure::kSpearman;
    UnitRefusal refused = UnitRefusal::None;
    if (type == nullptr || (!spearman && (!unit || unit->family != family))) {
        refused = UnitRefusal::NoType;
    } else if ((!owner_ok(world, owner) && !world.acts_as(owner, sim::FactionBehaviour::Neutral)) ||
               (spearman && !world.acts_as(owner, sim::FactionBehaviour::Natives))) {
        refused = UnitRefusal::NoOwner;
    } else {
        refused = unit_fits(world, buildings, x, y);
    }
    if (refused != UnitRefusal::None) {
        return make_error(ErrorCode::InvalidFormat, refusal_name(refused));
    }
    const auto ux = static_cast<std::uint32_t>(x);
    const auto uy = static_cast<std::uint32_t>(y);
    sim::World after = world;
    sim::Soldier s = sim::make_soldier(ux, uy, family, owner, world.cell(ux, uy).island);
    s.direction = static_cast<std::uint8_t>(direction & 7U);
    s.health = type->combat.max_health;
    s.anim = 1;  // standing
    after.add_soldier(std::move(s));
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "unit_place"));
}

annokit::core::Result<std::unique_ptr<Operation>> set_ship_cargo(const sim::World& world, std::uint32_t id,
                                                                 std::size_t slot, std::uint8_t ware,
                                                                 std::uint32_t amount) {
    const sim::Ship* ship = world.ship(id);
    if (ship == nullptr || slot >= ship->cargo.size()) {
        return make_error(ErrorCode::NotFound, "no such ship or hold");
    }
    const auto& trade = world.goods().trade_wares();
    if (ware != 0 && std::find(trade.begin(), trade.end(), ware) == trade.end()) {
        return make_error(ErrorCode::OutOfRange, "ware " + std::to_string(ware) + " is no trade ware");
    }
    if (amount > sim::hold_capacity(*ship)) {
        return make_error(ErrorCode::OutOfRange, "more than a hold takes");
    }
    sim::World after = world;
    sim::CargoSlot& hold = after.ship(id)->cargo[slot];
    hold = sim::CargoSlot{};  // no price paid, no unit of a package
    if (ware != 0 && amount > 0) {
        hold.ware = ware;
        hold.amount = amount;
    }
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "ship_cargo"));
}

annokit::core::Result<std::unique_ptr<Operation>> edit_route(const sim::World& world,
                                                             const assets::BuildingTable& buildings,
                                                             const assets::FigureTable& figures, std::uint32_t id,
                                                             sim::RouteEdit edit) {
    if (world.ship(id) == nullptr) {
        return make_error(ErrorCode::NotFound, "no such ship");
    }
    edit.ship = id;
    edit.player = sim::kNoPlayer;  // the editor's hand, not a player's
    if (edit.op == sim::RouteEdit::Op::SetStop && edit.target.kind != sim::Target::Kind::City && !edit.target.empty()) {
        return make_error(ErrorCode::Unsupported, "a route of the editor stops at cities");
    }
    sim::World after = world;
    const sim::Rules rules = scenario_rules(world).value_or(sim::Rules{});
    sim::Fleet fleet(after, buildings, figures, rules);
    sim::Ship& ship = *after.ship(id);
    // A route begins at its first stop.
    const auto first_stop = [&ship] {
        ship.route.current = 0;
        while (ship.route.current + 1U < sim::TradeRoute::kStops &&
               ship.route.stops[ship.route.current].target.empty()) {
            ++ship.route.current;
        }
    };
    const bool start = edit.op == sim::RouteEdit::Op::Start;
    if (start) {
        first_stop();
    }
    if (!fleet.edit_route(edit)) {
        return make_error(ErrorCode::Unsupported, "the route refuses the change");
    }
    // A running route begins again from its first stop with what it carries now, as at the start.
    if (ship.route.active && !start) {
        if (!ship.route.has_stops()) {
            ship.route.active = false;
            ship.target = {};
        } else {
            first_stop();
            (void)fleet.edit_route(sim::RouteEdit{sim::kNoPlayer, id, sim::RouteEdit::Op::Start});
        }
    }
    if (edit.op == sim::RouteEdit::Op::Stop) {
        ship.target = {};  // a ship placed in the editor waits where it is
    }
    ship.route.problem = false;
    ship.route.problem_rounds = 0;
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "ship_route"));
}

annokit::core::Result<std::unique_ptr<Operation>> remove_ship(const sim::World& world, std::uint32_t id) {
    if (world.ship(id) == nullptr) {
        return make_error(ErrorCode::NotFound, "no such ship");
    }
    sim::World after = world;
    after.erase_ships([id](const sim::Ship& s) { return s.id == id; });
    forget_everywhere(after, sim::Target::Kind::Ship, id);
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "ship_remove"));
}

annokit::core::Result<std::unique_ptr<Operation>> remove_unit(const sim::World& world, std::uint32_t id) {
    if (world.soldier(id) == nullptr) {
        return make_error(ErrorCode::NotFound, "no such unit");
    }
    sim::World after = world;
    after.erase_soldiers([id](const sim::Soldier& s) { return s.id == id; });
    forget_everywhere(after, sim::Target::Kind::Soldier, id);
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "unit_remove"));
}

const char* refusal_name(UnitRefusal refusal) noexcept {
    switch (refusal) {
        case UnitRefusal::None:
            return "none";
        case UnitRefusal::Outside:
            return "outside the map";
        case UnitRefusal::NoWater:
            return "no water a ship sails on";
        case UnitRefusal::NoGround:
            return "no ground a unit walks on";
        case UnitRefusal::Taken:
            return "taken by another";
        case UnitRefusal::NoOwner:
            return "no player to own it";
        case UnitRefusal::NoType:
            return "no such type";
        case UnitRefusal::NoRoad:
            return "no ground a cart drives on";
        case UnitRefusal::NoMarket:
            return "no Kontor or market of its owner within reach";
        case UnitRefusal::CartLimit:
            return "no further cart for its owner on the island";
    }
    return "unknown";
}

}  // namespace o1602::editor
