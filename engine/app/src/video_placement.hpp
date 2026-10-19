// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where a video of the original plays (docs/re/videos.md, ADR 0010). Needs no platform, so that
// the tests have it.
#pragma once

#include <cstdint>

namespace o1602::app {

/// Whether a video plays in the minimap's window. Messages play there; the logos and the intro,
/// the credits (10), the pause (6, looping) and the ends of the game (defeat 998, 1010, 2020;
/// missions fulfilled from 2500) fill the screen. Without a minimap on screen (the main menu, a
/// game without the panel) a message's video fills the screen too: else its sound would play
/// without a picture.
[[nodiscard]] constexpr bool video_plays_in_minimap(std::uint16_t number, bool loop, bool intro,
                                                    bool minimap_shown) noexcept {
    const bool full =
        intro || loop || number == 10 || number == 998 || number == 1010 || number == 2020 || number >= 2500;
    return !full && minimap_shown;
}

}  // namespace o1602::app
