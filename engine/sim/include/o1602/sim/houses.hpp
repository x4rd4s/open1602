// SPDX-License-Identifier: GPL-3.0-or-later
//
// Houses: who lives where, and how houses grow, rise to the next class, fall back or are
// abandoned.
//
// Every house is visited about every 15.5 s (16 timer groups, 15 s + 64 ms * group). Its growth
// rate follows the city's food supply, the satisfaction of its class and the public buildings in
// reach: a fully supplied house grows by 8-12 % per visit, a neglected one shrinks. A full house
// queues to try out the next class; if the people are content, the services of the next class are
// there and the warehouse has the building material, it becomes a house of that class. A house
// that loses its people falls back a class, gives its people to the neighbours or turns into a
// ruin.
#pragma once

#include <cstdint>
#include <set>
#include <span>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/resident_classes.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

/// Bits of House::services (house + 8 in the original): public buildings in reach.
namespace house_service {
inline constexpr std::uint16_t kDisasterMask = 0x0003;  ///< 1 plague, 2 fire
inline constexpr std::uint16_t kKapelle = 1U << 2;
inline constexpr std::uint16_t kKirche = 1U << 3;
inline constexpr std::uint16_t kWirtshaus = 1U << 4;
inline constexpr std::uint16_t kSchule = 1U << 5;
inline constexpr std::uint16_t kBadehaus = 1U << 6;
inline constexpr std::uint16_t kTheater = 1U << 7;
inline constexpr std::uint16_t kHochschule = 1U << 8;
inline constexpr std::uint16_t kKlinik = 1U << 9;
inline constexpr std::uint16_t kKontor = 1U << 10;
inline constexpr std::uint16_t kGalgen = 1U << 11;
inline constexpr std::uint16_t kBrunnen = 1U << 12;
/// Public buildings of a kind of their own (assets::public_building::kOwn, own_buildings.toml).
inline constexpr assets::ServiceBits kOwnMask = assets::public_building::kOwnMask;
}  // namespace house_service

/// Bits of the house panel's public buildings (the icons of
/// SIEDLER): another layout than House::services.
namespace panel_service {
inline constexpr std::uint32_t kMarkt = 0x01;
inline constexpr std::uint32_t kKapelle = 0x02;
inline constexpr std::uint32_t kSchule = 0x04;
inline constexpr std::uint32_t kWirtshaus = 0x08;
inline constexpr std::uint32_t kKirche = 0x10;
inline constexpr std::uint32_t kBadehaus = 0x20;
inline constexpr std::uint32_t kTheater = 0x40;
inline constexpr std::uint32_t kHochschule = 0x80;
inline constexpr std::uint32_t kKlinik = 0x100;
inline constexpr std::uint32_t kGalgen = 0x200;
inline constexpr std::uint32_t kBrunnen = 0x8000;
}  // namespace panel_service

/// Period of house timer group `group`.
[[nodiscard]] constexpr std::uint32_t house_group_period_ms(std::uint32_t group) noexcept {
    return 15'000U + 64U * group;
}

struct HouseEvent {
    enum class Kind : std::uint8_t {
        Upgraded,    ///< became a house of the next class
        Downgraded,  ///< fell back a class
        Abandoned,   ///< nobody lives there any more: ruins
    };
    Kind kind = Kind::Upgraded;
    std::uint16_t island = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint8_t resident_class = 0;  ///< class after the change
};

/// Whether a house has the public buildings class `resident_class` needs: pioneers a
/// market or Kontor; settlers a market and a chapel or church; citizens also a tavern and a
/// school; merchants a church and a bath house; aristocrats a college and a theatre. A class a
/// package added needs what the original's class it looks like needs (ResidentClass::look).
[[nodiscard]] bool has_services(const House& house, std::uint8_t resident_class) noexcept;
/// has_services for class `resident_class` of `buildings`, also one a package added: the public
/// buildings classes.toml gave the class (ResidentClass::public_needs), else those of its look.
[[nodiscard]] bool has_services(const House& house, const assets::BuildingTable& buildings,
                                std::uint8_t resident_class) noexcept;
/// The class of the original whose public buildings class `resident_class` needs.
[[nodiscard]] std::uint8_t service_class(const assets::BuildingTable& buildings, std::uint8_t resident_class) noexcept;

/// The public buildings in reach of a house in the panel's layout .
/// A church also counts as a chapel and a college as a school; a Kontor is no market.
[[nodiscard]] std::uint32_t panel_services(const House& house) noexcept;

