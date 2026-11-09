// SPDX-License-Identifier: GPL-3.0-or-later
// Golden hashes (DS-2): how a game develops must be the same on every platform CI builds (GCC,
// Clang, MSVC, Apple Clang), or a multiplayer game between them desyncs. A test runs a small
// synthetic game and compares the fingerprint of its world with the number written in the test. A
// change that alters the game, or the savegame's sections, on purpose updates the number and says
// why in its PR; the test prints the new one.
#pragma once

#include <cstdint>
#include <cstdio>

#include "../test_framework.hpp"

#include "annokit/core/hash.hpp"

#include "o1602/sim/save_game.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::test {

/// XXH64 over the world as an unpacked savegame (without a running simulation's state).
inline std::uint64_t world_hash(const sim::World& world, const assets::BuildingTable& buildings) {
    auto bytes = sim::write_game(world, buildings, nullptr, sim::SaveOptions{false});
    REQUIRE(bytes.ok());
    return annokit::core::xxh64(*bytes);
}

/// Checks `hash` against the golden value, printing the hash when they differ.
inline void check_golden(const char* what, std::uint64_t hash, std::uint64_t expected) {
    if (hash != expected) {
        std::printf("  %s: state hash 0x%016llx\n", what, static_cast<unsigned long long>(hash));
    }
    CHECK(hash == expected);
}

}  // namespace o1602::test
