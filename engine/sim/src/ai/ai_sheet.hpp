// SPDX-License-Identifier: GPL-3.0-or-later
//
// The data sheet of the economy for the computer players (P2.1): what the game's data says about
// its classes of inhabitants and its producers, read once from the building table, in the units the
// computers reckon with. Taxes, the room of a house, the goods each class asks for, what a producer
// makes and uses at full rate, what it costs to build and to run. Nothing here is a number of the
// computers' own: every entry is the table's, converted by the rules the simulation follows
// (consumption.hpp), and a package's classes and producers come in with the original's, by their
// place in the table, not by a fixed id or count.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/sim/consumption.hpp"

namespace o1602::sim::ai {

class Sheet {
public:
    /// A class of inhabitants (BuildingTable::resident_classes, in its order).
    struct Class {
        const assets::ResidentClass* data = nullptr;
        std::uint32_t max_per_house = 0;  ///< Maxwohn
        std::uint32_t share = 0;          ///< the supply it asks for (Prozent * 128 / 100)
        /// What it asks for besides food, per inhabitant and city tick, 1/256 of a stock unit.
        std::vector<assets::ResidentClass::Need> needs;
    };

    /// A building that works in cycles (an interval), in table order.
    struct Producer {
        const assets::BuildingType* type = nullptr;
        std::uint8_t ware = 0;  ///< what it makes
        std::uint8_t raw = 0;   ///< its raw materials
        std::uint8_t raw2 = 0;
        /// At full rate, 1/32 t a minute: what it makes and what it uses of each raw material. Its
        /// cycle alone (the interval); a farm's or a fisher's walks come on top
        /// (production_cycle.hpp, full_speed_seconds).
        std::uint64_t output = 0;
        std::uint64_t raw_use = 0;
        std::uint64_t raw2_use = 0;
        std::uint32_t running_cost = 0;  ///< gold a minute switched on
        std::uint32_t idle_cost = 0;     ///< gold a minute switched off
        assets::BuildingType::BuildCost build_cost;
        std::uint16_t workers = 0;
    };

    explicit Sheet(const assets::BuildingTable& table);

    [[nodiscard]] std::span<const Class> classes() const noexcept { return m_classes; }
    [[nodiscard]] std::span<const Producer> producers() const noexcept { return m_producers; }
    /// The producer of record `id`, nullptr for a building without a cycle.
    [[nodiscard]] const Producer* producer(std::uint32_t id) const noexcept;
    /// The producer of record `b` of the table, nullptr for a building without a cycle.
    [[nodiscard]] const Producer* producer(const assets::BuildingType& b) const noexcept;

    [[nodiscard]] std::uint8_t food_ware() const noexcept { return m_food_ware; }
    /// Food every inhabitant asks for a city tick, 1/256 of a stock unit.
    [[nodiscard]] std::uint32_t food_need() const noexcept { return m_food_need; }

    /// Maxwohn of class k, 0 for a class the table does not have.
    [[nodiscard]] std::uint32_t max_per_house(std::size_t k) const noexcept;
    /// What an inhabitant of class k asks for of ware w a city tick besides food (1/256 of a stock
    /// unit), the sum of its needs of w; 0 for a class the table does not have.
    [[nodiscard]] std::uint64_t need_amount(std::size_t k, std::uint8_t w) const noexcept;
    /// The taxes a minute of `people` of class k at tax rate `rate` (consumption.hpp, class_tax);
    /// 0 for a class the table does not have.
    [[nodiscard]] std::int64_t tax(std::size_t k, std::uint64_t people, std::uint32_t rate) const noexcept;

    /// `units` asked for each city tick (1/256 of a stock unit), as 1/32 t a minute.
    [[nodiscard]] static constexpr std::uint64_t per_minute(std::uint64_t units) noexcept {
        return units * kCityTicksPerMinute / kSupplyUnitsPerStockUnit;
    }

private:
    std::vector<Class> m_classes;
    std::vector<Producer> m_producers;
    std::vector<std::pair<std::uint32_t, std::size_t>> m_by_id;  ///< record id, index into m_producers; sorted
    std::uint8_t m_food_ware = 0;
    std::uint32_t m_food_need = 0;
};

}  // namespace o1602::sim::ai
