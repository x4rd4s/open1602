// SPDX-License-Identifier: GPL-3.0-or-later
//
// o1602-tool: what the command files share (argument parsing, error output, the palette).

#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "o1602/assets/palette.hpp"
#include "o1602/core/result.hpp"

namespace o1602::tool {

namespace fs = std::filesystem;

/// Simple "--name value" / "--flag" option lookup.
struct Args {
    std::vector<std::string_view> positional;
    std::map<std::string_view, std::string_view> options;

    Args(int argc, char** argv) {
        for (int i = 2; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg.starts_with("--")) {
                const bool has_value = i + 1 < argc && !std::string_view(argv[i + 1]).starts_with("--");
                options[arg] = has_value ? std::string_view(argv[++i]) : std::string_view();
            } else {
                positional.push_back(arg);
            }
        }
    }

    [[nodiscard]] bool flag(std::string_view name) const { return options.contains(name); }

    [[nodiscard]] std::optional<std::size_t> number(std::string_view name) const {
        const auto it = options.find(name);
        if (it == options.end()) {
            return std::nullopt;
        }
        std::size_t value = 0;
        const auto [ptr, ec] = std::from_chars(it->second.data(), it->second.data() + it->second.size(), value);
        if (ec != std::errc{}) {
            return std::nullopt;
        }
        return value;
    }

    /// "--name A<sep>B" as two numbers, e.g. "--tile 120,80" or "--size 1920x1080".
    [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> pair(std::string_view name,
                                                                            char separator) const {
        const auto it = options.find(name);
        if (it == options.end()) {
            return std::nullopt;
        }
        const std::string_view v = it->second;
        const auto sep = v.find(separator);
        if (sep == std::string_view::npos) {
            return std::nullopt;
        }
        std::int32_t a = 0;
        std::int32_t b = 0;
        if (std::from_chars(v.data(), v.data() + sep, a).ec != std::errc{} ||
            std::from_chars(v.data() + sep + 1, v.data() + v.size(), b).ec != std::errc{}) {
            return std::nullopt;
        }
        return std::pair{a, b};
    }
};

/// Prints "error: <message>" and returns exit code 1.
int fail(const core::Error& error);

/// Prints the usage and returns exit code 2.
int usage();

/// The palette of --palette, or $ANNO1602_DIR/TOOLGFX/STADTFLD.COL.
core::Result<assets::Palette> load_palette(const Args& args);

// formats.cpp
int cmd_chunks(const Args& args);
int cmd_cod(const Args& args);
int cmd_cod_dump(const Args& args);
int cmd_bsh_info(const Args& args);
int cmd_bsh_export(const Args& args);

// savegames.cpp
int cmd_savegames(const Args& args);
int cmd_state(const Args& args);
int cmd_islands(const Args& args);
int cmd_new_world(const Args& args);
int cmd_export(const Args& args);

// render.cpp
int cmd_render_island(const Args& args);
int cmd_render_world(const Args& args);

// video.cpp
int cmd_smk(const Args& args);

}  // namespace o1602::tool
