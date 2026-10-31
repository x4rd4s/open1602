// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scenarios of content packages (docs/modding/scenarios.md, item 7 of docs/modding/open-work.md):
// `scenarios.toml` names maps a package brings, each with its names and briefing by language, its
// difficulty, the goals of the people's mission and a script of its own. The map is a scenario file
// of the original's format (.szs, .szm) or a game saved by Open1602.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "o1602/assets/game_content.hpp"
#include "o1602/assets/localized_names.hpp"

namespace o1602::assets {

/// The file of a package that names its scenarios.
inline constexpr std::string_view kScenariosFile = "scenarios.toml";

/// A city goal of a scenario (ScenarioGoals::city).
struct ScenarioCityGoal {
    std::int32_t inhabitants = 0;
    std::uint8_t resident_class = 0;
    std::uint32_t count = 0;
    friend bool operator==(const ScenarioCityGoal&, const ScenarioCityGoal&) = default;
};

/// The goals a scenario gives the people's mission; what is not given is no goal. Without any, the
/// map's own missions stay.
struct ScenarioGoals {
    std::optional<std::int32_t> inhabitants;    ///< in all cities together
    std::optional<std::int32_t> cities;         ///< so many cities
    std::optional<std::int32_t> balance;        ///< balance per minute
    std::optional<std::int32_t> trade_balance;  ///< trade balance per minute
    std::optional<std::uint16_t> defeated;      ///< so many opponents defeated
    bool castle = false;
    bool cathedral = false;
    /// Goods in stock over all own cities: the good's id ("anno1602:tools", "<package>:<name>")
    /// and tons; at most two, as the original's missions.
    std::vector<std::pair<std::string, std::uint32_t>> goods;
    /// A city with at least `inhabitants`, and `count` of them of class `resident_class` or higher.
    std::optional<ScenarioCityGoal> city;

    [[nodiscard]] bool any() const noexcept {
        return inhabitants || cities || balance || trade_balance || defeated || castle || cathedral || !goods.empty() ||
               city;
    }
    friend bool operator==(const ScenarioGoals&, const ScenarioGoals&) = default;
};

/// A scenario a package brings.
struct PackageScenario {
    std::string id;       ///< "<package>:<name>"
    std::string package;  ///< the package's id
    std::filesystem::path file;
    LocalizedNames names;      ///< Windows-1252
    LocalizedNames briefings;  ///< Windows-1252
    std::uint8_t stars = 0;    ///< difficulty 0 to 5, as the original's stars
    /// The scenario's own script (a file of the package), none if empty. It runs besides the
    /// package's script.lua, in a sandbox of its own.
    std::filesystem::path script;
    /// Money of every human player at the start; nothing keeps the map's.
    std::optional<std::int32_t> money;
    ScenarioGoals goals;

    /// The name in `language`, else in English, else the first given, else the id.
    [[nodiscard]] std::string name(std::string_view language) const;
    [[nodiscard]] std::string briefing(std::string_view language) const;
};

/// Reads `scenarios.toml` of `package` (its text `utf8`) and adds its scenarios to `scenarios`, in
/// the order of the file. A table that cannot be read, or whose map or script is not a file of the
/// package, is passed over with a warning that names the file and the line.
void read_scenarios_file(std::vector<PackageScenario>& scenarios, const ModPackage& package, std::string_view utf8,
                         std::vector<std::string>& warnings);

/// The scenarios of `packages` that have a scenarios.toml, in their load order.
[[nodiscard]] std::vector<PackageScenario> load_package_scenarios(std::span<const ModPackage> packages,
                                                                  std::vector<std::string>& warnings);

/// The scenario of id `id`, nullptr if none.
[[nodiscard]] const PackageScenario* find_scenario(std::span<const PackageScenario> scenarios, std::string_view id);

}  // namespace o1602::assets
