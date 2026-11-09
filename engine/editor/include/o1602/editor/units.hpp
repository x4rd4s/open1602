// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's ships, land units and trade carts (ADR 0013): put down for a player where the game
// lets them be, and taken away again, each on a copy of the world given back as a WorldEdit for
// undo, like the buildings'.
#pragma once

#include <cstdint>
#include <memory>

#include "annokit/core/result.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/editor/operation.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::editor {

/// Why a ship, a land unit or a trade cart cannot go on a tile.
enum class UnitRefusal : std::uint8_t {
    None,
    Outside,    ///< not on the map
    NoWater,    ///< a ship on land or on water it cannot sail
    NoGround,   ///< a land unit on the sea, or on ground soldiers do not walk on
    Taken,      ///< another ship next to it, another unit or cart on the same tile
    NoOwner,    ///< not a human or computer player, nor the natives or pirates
    NoType,     ///< no ship or land unit of the figure tables
    NoRoad,     ///< a trade cart off the ground carts drive on
    NoMarket,   ///< a trade cart out of reach of a Kontor or market of its owner
    CartLimit,  ///< a trade cart more than its owner may have on the island
};

/// Whether a ship can go on world tile (x, y): water ships sail on, no other ship within a tile.
[[nodiscard]] UnitRefusal ship_fits(const sim::World& world, const assets::BuildingTable& buildings, std::int64_t x,
                                    std::int64_t y);
/// Whether a land unit can go on world tile (x, y): an island's ground soldiers walk on, no other
/// unit on the tile.
[[nodiscard]] UnitRefusal unit_fits(const sim::World& world, const assets::BuildingTable& buildings, std::int64_t x,
                                    std::int64_t y);

/// Whether a trade cart of `owner` can go on world tile (x, y): a human or computer player's, on
/// ground carts drive on within reach of a Kontor or market of one of its cities, no other cart on
/// the tile, and no more carts on the island than the game gives the owner there (one, or with
/// Rules::more_carts of the scenario's settings one for each of its Kontors and markets).
[[nodiscard]] UnitRefusal cart_fits(const sim::World& world, const assets::BuildingTable& buildings,
                                    sim::PlayerId owner, std::int64_t x, std::int64_t y);

/// A trade cart for `owner` on world tile (x, y), facing `direction` (0..7), empty and idle, at
/// home in the nearest of the owner's cities it reaches, as the city's button sends it out.
/// Fails (InvalidFormat) with the refusal's name where it cannot go.
[[nodiscard]] annokit::core::Result<std::unique_ptr<Operation>> place_cart(const sim::World& world,
                                                                           const assets::BuildingTable& buildings,
                                                                           const assets::FigureTable& figures,
                                                                           sim::PlayerId owner, std::int64_t x,
                                                                           std::int64_t y, std::uint8_t direction);

/// A ship of `figure` (one of the shipyards' or a package's ship, or the pirates' ship) for `owner` on world tile (x,
/// y), facing `direction` (0..7), new and empty as a shipyard launches it; unnamed, so the game names it when it
/// starts. Fails (InvalidFormat) with the refusal's name where it cannot go.
[[nodiscard]] annokit::core::Result<std::unique_ptr<Operation>> place_ship(
    const sim::World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
    sim::PlayerId owner, std::uint16_t figure, std::int64_t x, std::int64_t y, std::uint8_t direction);

/// A land unit of the family `family` (its first figure, FigureTable::unit_families) for `owner`
/// on world tile (x, y), facing `direction` (0..7), at full health.
[[nodiscard]] annokit::core::Result<std::unique_ptr<Operation>> place_unit(
    const sim::World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
    sim::PlayerId owner, std::uint16_t family, std::int64_t x, std::int64_t y, std::uint8_t direction);

/// The hold `slot` of ship `id` loaded with `amount` (1/32 t, at most sim::hold_capacity) of the
/// trade ware `ware`; 0 tons or ware 0: empty. Fails (NotFound) without the ship or the hold,
/// (OutOfRange) for a ware that is no trade ware or too much.
[[nodiscard]] annokit::core::Result<std::unique_ptr<Operation>> set_ship_cargo(const sim::World& world,
                                                                               std::uint32_t id, std::size_t slot,
                                                                               std::uint8_t ware, std::uint32_t amount);

/// The ship or trade cart `id` taken away, with every order of others that names it. Fails (NotFound).
[[nodiscard]] annokit::core::Result<std::unique_ptr<Operation>> remove_ship(const sim::World& world, std::uint32_t id);
/// The land unit `id` taken away, with every order of others that names it. Fails (NotFound).
[[nodiscard]] annokit::core::Result<std::unique_ptr<Operation>> remove_unit(const sim::World& world, std::uint32_t id);

/// The name of a refusal for messages and logs.
[[nodiscard]] const char* refusal_name(UnitRefusal refusal) noexcept;

}  // namespace o1602::editor
