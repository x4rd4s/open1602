// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_icon.hpp"

#include <cstddef>
#include <string_view>

#include "annokit/core/embedded_file.hpp"

namespace o1602::app {

namespace icons {
// Compiled in by annokit_embed_files (engine/app/menus/CMakeLists.txt).
extern const annokit::core::EmbeddedFile kEmbeddedFiles[];
extern const std::size_t kEmbeddedFileCount;
}  // namespace icons

namespace {

std::span<const std::uint8_t> embedded(std::string_view name) noexcept {
    for (std::size_t i = 0; i < icons::kEmbeddedFileCount; ++i) {
        const annokit::core::EmbeddedFile& file = icons::kEmbeddedFiles[i];
        if (name == file.name) {
            return {file.data, file.size};
        }
    }
    return {};
}

}  // namespace

AppIcon app_icon() noexcept {
    return {embedded("open1602-64"), embedded("open1602-128")};
}

}  // namespace o1602::app
