// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/objects.hpp"

#include <utility>

#include "o1602/editor/islands.hpp"
#include "o1602/sim/logistics.hpp"

namespace o1602::editor {

using core::ErrorCode;
using core::make_error;

PlacedBuildings place_buildings(const sim::World& world, const assets::BuildingTable& buildings, sim::BuildOrder order,
                                const std::vector<std::string>& city_names) {
    order.free = true;
    PlacedBuildings result;
    result.plan = sim::plan_build(world, buildings, order);
    if (result.plan.placed == 0) {
        return result;
    }
    sim::World after = world;
    result.plan = sim::execute_build(after, buildings, order, city_names, nullptr);
    if (result.plan.placed > 0) {
        result.step = std::make_unique<WorldEdit>(std::move(after), "building_place");
    }
    return result;
}

core::Result<std::unique_ptr<Operation>> remove_building(const sim::World& world,
                                                         const assets::BuildingTable& buildings, std::uint16_t island,
                                                         sim::TilePos tile) {
    if (island >= world.islands().size()) {
        return make_error(ErrorCode::NotFound, "no such island");
    }
    const auto at = sim::building_at(world, buildings, island, tile);
    if (!at) {
        return make_error(ErrorCode::NotFound, "no building there");
    }
    const sim::IslandMap& map = world.islands()[island].map;
    const sim::MapCell& cell =
        map.top(static_cast<std::uint32_t>(at->origin.x), static_cast<std::uint32_t>(at->origin.y));
    sim::DemolishOrder order;
    order.player = cell.owner;
    order.island = island;
    order.from = at->origin;
    order.to = at->origin;
    if (sim::plan_demolish(world, buildings, order).empty()) {
        return make_error(ErrorCode::Unsupported, "the building cannot be taken away");
    }
    sim::World after = world;
    const sim::DemolishResult removed = sim::execute_demolish(after, buildings, order, nullptr);
    if (removed.removed == 0) {
        return make_error(ErrorCode::Unsupported, "the building cannot be taken away");
    }
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "building_remove"));
}

}  // namespace o1602::editor
