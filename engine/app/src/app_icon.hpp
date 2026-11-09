// SPDX-License-Identifier: GPL-3.0-or-later
//
// The program's icon for the title bar, the task bar and the macOS dock (data/icons/window,
// rendered by tools/make_icons.py), compiled into the program.
#pragma once

#include <cstdint>
#include <span>

namespace o1602::app {

struct AppIcon {
    std::span<const std::uint8_t> bmp;        ///< 64 px, BMP
    std::span<const std::uint8_t> bmp_large;  ///< 128 px for HiDPI screens, BMP
};

/// The icon for annokit::platform::WindowConfig.
[[nodiscard]] AppIcon app_icon() noexcept;

}  // namespace o1602::app
