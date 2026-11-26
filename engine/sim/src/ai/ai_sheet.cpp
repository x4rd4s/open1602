// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai_sheet.hpp"

#include <algorithm>

namespace o1602::sim::ai {

namespace {

/// `amount` a cycle of `interval` seconds, as so much a minute.
std::uint64_t per_minute_of_cycle(std::uint32_t amount, std::uint32_t interval) {
    return std::uint64_t{amount} * 60 / interval;
}

}  // namespace

Sheet::Sheet(const assets::BuildingTable& table) : m_food_ware(table.food_ware()), m_food_need(table.food_need()) {
    for (const assets::ResidentClass& k : table.resident_classes()) {
        m_classes.push_back(Class{&k, k.max_per_house, k.share, k.needs});
    }
    for (const assets::BuildingType& b : table.types()) {
        if (b.interval == 0) {
            continue;
        }
        Producer p;
        p.type = &b;
        p.ware = b.ware;
        p.raw = b.raw_ware;
        p.raw2 = b.raw2_ware;
        p.output = per_minute_of_cycle(b.produce_amount, b.interval);
        p.raw_use = per_minute_of_cycle(b.raw_amount, b.interval);
        p.raw2_use = per_minute_of_cycle(b.raw2_amount, b.interval);
        p.running_cost = b.running_cost;
        p.idle_cost = b.idle_cost;
        p.build_cost = b.build_cost;
        p.workers = b.workers;
        m_by_id.emplace_back(b.id, m_producers.size());
        m_producers.push_back(p);
    }
    // The first record of an id wins, as BuildingTable::by_id finds it.
    std::stable_sort(m_by_id.begin(), m_by_id.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
}

const Sheet::Producer* Sheet::producer(std::uint32_t id) const noexcept {
    const auto it = std::lower_bound(m_by_id.begin(), m_by_id.end(), id,
                                     [](const auto& e, std::uint32_t v) { return e.first < v; });
    return it != m_by_id.end() && it->first == id ? &m_producers[it->second] : nullptr;
}

const Sheet::Producer* Sheet::producer(const assets::BuildingType& b) const noexcept {
    auto it = std::lower_bound(m_by_id.begin(), m_by_id.end(), b.id,
                               [](const auto& e, std::uint32_t v) { return e.first < v; });
    for (; it != m_by_id.end() && it->first == b.id; ++it) {
        if (m_producers[it->second].type == &b) {
            return &m_producers[it->second];
        }
    }
    return nullptr;
}

std::uint32_t Sheet::max_per_house(std::size_t k) const noexcept {
    return k < m_classes.size() ? m_classes[k].max_per_house : 0;
}

std::uint64_t Sheet::need_amount(std::size_t k, std::uint8_t w) const noexcept {
    if (k >= m_classes.size()) {
        return 0;
    }
    std::uint64_t sum = 0;
    for (const assets::ResidentClass::Need& n : m_classes[k].needs) {
        sum += n.ware == w ? n.amount : 0U;
    }
    return sum;
}

std::int64_t Sheet::tax(std::size_t k, std::uint64_t people, std::uint32_t rate) const noexcept {
    return k < m_classes.size() ? class_tax(*m_classes[k].data, people, rate) : 0;
}

}  // namespace o1602::sim::ai
