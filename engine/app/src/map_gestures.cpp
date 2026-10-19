// SPDX-License-Identifier: GPL-3.0-or-later
#include "map_gestures.hpp"

#include <algorithm>
#include <cmath>

namespace o1602::app {

void LeftPress::move(double x, double y) {
    if (!m_at) {
        return;
    }
    const double dx = x - m_at->first;
    const double dy = y - m_at->second;
    m_rubber_band = m_rubber_band || dx * dx + dy * dy >= 25.0;  // 5 px
}

bool MapDrag::move(double dx, double dy) {
    if (m_dragging) {
        m_distance += std::abs(dx) + std::abs(dy);
    }
    return m_dragging;
}

bool GroupKey::press(std::size_t group, std::uint64_t now_ms) {
    const bool twice = m_last == group && now_ms - m_last_ms < kTwiceMs;
    m_last = group;
    m_last_ms = now_ms;
    return twice;
}

std::optional<std::size_t> next_idle(const std::vector<ui::UnitRef>& idle, std::optional<ui::UnitRef> selected) {
    if (idle.empty()) {
        return std::nullopt;
    }
    if (!selected) {
        return 0;
    }
    const auto it = std::find(idle.begin(), idle.end(), *selected);
    return it == idle.end() ? 0 : static_cast<std::size_t>(it - idle.begin() + 1) % idle.size();
}

}  // namespace o1602::app
