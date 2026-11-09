// SPDX-License-Identifier: GPL-3.0-or-later
//
// Random numbers of the simulation (ADR 0004). The original draws everything from the C library's
// rand(), shared with presentation code that only runs for what is on screen (M-006), so two
// computers diverge. Open1602 gives each subsystem its own generator, kept in the world state:
// the same world gives the same game on every computer, and drawing for one subsystem never
// changes another one.
#pragma once

#include <array>
#include <cstdint>

#include "annokit/core/random.hpp"

namespace o1602::sim {

/// PCG32 (O'Neill, annokit::core): small, fast, good statistics, and fully specified, so results
/// are the same with every compiler and standard library.
using Random = annokit::core::Random;

/// One generator per subsystem.
enum class RandomStream : std::uint8_t {
    Houses,        ///< house variants on upgrades
    Events,        ///< fires, plague, robbers
    Animals,       ///< deer
    Pirates,       ///< what pirates take
    Construction,  ///< random bits of new buildings (house timer group, growth class), ruins
    Islands,       ///< droughts and the fields they wither
    Ships,         ///< courses, the free trader, ship names
    Military,      ///< soldiers wandering, plans after a failure
    Ai,            ///< the computer players (house and field variants, ship types, patrols)
    Count,
};

/// Seed of the simulation's generators when a game names none.
inline constexpr std::uint64_t kDefaultSimulationSeed = 1602;

/// The generators of a world, all derived from one seed.
class RandomStreams {
public:
    explicit constexpr RandomStreams(std::uint64_t seed = kDefaultSimulationSeed) noexcept { reseed(seed); }
    constexpr void reseed(std::uint64_t seed) noexcept {
        for (std::size_t i = 0; i < m_streams.size(); ++i) {
            m_streams[i] = Random(seed, 0x5DEECE66DULL + i);
        }
    }
    [[nodiscard]] constexpr Random& operator[](RandomStream s) noexcept {
        return m_streams[static_cast<std::size_t>(s)];
    }
    [[nodiscard]] constexpr const Random& operator[](RandomStream s) const noexcept {
        return m_streams[static_cast<std::size_t>(s)];
    }

private:
    std::array<Random, static_cast<std::size_t>(RandomStream::Count)> m_streams;
};

}  // namespace o1602::sim
