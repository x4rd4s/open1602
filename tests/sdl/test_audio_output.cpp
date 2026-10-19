// SPDX-License-Identifier: GPL-3.0-or-later
// The sound device (engine/platform/audio_output.cpp) on SDL's dummy audio driver, which plays in
// real time without a sound card.

#include <SDL3/SDL.h>
#include <cstdint>
#include <vector>

#include "../test_framework.hpp"
#include "sdl_support.hpp"

#include "o1602/platform/audio_output.hpp"

using namespace o1602;

namespace {

/// The game's top-up (GameSound::top_up): queue frames until `ms` milliseconds wait.
void top_up(platform::AudioOutput& output, std::uint32_t ms) {
    const std::size_t target = std::size_t{output.rate()} * ms / 1000;
    const std::size_t queued = output.queued_frames();
    if (queued < target) {
        const std::vector<std::int16_t> silence((target - queued) * 2, 0);
        output.push(silence.data(), target - queued);
    }
}

}  // namespace

TEST_CASE("audio output: opens the default device at the rate asked for, with nothing queued") {
    test::use_dummy_drivers();
    auto output = platform::AudioOutput::open(22050);
    REQUIRE(output.ok());
    CHECK((*output)->rate() == 22050);
    CHECK((*output)->queued_frames() == 0);
    CHECK(SDL_WasInit(SDL_INIT_AUDIO) != 0);
    output = platform::AudioOutput::open(44100);  // a second device while the first is open
    REQUIRE(output.ok());
    CHECK((*output)->rate() == 44100);
}

TEST_CASE("audio output: closing the device leaves SDL's audio subsystem as it was") {
    test::use_dummy_drivers();
    {
        auto output = platform::AudioOutput::open();
        REQUIRE(output.ok());
    }
    CHECK(SDL_WasInit(SDL_INIT_AUDIO) == 0);
}

TEST_CASE("audio output: pushed frames are queued, four bytes a stereo frame") {
    test::use_dummy_drivers();
    auto output = platform::AudioOutput::open();
    REQUIRE(output.ok());
    platform::AudioOutput& out = **output;
    out.push(nullptr, 0);  // nothing to push is no error
    CHECK(out.queued_frames() == 0);

    // A second of sound: the device takes at most a buffer or two of it at once.
    const std::vector<std::int16_t> frames(std::size_t{44100} * 2, 1000);
    out.push(frames.data(), 44100);
    const std::size_t queued = out.queued_frames();
    CHECK(queued <= 44100);
    CHECK(queued >= 44100 - 8192);
}

TEST_CASE("audio output: a prefill tops the queue up to its target, and the device plays it") {
    test::use_dummy_drivers();
    auto output = platform::AudioOutput::open();
    REQUIRE(output.ok());
    platform::AudioOutput& out = **output;
    top_up(out, 250);
    const std::size_t prefilled = out.queued_frames();
    CHECK(prefilled <= 44100 / 4);
    CHECK(prefilled >= 44100 / 4 - 8192);

    // Topping up again adds only what is missing.
    top_up(out, 250);
    CHECK(out.queued_frames() <= 44100 / 4);

    // The dummy device plays in real time: the queue empties within its quarter of a second.
    const std::uint64_t start = SDL_GetTicks();
    while (out.queued_frames() != 0 && SDL_GetTicks() - start < 3000) {
        SDL_Delay(10);
    }
    CHECK(out.queued_frames() == 0);
}
