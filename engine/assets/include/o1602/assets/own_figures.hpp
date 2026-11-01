// SPDX-License-Identifier: GPL-3.0-or-later
//
// Figures of a package's own, Open1602's own: `own_figures.toml` of a package declares figures that
// the original's `figuren.cod` does not have (docs/modding/own-figures.md). Each one copies a figure
// of the original (`like`), may draw an image of a package, and gets a number of its own in the
// figure table. Its string id `<package>:<name>` is what packages and savegames name it by, so that
// the numbers may differ between installations.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/package_graphics.hpp"
#include "o1602/assets/package_sounds.hpp"

namespace o1602::assets {

/// The file of a package that declares its figures.
inline constexpr std::string_view kOwnFiguresFile = "own_figures.toml";
/// Figures one package may declare, so that a broken file cannot fill the table.
inline constexpr std::size_t kMaxOwnFigures = 64;

/// A figure a package added, with the number the figure table gave it in this run.
struct OwnFigure {
    std::string id;            ///< "<package>:<name>", UTF-8
    std::uint32_t number = 0;  ///< its `FIGUR` number in this run's figure table

    friend bool operator==(const OwnFigure&, const OwnFigure&) = default;
};

/// The entry of `own` called `id`, nullptr if there is none.
[[nodiscard]] const OwnFigure* find_own_figure(std::span<const OwnFigure> own, std::string_view id) noexcept;

/// The one figure `id` names: "anno1602:<name or number>" of the original (a unit's name stands for
/// four figures and names none here) or "<package>:<name>" of `own`. Nothing if it names none.
[[nodiscard]] std::optional<std::uint32_t> figure_number(std::span<const OwnFigure> own, const FigureTable& figures,
                                                         std::string_view id);

/// Reads `own_figures.toml` of `package` and adds its figures to `table` and `own`. Packages are
/// read in their load order, so the numbers are the same on every computer with the same packages.
/// A table that cannot be used is passed over with a warning; a file that is no data file adds
/// nothing. `images`: what `image` may name (package_graphics.hpp); `sounds`: what `sound` may name
/// (package_sounds.hpp).
void apply_own_figures_file(FigureTable& table, std::vector<OwnFigure>& own, std::string_view package,
                            std::string_view utf8, std::vector<std::string>& warnings,
                            std::span<const PackageImage> images = {}, std::span<const PackageSound> sounds = {});

}  // namespace o1602::assets
