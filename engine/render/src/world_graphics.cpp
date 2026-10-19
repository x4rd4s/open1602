// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/world_graphics.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <vector>

#include "o1602/render/flag_tint.hpp"
#include "o1602/render/unit_tint.hpp"

namespace o1602::render {

std::string sheet_path(assets::FigureSheet sheet, Zoom zoom) {
    constexpr const char* kDirs[] = {"gfx/", "mgfx/", "sgfx/"};
    return std::string(kDirs[static_cast<int>(zoom) % 3]) + assets::figure_sheet_file(sheet);
}

core::Result<WorldGraphics> load_world_graphics(const assets::GameDirectory& game, const assets::Palette& palette,
                                                Zoom zoom, std::span<const assets::FigureSheet> figure_sheets,
                                                const GraphicsOptions& options, const BannerTable* banners,
                                                const assets::FigureTable* figures) {
    struct Loaded {
        assets::FigureSheet id;
        assets::SpriteSheet sheet;
        bool derived;
    };
    std::vector<Loaded> loaded;
    const auto load = [&](assets::FigureSheet id, Zoom z) -> std::optional<assets::SpriteSheet> {
        const auto path = game.resolve(sheet_path(id, z));
        if (!path) {
            return std::nullopt;
        }
        auto sheet = assets::SpriteSheet::load(*path);
        if (!sheet) {
            return std::nullopt;
        }
        return std::move(sheet).value();
    };

    auto terrain = load(assets::FigureSheet::Stadtfld, zoom);
    if (!terrain) {
        return core::make_error(core::ErrorCode::NotFound,
                                sheet_path(assets::FigureSheet::Stadtfld, zoom) + " not found or invalid");
    }
    loaded.push_back(Loaded{assets::FigureSheet::Stadtfld, std::move(*terrain), false});
    for (const assets::FigureSheet id : figure_sheets) {
        if (id == assets::FigureSheet::Stadtfld) {
            continue;
        }
        if (auto sheet = load(id, zoom)) {
            loaded.push_back(Loaded{id, std::move(*sheet), false});
        } else if (options.derive_missing_sheets && zoom != Zoom::Large) {
            const auto larger = static_cast<Zoom>(static_cast<int>(zoom) - 1);
            if (auto big = load(id, larger)) {
                loaded.push_back(Loaded{id, std::move(*big), true});
            }
        }
    }

    // Sources refer to the loaded sheets, which must not move from here on.
    std::vector<std::unique_ptr<SpriteSource>> sources;
    std::vector<const SpriteSource*> pointers;
    for (const Loaded& l : loaded) {
        if (l.derived) {
            sources.push_back(std::make_unique<HalfSizeSource>(l.sheet, palette));
        } else {
            sources.push_back(std::make_unique<SheetSource>(l.sheet, palette));
        }
        pointers.push_back(sources.back().get());
    }
    // Tinted player flags after all sheets: one block of frames per banner with a tinted flag.
    std::vector<std::optional<std::uint32_t>> flag_slot;  // block index per banner
    std::size_t flag_source = pointers.size();
    if (banners != nullptr && banners->flag_template()) {
        const SpriteSource* ship = nullptr;
        for (std::size_t i = 0; i < loaded.size(); ++i) {
            if (loaded[i].id == assets::FigureSheet::Ship) {
                ship = sources[i].get();
            }
        }
        std::vector<assets::Rgba> colors;
        for (const Banner& b : banners->banners()) {
            if (b.flag == FlagStyle::Tinted && ship != nullptr) {
                flag_slot.emplace_back(static_cast<std::uint32_t>(colors.size()));
                colors.push_back(b.color);
            } else {
                flag_slot.emplace_back(std::nullopt);
            }
        }
        if (!colors.empty()) {
            const FlagTemplate& t = *banners->flag_template();
            sources.push_back(std::make_unique<TintedFlagSource>(*ship, t.gfx, t.frames, colors, t.from_original));
            pointers.push_back(sources.back().get());
        }
    }
    // Tinted units after that: one block per banner whose units have no drawn variant.
    std::vector<std::optional<std::uint32_t>> unit_slot;
    std::size_t unit_source = pointers.size();
    std::vector<UnitImageGroup> unit_groups;
    if (banners != nullptr && figures != nullptr) {
        const SpriteSource* soldat = nullptr;
        for (std::size_t i = 0; i < loaded.size(); ++i) {
            if (loaded[i].id == assets::FigureSheet::Soldat) {
                soldat = sources[i].get();
            }
        }
        for (const std::uint32_t family : assets::FigureTable::kUnitFamilies) {
            const assets::FigureType* first = figures->get(family);
            const assets::FigureType* second = figures->get(family + 1);
            if (first == nullptr || second == nullptr || second->gfx <= first->gfx) {
                continue;
            }
            constexpr std::int32_t kFigtypKavalerie = 2;  // figuren.cod Kind FIGTYP_KAVALERIE
            UnitImageGroup g;
            g.first = first->gfx;
            g.stride = second->gfx - first->gfx;
            g.variants = assets::FigureTable::kUnitVariants;
            g.base = assets::FigureTable::kUnitVariants - 1;  // white
            g.mounted = first->kind == kFigtypKavalerie;
            unit_groups.push_back(g);
        }
        std::vector<UnitTintColor> colors;
        for (const Banner& b : banners->banners()) {
            if (soldat != nullptr && !unit_groups.empty() && !b.unit_variant && b.flag == FlagStyle::Tinted) {
                unit_slot.emplace_back(static_cast<std::uint32_t>(colors.size()));
                colors.push_back(UnitTintColor{b.color, b.unit_mount});
            } else {
                unit_slot.emplace_back(std::nullopt);
            }
        }
        if (!colors.empty()) {
            // Player colours of the drawn variants: the original red, blue, yellow and white.
            std::vector<assets::Rgba> variant_colors(
                BannerTable::kDefaultColors.begin(),
                BannerTable::kDefaultColors.begin() + assets::FigureTable::kUnitVariants);
            auto source = std::make_unique<TintedUnitSource>(*soldat, unit_groups, variant_colors, colors);
            std::uint32_t offset = 0;
            for (UnitImageGroup& g : unit_groups) {  // same layout as in the source
                g.offset = offset;
                offset += g.stride;
            }
            sources.push_back(std::move(source));
            pointers.push_back(sources.back().get());
        }
    }
    auto atlas = SpriteAtlas::build(std::span<const SpriteSource* const>(pointers), options.page_size, options.cancel);
    if (!atlas) {
        return atlas.error();
    }
    WorldGraphics g{std::move(atlas).value(), {}, {}, {}, {}};
    for (std::size_t i = 0; i < loaded.size(); ++i) {
        const auto k = static_cast<std::size_t>(loaded[i].id);
        g.base[k] = g.atlas.sheet_base(i);
        g.derived[k] = loaded[i].derived;
    }
    if (flag_source < pointers.size()) {
        const std::uint32_t base = g.atlas.sheet_base(flag_source);
        const std::uint32_t frames = banners->flag_template()->frames;
        for (const auto& slot : flag_slot) {
            g.banner_flags.push_back(slot ? std::optional<std::uint32_t>(base + *slot * frames) : std::nullopt);
        }
    }
    if (unit_source < pointers.size()) {
        const std::uint32_t base = g.atlas.sheet_base(unit_source);
        g.unit_tint.groups = unit_groups;
        for (const UnitImageGroup& group : unit_groups) {
            g.unit_tint.block_size += group.stride;
        }
        for (const auto& slot : unit_slot) {
            g.unit_tint.banner_base.push_back(slot ? std::optional<std::uint32_t>(base + *slot * g.unit_tint.block_size)
                                                   : std::nullopt);
        }
    }
    return g;
}

core::Result<PreparedGraphics> prepare_world_graphics(const assets::GameDirectory& game, const assets::Palette& palette,
                                                      Zoom zoom, std::span<const assets::FigureSheet> figure_sheets,
                                                      const GraphicsOptions& options, const BannerTable* banners,
                                                      const assets::FigureTable* figures) {
    const auto start = std::chrono::steady_clock::now();
    auto graphics = load_world_graphics(game, palette, zoom, figure_sheets, options, banners, figures);
    if (!graphics) {
        return graphics.error();
    }
    PreparedGraphics p{std::move(graphics).value(), {}, 0};
    // Units are picked by their visible pixels: the figure sheets keep a mask (the terrain sheet
    // comes first and is not needed).
    auto first_figure = static_cast<std::uint32_t>(p.graphics.atlas.sprite_count());
    for (std::size_t k = 0; k < assets::kFigureSheetCount; ++k) {
        if (k != static_cast<std::size_t>(assets::FigureSheet::Stadtfld) && p.graphics.base[k]) {
            first_figure = std::min(first_figure, *p.graphics.base[k]);
        }
    }
    if (options.cancel != nullptr && options.cancel->load(std::memory_order_relaxed)) {
        return core::make_error(core::ErrorCode::Unsupported, "cancelled");
    }
    p.hit_mask = SpriteHitMask::build(p.graphics.atlas, first_figure);
    p.ms = static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    return p;
}

}  // namespace o1602::render
