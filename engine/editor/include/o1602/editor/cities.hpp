// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's cities (ADR 0013): a city's name, the goods in its warehouses and its owner, each
// changed on a copy of the world and given back as a WorldEdit for undo, like the buildings'.
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "annokit/core/result.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/editor/operation.hpp"
#include "o1602/sim/ships.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::editor {

/// The longest name a city takes, as a player's rename (sim::CityOrder::kMaxNameLength).
inline constexpr std::size_t kMaxCityName = 40;

/// The city `city` of island `island`, named `name` (Windows-1252, sim::valid_name). Fails
/// (NotFound) without the city, (InvalidFormat) for a name a player could not give.
[[nodiscard]] annokit::core::Result<std::unique_ptr<Operation>> rename_city(const sim::World& world,
                                                                            std::uint16_t island, std::uint8_t city,
                                                                            std::string_view name);

/// The room per ware of the city in 1/32 t (sim::City::capacity, as the game reckons it from the
/// city's Kontor, markets and warehouses); 0 without the city.
[[nodiscard]] std::uint32_t city_room(const sim::World& world, const assets::BuildingTable& buildings,
                                      std::uint16_t island, std::uint8_t city);

/// The city with `amount` (1/32 t) of `ware` in its warehouses, at most its room (city_room()):
/// the game would lose what is above. Fails (NotFound) without the city, (InvalidFormat) for a
/// ware that is no trade good of the world.
[[nodiscard]] annokit::core::Result<std::unique_ptr<Operation>> set_city_goods(const sim::World& world,
                                                                               const assets::BuildingTable& buildings,
                                                                               std::uint16_t island, std::uint8_t city,
                                                                               std::uint8_t ware, std::uint32_t amount);

/// What the city's Kontor offers for the ware of `setting` to other players' ships and the free
/// trader: its mode, price and amount (keep or limit, at most city_room()) as the Kontor's trade
/// window sets them (sim::apply_trade_setting; the setting's player, island and city are not read).
/// Fails (NotFound) without the city or without room (no Kontor), (InvalidFormat) for a ware that is
/// no trade good, (Unsupported) for one more ware than the Kontor's slots allow (sim::trade_slots).
[[nodiscard]] annokit::core::Result<std::unique_ptr<Operation>> set_city_trade(const sim::World& world,
                                                                               const assets::BuildingTable& buildings,
                                                                               std::uint16_t island, std::uint8_t city,
                                                                               const sim::TradeSetting& setting);

/// The city's trade as its owner starts with it: a native village's offer
/// (sim::native_village_offer), nothing for everybody else. Fails (NotFound) without the city.
[[nodiscard]] annokit::core::Result<std::unique_ptr<Operation>> reset_city_trade(const sim::World& world,
                                                                                 std::uint16_t island,
                                                                                 std::uint8_t city);

/// The city and every tile it holds given to `owner`, a human, a computer, the natives or the
/// pirates (for whom the editor places buildings too), as when a player takes a city over: its
/// statistics start anew. A city that becomes a native village gets a village's offer
/// (sim::native_village_offer), one that stops being one trades nothing. Fails (NotFound) without
/// the city or the player, (Unsupported) where `owner` has a city on the island already (one each)
/// or is none of those.
[[nodiscard]] annokit::core::Result<std::unique_ptr<Operation>> change_city_owner(const sim::World& world,
                                                                                  std::uint16_t island,
                                                                                  std::uint8_t city,
                                                                                  sim::PlayerId owner);

/// Land given to the city `city` of island `island` without a building that claims it, or taken
/// back (`take`): the editor's tool "Territory". `tiles` are tiles of the island. Given: each tile
/// that is land and free or the city's already becomes the city's and stays so while the city stands
/// (sim::WorldIsland::claimed); another city's land is passed over. Taken: each tile the editor
/// gave the city is given up; it stays the city's where a market, Kontor or tower of its owner covers
/// it or a building of the city stands on it. Fails (NotFound) without the city, (Unsupported) when
/// no tile changes.
[[nodiscard]] annokit::core::Result<std::unique_ptr<Operation>> claim_land(
    const sim::World& world, const assets::BuildingTable& buildings, std::uint16_t island, std::uint8_t city,
    const std::vector<std::pair<std::uint32_t, std::uint32_t>>& tiles, bool take);

}  // namespace o1602::editor