/// What class `resident_class` needs of public buildings in the panel's layout,
/// from the same table as has_services: of each choice the
/// market, else the lesser building (a chapel for a chapel or church). 0 beyond the table.
[[nodiscard]] std::uint32_t required_panel_services(std::uint8_t resident_class) noexcept;
/// required_panel_services for class `resident_class` of `buildings`, as has_services reads it.
[[nodiscard]] std::uint32_t required_panel_services(const assets::BuildingTable& buildings,
                                                    std::uint8_t resident_class) noexcept;

/// The needs of class `resident_class` of `buildings` as has_services reads them: one entry per
/// need, each the service bits of which any one satisfies it (0: none needed).
[[nodiscard]] std::span<const assets::ServiceBits> class_needs(const assets::BuildingTable& buildings,
                                                               std::uint8_t resident_class) noexcept;

/// The service bits a building gives the houses it reaches (House::services); 0 for a market,
/// which sets House::market instead, and for a building that gives none.
[[nodiscard]] assets::ServiceBits service_of(const assets::BuildingType& building) noexcept;

/// Whether a building of `w` x `h` tiles at (`x`, `y`) with `radius` reaches a house of `hw` x `hh`
/// tiles at (`hx`, `hy`), as update_services decides it: the gap from the building's centre
/// column(s) and row(s) to the nearest tile of the house lies within the radius's circle.
[[nodiscard]] bool reaches(std::int32_t x, std::int32_t y, std::uint32_t w, std::uint32_t h, std::uint8_t radius,
                           std::int32_t hx, std::int32_t hy, std::uint32_t hw, std::uint32_t hh);

/// Recomputes which markets, Kontors and public buildings reach each house of island `island`:
/// buildings of the same city whose Radius covers the house from their centre.
void update_services(World& world, const assets::BuildingTable& buildings, std::uint16_t island);
/// The same for the houses of `cities` on island `island` only (a building changes only its own
/// city's services).
void update_services(World& world, const assets::BuildingTable& buildings, std::uint16_t island,
                     const std::set<std::uint8_t>& cities);
/// The same for every island.
void update_services(World& world, const assets::BuildingTable& buildings);

/// What the growth of a house depends on, as its city and its own state give it (growth_rate).
struct GrowthInputs {
    std::uint8_t satisfaction = 0;     ///< of its class (0..128); 0 for a house above the pioneers without a market
    std::uint8_t food = 0;             ///< the city's food supply (0..128); 0 for a house no market or Kontor feeds
    std::uint8_t market_distance = 0;  ///< House::market_distance
    bool services_complete = false;    ///< House::services_complete
    std::uint8_t disaster = 0;         ///< plague or fire on the house (House::services, kDisasterMask)
    bool city_disasters = false;       ///< a fire or plague anywhere in the city
};

/// The inputs of a house's growth from its city and itself.
[[nodiscard]] GrowthInputs growth_inputs(const City& city, const House& house) noexcept;

/// Growth rate of a house: positive grows by rate * 6/128 of its inhabitants per
/// visit, negative shrinks by |rate| * 4/128, 0 stays.
[[nodiscard]] std::int32_t growth_rate(const GrowthInputs& in) noexcept;
[[nodiscard]] std::int32_t growth_rate(const City& city, const House& house) noexcept;

/// One visit's change of a house's inhabitants (in 1/64, House::inhabitants64) at growth rate
/// `rate`, within the room `cap` (1/64) of its class: what grows beyond it is lost. Moves to the
/// neighbours, a class up or down and an empty house's ruin are the visit's own (advance_houses).
[[nodiscard]] std::uint32_t grown_inhabitants64(std::uint32_t inhabitants64, std::int32_t rate,
                                                std::uint32_t cap) noexcept;

/// How many times a house of a timer group is visited in the next `ms` of game time, from the
/// group's clock `clock_ms` (HouseClocks::time_ms) and its period (house_group_period_ms), for steps
/// shorter than the period.
[[nodiscard]] constexpr std::uint64_t house_visits(std::uint32_t clock_ms, std::uint32_t period_ms,
                                                   std::uint64_t ms) noexcept {
    return (std::uint64_t{clock_ms} + ms) / period_ms;
}

/// A house's inhabitants (1/64) after the next `ms` of game time at fixed inputs: the visits of its
/// timer group `group` from its clock `clock_ms`, each as grown_inhabitants64 within `cap`. An
/// empty house stays empty.
[[nodiscard]] std::uint32_t project_house(const GrowthInputs& in, std::uint32_t inhabitants64, std::uint32_t cap,
                                          std::uint32_t group, std::uint32_t clock_ms, std::uint64_t ms) noexcept;

/// Advances the house timer groups by `dt_ms` and visits the houses of every group that ticked.
void advance_houses(World& world, const assets::BuildingTable& buildings, std::uint32_t dt_ms,
                    std::vector<HouseEvent>* events);

}  // namespace o1602::sim
