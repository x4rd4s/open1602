// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/tick_trail.hpp"

#include <algorithm>
#include <cmath>

namespace o1602::render {

void tick_lerp(double was_x, double was_y, double now_x, double now_y, double t, double max_jump, double& x,
               double& y) noexcept {
    x = now_x;
    y = now_y;
    if (std::abs(now_x - was_x) > max_jump || std::abs(now_y - was_y) > max_jump) {
        return;
    }
    t = std::clamp(t, 0.0, 1.0);
    x = was_x + (now_x - was_x) * t;
    y = was_y + (now_y - was_y) * t;
}

void TickTrail::sort() {
    // Units mostly come in the order of their keys already.
    if (!std::is_sorted(m_was.begin(), m_was.end(), [](const Was& a, const Was& b) { return a.key < b.key; })) {
        std::sort(m_was.begin(), m_was.end(), [](const Was& a, const Was& b) { return a.key < b.key; });
    }
}

void TickTrail::position(std::uint64_t key, double& x, double& y) const noexcept {
    const auto it =
        std::lower_bound(m_was.begin(), m_was.end(), key, [](const Was& w, std::uint64_t k) { return w.key < k; });
    if (it == m_was.end() || it->key != key) {
        return;
    }
    tick_lerp(it->x, it->y, x, y, m_fraction, kMaxStep, x, y);
}

}  // namespace o1602::render
