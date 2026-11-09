// SPDX-License-Identifier: GPL-3.0-or-later
// Smacker videos of the original data (docs/formats/smk.md).

#include <algorithm>
#include <filesystem>
#include <optional>

#include "../test_framework.hpp"

#include "annokit/core/file.hpp"
#include "annokit/video/smacker.hpp"

#include "o1602/assets/game_directory.hpp"

using namespace o1602;

TEST_CASE("data: a video decodes completely, read whole or frame by frame, its sound lasts as long as its frames") {
    auto game = assets::GameDirectory::from_environment();
    if (!game) {
        SKIP("ANNO1602_DIR not set");
    }
    // Any video the installation (or the test copy) has; they all share one layout.
    std::optional<std::filesystem::path> path;
    for (const char* name : {"VideoSmk/911.smk", "VideoSmk/282.smk", "TOOLGFX/6s.smk"}) {
        path = game->resolve(name);
        if (path) {
            break;
        }
    }
    if (!path) {
        SKIP("no Smacker video in the game directory");
    }
    auto bytes = annokit::core::read_file(*path);
    REQUIRE(bytes.ok());
    auto video = annokit::video::Smacker::open(std::move(*bytes));
    REQUIRE(video.ok());
    auto streamed = annokit::video::Smacker::open_file(*path);  // FB-14: the same frames, read as needed
    REQUIRE(streamed.ok());
    const auto& info = video->info();
    CHECK(info.version == 2);
    CHECK(info.frame_us == 66660);  // 15 frames a second
    CHECK(info.tracks[0].present);
    std::size_t palettes = 0;
    std::size_t lit = 0;
    while (!video->finished()) {
        REQUIRE(video->next().ok());
        REQUIRE(streamed->next().ok());
        CHECK(streamed->palette() == video->palette());
        CHECK(std::equal(streamed->pixels().begin(), streamed->pixels().end(), video->pixels().begin(),
                         video->pixels().end()));
        palettes += video->palette_changed() ? 1U : 0U;
        for (const std::uint8_t p : video->pixels()) {
            lit += p != 0 ? 1U : 0U;
        }
    }
    CHECK(palettes >= 1);
    CHECK(lit > 0);
    for (std::size_t t = 0; t < annokit::video::kSmackerTracks; ++t) {
        if (!info.tracks[t].present) {
            continue;
        }
        auto pcm = video->audio(t);
        REQUIRE(pcm.ok());
        CHECK(streamed->audio(t)->samples == pcm->samples);
        const auto video_ms = std::uint64_t{info.frames} * info.frame_us / 1000;
        CHECK(pcm->duration_ms() + 200 >= video_ms);
        CHECK(pcm->duration_ms() <= video_ms + 200);
    }
}
