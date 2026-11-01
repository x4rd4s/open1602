// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/own_figures.hpp"

#include <algorithm>
#include <charconv>

#include "o1602/assets/figure_looks.hpp"
#include "o1602/assets/original_names.hpp"
#include "o1602/core/data_file.hpp"

namespace o1602::assets {

namespace {

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

/// A name a package may give its figure: letters, digits and underscores, as the ids of the
/// original's figures have them (original_names.hpp).
bool is_name(std::string_view name) noexcept {
    return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

}  // namespace

const OwnFigure* find_own_figure(std::span<const OwnFigure> own, std::string_view id) noexcept {
    const auto it = std::find_if(own.begin(), own.end(), [&](const OwnFigure& o) { return o.id == id; });
    return it == own.end() ? nullptr : &*it;
}

std::optional<std::uint32_t> figure_number(std::span<const OwnFigure> own, const FigureTable& figures,
                                           std::string_view id) {
    if (const OwnFigure* added = find_own_figure(own, id)) {
        return added->number;
    }
    constexpr std::string_view kOriginal = "anno1602:";
    if (!id.starts_with(kOriginal)) {
        return std::nullopt;
    }
    const std::string_view after = id.substr(kOriginal.size());
    std::optional<std::uint32_t> number;
    if (const OriginalName* named = find_original_name(original_figure_names(), after)) {
        if (named->count == 1) {
            number = named->first;
        }
    } else {
        const bool hex = after.starts_with("0x");
        const std::string_view digits = hex ? after.substr(2) : after;
        std::uint32_t n = 0;
        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), n, hex ? 16 : 10);
        if (error == std::errc{} && end == digits.data() + digits.size() && !digits.empty()) {
            number = n;
        }
    }
    return number && figures.get(*number) != nullptr ? number : std::nullopt;
}

void apply_own_figures_file(FigureTable& table, std::vector<OwnFigure>& own, std::string_view package,
                            std::string_view utf8, std::vector<std::string>& warnings,
                            std::span<const PackageImage> images, std::span<const PackageSound> sounds) {
    const std::string where = std::string(package) + "/" + std::string(kOwnFiguresFile);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        warnings.push_back(where + " " + tables.error().message);
        return;
    }
    std::size_t added = 0;
    for (const core::DataTable& t : *tables) {
        const std::string at = line_of(where, t.line);
        if (!is_name(t.name)) {
            warnings.push_back(at + "[" + t.name + "]: a figure's name holds only lower case letters, digits and '_'");
            continue;
        }
        const std::string id = std::string(package) + ":" + t.name;
        if (find_own_figure(own, id) != nullptr) {
            warnings.push_back(at + id + " is declared twice; the second is passed over");
            continue;
        }
        if (added >= kMaxOwnFigures) {
            warnings.push_back(at + "a package declares at most " + std::to_string(kMaxOwnFigures) +
                               " figures; the rest are passed over");
            return;
        }
        const FigureType* like = nullptr;
        const PackageImage* image = nullptr;
        std::optional<bool> shadow;
        const PackageSound* sound = nullptr;
        const PackageSound* shot_sound = nullptr;
        const PackageSound* hit_sound = nullptr;
        bool ok = true;
        for (const core::DataEntry& e : t.entries) {
            const std::string field = line_of(where, e.value.line) + "'" + e.key + "' ";
            if (e.key == "like") {
                const std::string* s = e.value.string();
                const auto number = s != nullptr ? figure_number(own, table, *s) : std::nullopt;
                like = number ? table.get(*number) : nullptr;
                if (like == nullptr) {
                    warnings.push_back(field +
                                       "names no single figure (anno1602:<name or number>, a unit in one colour by "
                                       "its number, or <package>:<name>)");
                    ok = false;
                }
            } else if (e.key == "image") {
                const std::string* s = e.value.string();
                image = s != nullptr ? find_package_image(images, *s) : nullptr;
                if (image == nullptr) {
                    warnings.push_back(field + "names no image of a package (graphics.toml, <package>:<name>)");
                    ok = false;
                }
            } else if (e.key == "sound" || e.key == "shot_sound" || e.key == "hit_sound") {
                const std::string* s = e.value.string();
                const PackageSound*& which = e.key == "sound" ? sound : e.key == "shot_sound" ? shot_sound : hit_sound;
                which = s != nullptr ? find_package_sound(sounds, *s) : nullptr;
                if (which == nullptr) {
                    warnings.push_back(field + "names no sound of a package (sounds.toml, <package>:<name>)");
                    ok = false;
                }
            } else if (e.key == "shadow") {
                if (e.value.boolean() == nullptr) {
                    warnings.push_back(field + "is true or false");
                    ok = false;
                } else {
                    shadow = *e.value.boolean();
                }
            } else {
                warnings.push_back(field + "is no field of a figure of a package and is passed over");
            }
        }
        if (!ok) {
            continue;
        }
        if (like == nullptr) {
            warnings.push_back(at + id + " names no figure to copy ('like')");
            continue;
        }
        FigureType f = *like;
        if (image != nullptr) {
            // The image holds every sprite the figure draws, in the original's order from its Gfx
            // (figure_looks.hpp); it is drawn from the terrain sheet, where the packages' images are.
            if (const std::uint32_t needed = figure_sprite_count(f); needed > image->count) {
                warnings.push_back(at + id + ": its image " + image->id + " holds " + std::to_string(image->count) +
                                   " sprites, the figure draws " + std::to_string(needed));
                continue;
            }
            f.sheet = FigureSheet::Stadtfld;
            f.gfx = image->first;
        }
        if (shadow && !*shadow) {
            f.shadow = 0;
        }
        if (sound != nullptr) {
            // At the end of every cycle of each of its animations (AnimSmp).
            for (FigureAnim& a : f.anims) {
                a.sound = sound->number;
                a.sound_range = 1;
                a.sound_chance = 1;
            }
        }
        if (shot_sound != nullptr) {
            f.shot_sound = FigureSound{shot_sound->number, 1};
        }
        if (hit_sound != nullptr) {
            f.hit_sound = FigureSound{hit_sound->number, 1};
        }
        const auto number = table.add(f);
        if (!number) {
            warnings.push_back(at + id + ": " + number.error().message);
            continue;
        }
        own.push_back(OwnFigure{id, *number});
        ++added;
    }
}

}  // namespace o1602::assets
