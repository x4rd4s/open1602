// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/cities.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "o1602/editor/islands.hpp"
#include "o1602/editor/terrain.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/logistics.hpp"

namespace o1602::editor {

using annokit::core::ErrorCode;
using annokit::core::make_error;

namespace {

annokit::core::Result<std::unique_ptr<Operation>> no_city(std::uint16_t island, std::uint8_t city) {
    return make_error(ErrorCode::NotFound,
                      "no city " + std::to_string(city) + " on island " + std::to_string(island + 1));
}

std::unique_ptr<Operation> edit(sim::World after, const char* name) {
    return std::make_unique<WorldEdit>(std::move(after), name);
}

/// Tiles given to a city or taken back (claim_land): each tile's claim and city before and after.
class ClaimEdit final : public Operation {
public:
    struct Change {
        std::uint16_t x = 0;
        std::uint16_t y = 0;
        std::uint8_t claimed_before = sim::MapCell::kNoCity;
        std::uint8_t claimed_after = sim::MapCell::kNoCity;
        std::uint8_t city_before = sim::MapCell::kNoCity;
        std::uint8_t city_after = sim::MapCell::kNoCity;
        sim::PlayerId owner_before = sim::kNoPlayer;
        sim::PlayerId owner_after = sim::kNoPlayer;
    };
    ClaimEdit(std::uint16_t island, std::vector<Change> changes) noexcept
        : m_island(island), m_changes(std::move(changes)) {}

