// SPDX-License-Identifier: GPL-3.0-or-later
//
// Improved computers: the world as the player in a computer's seat sees it (P1.2 of the rebuild).
// Every query says which of three things it reads: the computer's own cities and units, what a
// human in its seat sees of the others (cities and units on islands it discovered, a partner's
// Kontor it knows), or the islands it knows. The hardest level sees more (omniscient_level), at
// this one place. Everything goes by ware id and the game's tables: no list of goods, classes or
// regions of its own.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "ai_engine.hpp"

namespace o1602::sim::ai {

/// What a partner's Kontor shows of a ware on its trade window: its price and what it offers.
struct KontorOffer {
    std::uint16_t price = 0;
    std::uint64_t offered = 0;  ///< 1/32 t (Engine::kontor_offer)
};

class View {
public:
    View(const Engine& engine, const AiPlayer& viewer) : m_engine(engine), m_viewer(viewer) {}

    // Own cities and units.
    [[nodiscard]] std::vector<const City*> own_cities() const;
    [[nodiscard]] std::uint32_t own_city_count() const;
    [[nodiscard]] std::uint32_t own_population() const;
    /// What the producers of `ware` make in `city`, 1/32 t a minute; 0 for a city not the viewer's.
    [[nodiscard]] std::uint32_t own_supply(const City& city, std::uint8_t ware) const;
    [[nodiscard]] const Overview& overview() const;
    [[nodiscard]] WareView ware(std::uint8_t ware, std::uint32_t minutes,
                                std::optional<std::uint16_t> island = std::nullopt) const;

    // Islands.
    [[nodiscard]] bool knows_island(std::uint16_t island) const;
    [[nodiscard]] bool knows_crops(std::uint16_t island) const;
    /// An island's fertility for a crop or resource (0, 64 or 128), once the viewer knows its crops.
    [[nodiscard]] std::optional<std::uint32_t> fertility(std::uint16_t island, std::uint8_t ware) const;
    /// Whether the island grows `crop`: a full field (128), or for ore a big or small vein. False
    /// for an island whose crops the viewer does not know.
    [[nodiscard]] bool grows(std::uint16_t island, std::uint8_t crop) const;
    /// Crops of an island that grow fully among spices, tobacco, wine, cotton, cocoa, sugar; 0 for
    /// an island whose crops the viewer does not know.
    [[nodiscard]] std::uint32_t full_crops(std::uint16_t island) const;
    /// A vein of the ware (ore, gold) on the island outside every city's land, on an island whose
    /// crops (and so veins) the viewer knows.
    [[nodiscard]] bool free_deposit(std::uint16_t island, std::uint8_t ware) const;
    /// An island's size class: its outline is on every player's map from the start (the minimap
    /// draws an undiscovered island's ground), so it is not gated.
    [[nodiscard]] std::uint32_t island_size(std::uint16_t island) const;

    // Other players, as seen.
    /// The cities of `owner` the viewer knows of (on islands it discovered, Engine::city_known).
    [[nodiscard]] std::vector<const City*> seen_cities(PlayerId owner) const;
    [[nodiscard]] std::uint32_t seen_city_count(PlayerId owner) const;
    /// The houses of those cities, as a human counts them on the map (not their inhabitants).
    [[nodiscard]] std::uint32_t seen_houses(PlayerId owner) const;
    [[nodiscard]] std::uint32_t seen_army(PlayerId owner) const;
    [[nodiscard]] std::uint32_t seen_cannons(PlayerId owner) const;
    /// What a partner's Kontor shows of `ware` while the viewer knows it; none for a Kontor it does
    /// not know, one of its own, or a ware the Kontor does not sell.
    [[nodiscard]] std::optional<KontorOffer> kontor_offer(const City& partner, std::uint8_t ware) const;

    /// The level that sees every unit and Kontor of the others (Engine::sees_all): the one switch.
    [[nodiscard]] bool omniscient_level() const;

private:
    const Engine& m_engine;
    const AiPlayer& m_viewer;
};

}  // namespace o1602::sim::ai
