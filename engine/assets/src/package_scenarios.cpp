// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/package_scenarios.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <system_error>
#include <utility>

#include "o1602/assets/original_names.hpp"
#include "o1602/core/data_file.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"

namespace o1602::assets {

namespace {

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

/// A name a package may give its scenario: lower case letters, digits and '_'.
bool is_name(std::string_view name) noexcept {
    return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

/// The language of a key `<prefix>.<language>`, empty if `key` is none.
std::string_view text_key_language(std::string_view key, std::string_view prefix) noexcept {
    if (!key.starts_with(prefix) || key.size() <= prefix.size() || key[prefix.size()] != '.') {
        return {};
    }
    const std::string_view code = key.substr(prefix.size() + 1);
    const bool ok = code.size() <= 16 && std::all_of(code.begin(), code.end(),
                                                     [](const char c) { return (c >= 'a' && c <= 'z') || c == '-'; });
    return ok ? code : std::string_view{};
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// The file `relative` of `package`, if it is a safe path to a file that is there.
std::optional<std::filesystem::path> package_file(const ModPackage& package, const std::string& relative) {
    if (!safe_package_path(relative)) {
        return std::nullopt;
    }
    std::filesystem::path path = package.folder / core::path_from_utf8(relative);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return std::nullopt;
    }
    return path;
}

/// A whole number from `min` to `max`, else nothing.
std::optional<std::int64_t> number(const core::DataValue& value, std::int64_t min, std::int64_t max) {
    const std::int64_t* n = value.integer();
    if (n == nullptr || *n < min || *n > max) {
        return std::nullopt;
    }
    return *n;
}

constexpr std::int64_t kMaxInt32 = std::numeric_limits<std::int32_t>::max();
constexpr std::int64_t kMinInt32 = std::numeric_limits<std::int32_t>::min();
constexpr std::uint8_t kMaxStars = 5;
constexpr std::size_t kMaxGoods = 2;
/// Tons a goods goal may ask: the amount is kept in 1/32 t in 32 bits.
constexpr std::int64_t kMaxTons = std::numeric_limits<std::uint32_t>::max() / 32;

}  // namespace

std::string PackageScenario::name(std::string_view language) const {
    return name_in(names, language, id);
}

std::string PackageScenario::briefing(std::string_view language) const {
    return name_in(briefings, language, {});
}

void read_scenarios_file(std::vector<PackageScenario>& scenarios, const ModPackage& package, std::string_view utf8,
                         std::vector<std::string>& warnings) {
    const std::string where = package.id + "/" + std::string(kScenariosFile);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        warnings.push_back(where + " " + tables.error().message);
        return;
    }
    for (const core::DataTable& t : *tables) {
        const std::string at = line_of(where, t.line);
        if (!is_name(t.name)) {
            warnings.push_back(at + "[" + t.name +
                               "]: a scenario's name holds only lower case letters, digits and '_'");
            continue;
        }
        PackageScenario s;
        s.id = package.id + ":" + t.name;
        s.package = package.id;
        bool ok = true;
        const auto refuse = [&](const core::DataEntry& e, const std::string& what) {
            warnings.push_back(line_of(where, e.value.line) + "'" + e.key + "' " + what);
            ok = false;
        };
        for (const core::DataEntry& e : t.entries) {
            const std::string* text = e.value.string();
            if (e.key == "file") {
                const std::string ending = text != nullptr
                                               ? lower(core::path_to_utf8(core::path_from_utf8(*text).extension()))
                                               : std::string();
                const auto path = text != nullptr ? package_file(package, *text) : std::nullopt;
                if (!path || (ending != ".szs" && ending != ".szm" && ending != ".o1602")) {
                    refuse(e, "names no map of the package (a .szs, .szm or .o1602 file in its folder)");
                } else {
                    s.file = *path;
                }
            } else if (e.key == "script") {
                const auto path = text != nullptr ? package_file(package, *text) : std::nullopt;
                if (!path) {
                    refuse(e, "names no file of the package");
                } else {
                    s.script = *path;
                }
            } else if (e.key == "stars") {
                if (const auto n = number(e.value, 0, kMaxStars)) {
                    s.stars = static_cast<std::uint8_t>(*n);
                } else {
                    refuse(e, "is a number from 0 to " + std::to_string(kMaxStars));
                }
            } else if (e.key == "money") {
                if (const auto n = number(e.value, kMinInt32, kMaxInt32)) {
                    s.money = static_cast<std::int32_t>(*n);
                } else {
                    refuse(e, "is a whole number");
                }
            } else if (const std::string_view language = name_key_language(e.key); !language.empty()) {
                if (text == nullptr || text->empty()) {
                    refuse(e, "is a text");
                } else {
                    s.names.emplace_back(language, core::utf8_to_cp1252(*text));
                }
            } else if (const std::string_view b = text_key_language(e.key, "briefing"); !b.empty()) {
                if (text == nullptr) {
                    refuse(e, "is a text");
                } else {
                    s.briefings.emplace_back(b, core::utf8_to_cp1252(*text));
                }
            } else if (e.key.starts_with("goal.")) {
                ScenarioGoals& g = s.goals;
                const std::string_view goal = std::string_view(e.key).substr(5);
                const auto whole = [&](std::optional<std::int32_t>& field, std::int64_t min) {
                    if (const auto n = number(e.value, min, kMaxInt32)) {
                        field = static_cast<std::int32_t>(*n);
                    } else {
                        refuse(e, "is a whole number from " + std::to_string(min));
                    }
                };
                if (goal == "inhabitants") {
                    whole(g.inhabitants, 1);
                } else if (goal == "cities") {
                    whole(g.cities, 1);
                } else if (goal == "balance") {
                    whole(g.balance, kMinInt32);
                } else if (goal == "trade_balance") {
                    whole(g.trade_balance, kMinInt32);
                } else if (goal == "defeated") {
                    if (const auto n = number(e.value, 1, std::numeric_limits<std::uint16_t>::max())) {
                        g.defeated = static_cast<std::uint16_t>(*n);
                    } else {
                        refuse(e, "is a number from 1 to 65535");
                    }
                } else if (goal == "castle" || goal == "cathedral") {
                    if (e.value.boolean() == nullptr) {
                        refuse(e, "is true or false");
                    } else {
                        (goal == "castle" ? g.castle : g.cathedral) = *e.value.boolean();
                    }
                } else if (goal.starts_with("goods.")) {
                    // goal.goods.tools: the original's; goal.goods.pepper: the package's own;
                    // goal.goods."spice:pepper": another package's.
                    std::string good(goal.substr(6));
                    if (good.find(':') == std::string::npos) {
                        const bool original = find_original_name(original_ware_names(), good) != nullptr;
                        std::string full = original ? std::string("anno1602") : package.id;
                        full += ':';
                        full += good;
                        good = std::move(full);
                    }
                    const auto tons = number(e.value, 1, kMaxTons);
                    if (!tons) {
                        refuse(e, "is the tons, a number from 1");
                    } else if (g.goods.size() >= kMaxGoods) {
                        refuse(e, "is one goods goal too many: a mission has at most two");
                    } else {
                        g.goods.emplace_back(std::move(good), static_cast<std::uint32_t>(*tons));
                    }
                } else if (goal == "city.inhabitants" || goal == "city.class" || goal == "city.count") {
                    ScenarioCityGoal& c = g.city ? *g.city : g.city.emplace();
                    if (goal == "city.inhabitants") {
                        if (const auto n = number(e.value, 1, kMaxInt32)) {
                            c.inhabitants = static_cast<std::int32_t>(*n);
                        } else {
                            refuse(e, "is a whole number from 1");
                        }
                    } else if (goal == "city.class") {
                        if (const auto n = number(e.value, 0, 255)) {
                            c.resident_class = static_cast<std::uint8_t>(*n);
                        } else {
                            refuse(e, "is a class number from 0");
                        }
                    } else if (const auto n = number(e.value, 0, kMaxInt32)) {
                        c.count = static_cast<std::uint32_t>(*n);
                    } else {
                        refuse(e, "is a whole number from 0");
                    }
                } else {
                    refuse(e, "is no goal of a scenario and is passed over");
                }
            } else {
                refuse(e, "is no field of a scenario and is passed over");
            }
        }
        if (s.file.empty()) {
            warnings.push_back(at + s.id + " has no map ('file'); the scenario is passed over");
            continue;
        }
        if (!ok) {
            warnings.push_back(at + s.id + " is passed over");
            continue;
        }
        scenarios.push_back(std::move(s));
    }
}

std::vector<PackageScenario> load_package_scenarios(std::span<const ModPackage> packages,
                                                    std::vector<std::string>& warnings) {
    std::vector<PackageScenario> scenarios;
    for (const ModPackage& p : packages) {
        const std::filesystem::path path = p.folder / std::filesystem::path(kScenariosFile);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) {
            continue;
        }
        const auto bytes = core::read_file(path);
        if (!bytes) {
            warnings.push_back(p.id + "/" + std::string(kScenariosFile) + ": " + bytes.error().message);
            continue;
        }
        read_scenarios_file(
            scenarios, p,
            core::skip_utf8_bom(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size())),
            warnings);
    }
    return scenarios;
}

const PackageScenario* find_scenario(std::span<const PackageScenario> scenarios, std::string_view id) {
    const auto it =
        std::find_if(scenarios.begin(), scenarios.end(), [&](const PackageScenario& s) { return s.id == id; });
    return it != scenarios.end() ? &*it : nullptr;
}

}  // namespace o1602::assets