    void apply(sim::World& world) override { set(world, true); }
    void revert(sim::World& world) override { set(world, false); }
    [[nodiscard]] std::size_t memory() const noexcept override {
        return sizeof(*this) + m_changes.capacity() * sizeof(Change);
    }
    [[nodiscard]] std::string_view kind() const noexcept override { return "territory"; }

private:
    void set(sim::World& world, bool after) {
        sim::WorldIsland& isl = world.island(m_island);
        const std::size_t size = std::size_t{isl.map.width()} * isl.map.height();
        if (isl.claimed.size() != size) {
            isl.claimed.assign(size, sim::MapCell::kNoCity);
        }
        for (const Change& c : m_changes) {
            isl.claimed[std::size_t{c.y} * isl.map.width() + c.x] = after ? c.claimed_after : c.claimed_before;
            isl.map.set_city(c.x, c.y, after ? c.city_after : c.city_before, after ? c.owner_after : c.owner_before);
        }
        // An island without any claim keeps none.
        if (std::all_of(isl.claimed.begin(), isl.claimed.end(),
                        [](std::uint8_t v) { return v == sim::MapCell::kNoCity; })) {
            isl.claimed.clear();
        }
    }
    std::uint16_t m_island;
    std::vector<Change> m_changes;
};

}  // namespace

annokit::core::Result<std::unique_ptr<Operation>> claim_land(
    const sim::World& world, const assets::BuildingTable& buildings, std::uint16_t island, std::uint8_t city,
    const std::vector<std::pair<std::uint32_t, std::uint32_t>>& tiles, bool take) {
    const sim::City* c = world.city(island, city);
    if (c == nullptr || city >= sim::kCitySlotsPerIsland) {
        return no_city(island, city);
    }
    const sim::WorldIsland& isl = world.islands()[island];
    const sim::IslandMap& map = isl.map;
    const std::vector<sim::CityMask> covered =
        take ? sim::building_coverage(world, buildings, island) : std::vector<sim::CityMask>{};
    std::vector<ClaimEdit::Change> changes;
    std::vector<bool> seen(std::size_t{map.width()} * map.height(), false);
    for (const auto& [x, y] : tiles) {
        if (x >= map.width() || y >= map.height()) {
            continue;
        }
        const std::size_t i = std::size_t{y} * map.width() + x;
        if (seen[i]) {
            continue;
        }
        seen[i] = true;
        const sim::MapCell& top = map.top(x, y);
        const assets::BuildingType* b = top.empty() ? nullptr : buildings.by_sprite(top.sprite);
        if (b == nullptr || terrain_level(*b) == 0) {
            continue;  // never the sea or the surf
        }
        ClaimEdit::Change ch{static_cast<std::uint16_t>(x),
                             static_cast<std::uint16_t>(y),
                             isl.claimed_city(x, y),
                             isl.claimed_city(x, y),
                             top.city,
                             top.city,
                             top.owner,
                             top.owner};
        if (!take) {
            if ((top.city != sim::MapCell::kNoCity && top.city != city) || ch.claimed_before == city) {
                continue;  // another city's land, or given already
            }
            ch.claimed_after = city;
            ch.city_after = city;
            ch.owner_after = c->owner;
        } else {
            if (ch.claimed_before != city) {
                continue;
            }
            ch.claimed_after = sim::MapCell::kNoCity;
            // Still the city's where its buildings claim it, or where one of them stands.
            const bool held = (covered[i] >> city & 1U) != 0;
            const bool built = !is_terrain(buildings, *b) && b->kind != assets::kind::kRuin;
            if (!held && !built) {
                ch.city_after = sim::MapCell::kNoCity;
                ch.owner_after = sim::kNoPlayer;
            }
        }
        changes.push_back(ch);
    }
    if (changes.empty()) {
        return make_error(ErrorCode::Unsupported, "no tile changes");
    }
    return std::unique_ptr<Operation>(std::make_unique<ClaimEdit>(island, std::move(changes)));
}

annokit::core::Result<std::unique_ptr<Operation>> rename_city(const sim::World& world, std::uint16_t island,
                                                              std::uint8_t city, std::string_view name) {
    if (world.city(island, city) == nullptr) {
        return no_city(island, city);
    }
    if (!sim::valid_name(name, kMaxCityName)) {
        return make_error(ErrorCode::InvalidFormat, "a city cannot be named so");
    }
    sim::World after = world;
    after.city(island, city)->name = std::string(name);
    return edit(std::move(after), "city_rename");
}

std::uint32_t city_room(const sim::World& world, const assets::BuildingTable& buildings, std::uint16_t island,
                        std::uint8_t city) {
    const auto capacities = sim::city_capacities(world, buildings);
    const auto it = capacities.find(std::uint32_t{island} << 8 | city);
    return it == capacities.end() ? 0 : it->second;
}

annokit::core::Result<std::unique_ptr<Operation>> set_city_goods(const sim::World& world,
                                                                 const assets::BuildingTable& buildings,
                                                                 std::uint16_t island, std::uint8_t city,
                                                                 std::uint8_t ware, std::uint32_t amount) {
    if (world.city(island, city) == nullptr) {
        return no_city(island, city);
    }
    if (!world.goods().is_trade(ware)) {
        return make_error(ErrorCode::InvalidFormat, "ware " + std::to_string(ware) + " is no trade good");
    }
    sim::World after = world;
    sim::City& c = *after.city(island, city);
    c.ensure_ware(ware);
    c.goods[ware] = std::min(amount, city_room(world, buildings, island, city));
    return edit(std::move(after), "city_goods");
}

annokit::core::Result<std::unique_ptr<Operation>> set_city_trade(const sim::World& world,
                                                                 const assets::BuildingTable& buildings,
                                                                 std::uint16_t island, std::uint8_t city,
                                                                 const sim::TradeSetting& setting) {
    const std::uint32_t room = city_room(world, buildings, island, city);
    if (world.city(island, city) == nullptr || room == 0) {
        return no_city(island, city);
    }
    if (!world.goods().is_trade(setting.ware)) {
        return make_error(ErrorCode::InvalidFormat, "ware " + std::to_string(setting.ware) + " is no trade good");
    }
    sim::TradeSetting s = setting;
    if (s.amount) {
        s.amount = std::min(*s.amount, room);
    }
    sim::World after = world;
    if (!sim::apply_trade_setting(after, buildings, *after.city(island, city), s, room)) {
        return make_error(ErrorCode::Unsupported, "the Kontor trades in no more wares");
    }
    if (after.city(island, city)->trade == world.city(island, city)->trade) {
        return make_error(ErrorCode::Unsupported, "the trade is so already");
    }
    return edit(std::move(after), "city_trade");
}

annokit::core::Result<std::unique_ptr<Operation>> reset_city_trade(const sim::World& world, std::uint16_t island,
                                                                   std::uint8_t city) {
    if (world.city(island, city) == nullptr) {
        return no_city(island, city);
    }
    sim::World after = world;
    sim::City& c = *after.city(island, city);
    if (after.acts_as(c.owner, sim::FactionBehaviour::Natives)) {
        sim::native_village_offer(after, after.islands()[island], c);
    } else {
        for (sim::City::Trade& t : c.trade) {
            t.sell = false;
            t.buy = false;
        }
    }
    return edit(std::move(after), "city_trade");
}

annokit::core::Result<std::unique_ptr<Operation>> change_city_owner(const sim::World& world, std::uint16_t island,
                                                                    std::uint8_t city, sim::PlayerId owner) {
    const sim::City* c = world.city(island, city);
    if (c == nullptr) {
        return no_city(island, city);
    }
    const sim::Player* p = world.player(owner);
    if (p == nullptr) {
        return make_error(ErrorCode::NotFound, "no player " + std::to_string(owner));
    }
    if (p->role != sim::PlayerRole::Human && p->role != sim::PlayerRole::Computer &&
        !world.acts_as(owner, sim::FactionBehaviour::Natives) &&
        !world.acts_as(owner, sim::FactionBehaviour::Pirates)) {
        return make_error(ErrorCode::Unsupported, "only a human, a computer, the natives or the pirates take a city");
    }
    if (world.acts_as(c->owner, sim::FactionBehaviour::Neutral)) {
        // Their buildings stand without a Kontor, which every other city has.
        return make_error(ErrorCode::Unsupported, "the neutral buildings go over only to soldiers");
    }
    if (c->owner == owner) {
        return make_error(ErrorCode::Unsupported, "the city is the player's already");
    }
    const bool has_one = std::any_of(world.cities().begin(), world.cities().end(), [&](const sim::City& other) {
        return other.island == island && other.owner == owner;
    });
    if (has_one) {
        return make_error(ErrorCode::Unsupported, "the player has a city on the island already");
    }
    sim::World after = world;
    sim::IslandMap& map = after.island(island).map;
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            if (map.top(x, y).city == city) {
                map.set_city(x, y, city, owner);
            }
        }
    }
    sim::City& changed = *after.city(island, city);
    changed.owner = owner;
    // Natives trade for goodwill at price 0: a village's offer for a new village, and nothing for
    // a city that stops being one, so that it sells nothing for free.
    const bool was_village = world.acts_as(c->owner, sim::FactionBehaviour::Natives);
    const bool is_village = after.acts_as(owner, sim::FactionBehaviour::Natives);
    if (is_village && !was_village) {
        sim::native_village_offer(after, after.islands()[island], changed);
    } else if (was_village && !is_village) {
        for (sim::City::Trade& t : changed.trade) {
            t = sim::City::Trade{};
        }
    }
    changed.ledger.clear();  // the new owner's statistics start with the city
    changed.history.clear();
    return edit(std::move(after), "city_owner");
}

}  // namespace o1602::editor
