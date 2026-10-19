// SPDX-License-Identifier: GPL-3.0-or-later
//
// Draw list for a viewport of the world map (islands + open sea), same rules as the island
// renderer (docs/re/rendering.md). Coordinates are "world pixels" of the selected zoom level:
//   left edge of tile (vx, vy) = (vx - vy + view_height - 1) * tile_width / 2
//   bottom of tile row d = vx + vy = d * tile_height / 2 + tile_height - 1
// where (vx, vy) are world coordinates after view rotation.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/render/animation_clock.hpp"
#include "o1602/render/banner.hpp"
#include "o1602/render/island_renderer.hpp"
#include "o1602/render/ship_trail.hpp"
#include "o1602/render/sprite_atlas.hpp"
#include "o1602/render/unit_tint.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::render {

struct Viewport {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
};

struct WorldView {
    Zoom zoom = Zoom::Large;
    std::uint8_t rotation = 0;  ///< 0..3
    /// Size of the world in tiles (a property of the map, ADR 0007). Functions that receive the
    /// world as well take its size from there.
    std::uint32_t world_width = sim::World::kOriginalWidth;
    std::uint32_t world_height = sim::World::kOriginalHeight;
};

/// `view` with the size of `world`.
[[nodiscard]] inline WorldView fit_view(WorldView view, const sim::World& world) noexcept {
    view.world_width = world.width();
    view.world_height = world.height();
    return view;
}

/// Size of the whole world in world pixels (without the overhang of tall sprites above row 0).
void world_pixel_size(const WorldView& view, std::int32_t& width, std::int32_t& height);

/// World pixel position (centre of the tile diamond) of a world tile, for scrolling to it.
void world_tile_center(const WorldView& view, std::uint32_t x, std::uint32_t y, std::int32_t& px, std::int32_t& py);

/// Sprite of an open sea tile: ripple pattern (frame + 3x + y) mod AnimAnz (0x00489C90).
[[nodiscard]] std::uint32_t sea_sprite(const assets::BuildingType& sea, std::uint32_t frame, std::uint32_t x,
                                       std::uint32_t y);

/// Largest sprite of the sheet; tiles outside the viewport whose sprites may still reach into it
/// (tall buildings in rows below, wide sprites in neighbouring columns) are included.
struct SpriteExtent {
    std::int32_t width = 64;
    std::int32_t height = 64;
};

/// All sprites that may intersect the viewport, in painter's order: rows back to front, per row
/// first the flat pass (open sea, ground, normal buildings), then the Highflg pass.
[[nodiscard]] std::vector<DrawCommand> build_world_draw_list(const sim::World& world,
                                                             const assets::BuildingTable& buildings,
                                                             const AnimationClock& clock, const WorldView& view,
                                                             const Viewport& viewport, const SpriteExtent& extent);

/// Figures the original spawns on buildings every frame (Buildings_SpawnDisplayFigures, 0x00486B60
/// -> 0x004466A0). Buildings do not move, so Open1602 collects them once per world; what depends on
/// the building's state (smoke, status icons) is decided when drawing.
enum class BuildingFigureRole : std::uint8_t {
    Flag,        ///< warehouse, market, tower (kind 0x1D): flag in the colour of the owner's banner
    Smoke,       ///< chimney of a workshop (kind 0x17): rises while the building works, translucent
    Status,      ///< icon above a production building of the local player (kind 0x16, NOAKTIV):
                 ///< switched off, short of raw material or on fire
    Disaster,    ///< plague or fire on a house, for everybody (kind 0x1A, SENSENMANN)
    MarketFire,  ///< a burning market of the local player (kind 0x18, BRANDMARKT)
    Deposit,     ///< ore (SHOWEISEN) or gold (SHOWGOLD) on a mountain, once the local player explored
                 ///< the deposit (kind 0x1B, 0x00487076 and 0x0045205B)
};

struct BuildingFigure {
    BuildingFigureRole role = BuildingFigureRole::Flag;
    double x = 0.0;  ///< world position in tiles (continuous): building centre + rotated Fahnoffs
    double y = 0.0;
    double z = 0.0;                        ///< height: Fahnoffs[2] in half tile widths
    std::uint16_t figure = 0;              ///< FIGUR number (FAHNEMARKT, RAUCHBAECK, NOAKTIV, ...)
    sim::PlayerId owner = sim::kNoPlayer;  ///< owner of the city the building belongs to
    std::uint8_t phase = 0;                ///< animation offset so neighbouring figures do not move in lockstep
    std::uint16_t island = 0;              ///< the building (index into World::islands(), origin tile) for its state
    std::uint16_t origin_x = 0;
    std::uint16_t origin_y = 0;
};

/// Flags (HAUS_PRODTYP Kind KONTOR, MARKT, WACHTURM), smoke (workshops with Rauchfignr) and status
/// icons (all production buildings, kinds 1..6) of buildings inside a city.
[[nodiscard]] std::vector<BuildingFigure> collect_building_figures(const sim::World& world,
                                                                   const assets::BuildingTable& buildings,
                                                                   const assets::FigureTable& figures);
/// The same for one island, appended to `out` (to bring a list up to date after building there).
void collect_island_building_figures(const sim::World& world, const assets::BuildingTable& buildings,
                                     const assets::FigureTable& figures, std::uint16_t island,
                                     std::vector<BuildingFigure>& out);

/// A figure standing or walking on land (animals, soldiers; carriers later) as the simulation or
/// the viewer hands it to the renderer.
/// FigureInstance::crop of a figure that cannot be seen at all.
inline constexpr std::uint16_t kCropHidden = 256;

struct FigureInstance {
    double x = 0.0;  ///< world position in tiles (continuous; tile centre = x + 0.5)
    double y = 0.0;
    std::uint16_t figure = 0;  ///< FIGUR number
    std::uint8_t anim = 0;     ///< ANIM index and frame (AnimState)
    std::uint8_t frame = 0;
    std::uint8_t direction = 0;  ///< 0..7
    /// Height in half tile widths, drawn that much higher (0x00451CA4). The original gives every
    /// walking land figure the height of the tile it was made on, 0.028 * Posoffs (0x00446CA0),
    /// so 0.56 on land, and keeps it while it walks (render::spawn_height).
    double z = 0.0;
    /// Share of the body hidden from below in 1/256 of its height, kCropHidden or more: not drawn
    /// at all. Workers and animals in crops and young trees (render::crop_cut).
    std::uint16_t crop = 0;
    /// Owner: units drawn per colour (FigureTable::unit_variant) take the colour of the owner's
    /// banner - the drawn variant or the tinted template.
    sim::PlayerId owner = sim::kNoPlayer;
    /// Sheet of the images. The original picks it by the figure's draw kind (soldiers: SOLDAT.BSH),
    /// not by Blocknr, which many figures lack; nothing = Blocknr.
    std::optional<assets::FigureSheet> sheet;
};

/// A figure of an effect hanging or flying over the world: the fire over an erupting volcano
/// (VULKAN, kind 0x20) and lumps of lava (VULKANFIRE1, kind 0x21). Drawn from EFFEKTE.BSH at a
/// height over its point, with its shadow on the ground while it is low (0x0044FA90).
struct EffectFigure {
    double x = 0.0;  ///< world position in tiles (continuous)
    double y = 0.0;
    double z = 0.0;            ///< height in half tile widths
    double ground = 0.0;       ///< height of the ground below, in half tile widths
    std::uint16_t figure = 0;  ///< FIGUR number
    std::uint8_t anim = 0;     ///< ANIM index
    std::uint32_t phase = 0;   ///< animation offset in ms
    /// Sheet of the images: EFFEKTE.BSH for the volcano, FISCHE.BSH for sea creatures, GAUKLER.BSH
    /// for jugglers.
    assets::FigureSheet sheet = assets::FigureSheet::Effekte;
    std::uint8_t direction = 0;         ///< 0..7
    std::optional<std::uint8_t> frame;  ///< a frame of its own instead of one from the time
};

/// Where a figure attached to a building stands (0x004466A0): the building centre plus the
/// figure's Fahnoffs turned with the building, in world tiles.
void building_attach_point(const sim::WorldIsland& island, const assets::BuildingType& b, std::uint32_t x,
                           std::uint32_t y, std::uint8_t rotation, const assets::FigureType& fig, double& cx,
                           double& cy);

/// Opacity of figure shadows (the original dithers every second pixel).
inline constexpr std::uint8_t kShadowAlpha = 128;
/// Opacity of chimney smoke (the original draws every second pixel, 0x004048E0).
inline constexpr std::uint8_t kSmokeAlpha = 128;

/// A tile marked with a translucent diamond, e.g. the red ground under a building that cannot be
/// built there, on top of everything; or hatched on the ground, under the buildings: the area
/// dragged out for fields or trees.
struct TileMark {
    std::uint16_t island = 0;  ///< index into World::islands()
    std::uint16_t x = 0;       ///< island tile
    std::uint16_t y = 0;
    assets::Rgba color;  ///< alpha = opacity
    bool hatched = false;
};

/// Colour of the hatching over an area dragged out for fields or trees (History Edition).
inline constexpr assets::Rgba kDragHatch{202, 172, 52, 255};

/// Brightness of a dimmed tile (of 255).
inline constexpr std::uint8_t kDimBrightness = 120;

/// Colour of the dashed border of a catchment circle.
inline constexpr assets::Rgba kCircleOutline{255, 230, 0, 255};

/// Tiles drawn darker, as the original does while the player builds (tile bit 27, drawn through
/// a shading table with colour 0xD7): only the land the player can build on stays lit. A clicked
/// building with a catchment narrows that to the land inside its circle, a clicked Kontor adds
/// the water its ships trade from. A building being placed shows its circle as a dashed border
/// instead, drawn whole, over land and water alike.
class TileLighting {
public:
    /// Nothing dimmed.
    TileLighting() = default;
    /// Only the land of `player` stays lit.
    [[nodiscard]] static TileLighting territory(sim::PlayerId player);
    /// Only the land of `player` inside the circle `area` (world tiles) stays lit.
    [[nodiscard]] static TileLighting circle(sim::PlayerId player, const sim::Catchment& area);

    /// Adds the border of the circle `area` (world tiles) without narrowing the lit land.
    TileLighting& outline(const sim::Catchment& area);
    /// Keeps the water lit that ships reach the building at tile (x, y) of `island` from.
    TileLighting& harbour(const sim::World& world, const assets::BuildingTable& buildings, std::uint16_t island,
                          std::uint32_t x, std::uint32_t y);

    [[nodiscard]] bool active() const noexcept { return m_kind != Kind::None; }
    /// Whether the circle gets a dashed border.
    [[nodiscard]] bool outlined() const noexcept { return m_outlined; }
    /// Whether world tile (x, y) lies in the circle.
    [[nodiscard]] bool in_circle(std::int64_t x, std::int64_t y) const noexcept;
    /// Whether world tile (x, y) is dimmed; `cell` is its top island cell, nullptr for water.
    [[nodiscard]] bool dimmed(std::int64_t x, std::int64_t y, const sim::MapCell* cell) const noexcept;

private:
    enum class Kind : std::uint8_t { None, Territory, Circle };
    void set_circle(const sim::Catchment& area);

    Kind m_kind = Kind::None;
    sim::PlayerId m_player = sim::kNoPlayer;
    sim::Catchment m_area;
    std::vector<std::uint8_t> m_half;  ///< half widths of the circle's rows, empty without a circle
    bool m_outlined = false;
    // The building whose harbour water stays lit (world == nullptr: none).
    const sim::World* m_world = nullptr;
    const assets::BuildingTable* m_buildings = nullptr;
    std::uint16_t m_island = 0;
    std::uint32_t m_x = 0;
    std::uint32_t m_y = 0;
};

/// Everything besides the tiles that goes into the world draw list.
/// Animation phases of the production buildings as the presentation runs them (0x004638C0). They
/// follow the real-time animation clock, so they live here and not in sim::Production, whose phase
/// is part of the game state and must not depend on the frame rate (ADR 0004). Indexed like
/// World::productions(); a building without an entry shows its saved phase.
class ProductionPhases {
public:
    [[nodiscard]] std::optional<std::uint8_t> get(std::size_t production) const noexcept {
        if (production >= m_phase.size() || m_phase[production] == kNone) {
            return std::nullopt;
        }
        return m_phase[production];
    }
    void set(std::size_t production, std::uint8_t phase) {
        if (production >= m_phase.size()) {
            m_phase.resize(production + 1, kNone);
        }
        m_phase[production] = static_cast<std::uint8_t>(phase & 0x0FU);
    }
    /// The entry was freed or taken by another building: back to its saved phase.
    void forget(std::size_t production) noexcept {
        if (production < m_phase.size()) {
            m_phase[production] = kNone;
        }
    }
    void clear() noexcept { m_phase.clear(); }

private:
    static constexpr std::uint8_t kNone = 0xFF;
    std::vector<std::uint8_t> m_phase;
};

struct WorldLayers {
    SpriteExtent extent;
    /// Ships with their player flags. Needs the figure table and an atlas containing SHIP.BSH of the
    /// current zoom level at `ship_base` (sizes are taken from it, figures are centred).
    const assets::FigureTable* figures = nullptr;
    const SpriteAtlas* atlas = nullptr;
    std::uint32_t ship_base = 0;
    /// Ships between two ticks (ShipTrail); without it they are drawn where they are.
    const ShipTrail* ship_trail = nullptr;
    /// Colours and flags of the players' banners. Without it there are no flags and no ownership view.
    const BannerTable* banners = nullptr;
    /// Atlas index of each banner's tinted flag (WorldGraphics::banner_flags).
    const std::vector<std::optional<std::uint32_t>>* banner_flags = nullptr;
    /// Ownership view: tiles owned by a player get a diamond in the colour of the owner's banner.
    bool owner_tint = false;
    /// Single marked tiles, drawn on top of everything.
    const std::vector<TileMark>* tile_marks = nullptr;
    /// Tiles drawn darker (while building, or around a clicked building).
    const TileLighting* lighting = nullptr;
    /// Flags, smoke and status icons of buildings (collect_building_figures). Smoke and icons need
    /// the atlas bases of their sheets (`sheet_base`) and follow the buildings' production state.
    const std::vector<BuildingFigure>* building_figures = nullptr;
    /// Player whose buildings show status icons (the original: the local player). kNoPlayer: none.
    sim::PlayerId local_player = sim::kNoPlayer;
    /// Sets of figures on land (animals, soldiers, ...) with their shadows. Needs `figures` and the
    /// atlas bases of their sheets (`sheet_base`, e.g. from WorldGraphics::base); figures whose sheet
    /// is missing are skipped.
    std::vector<const std::vector<FigureInstance>*> land_figures;
    std::array<std::optional<std::uint32_t>, assets::kFigureSheetCount> sheet_base{};
    /// Tinted units for banners without drawn variants (WorldGraphics::unit_tint).
    const UnitTint* unit_tint = nullptr;
    /// Volcano fire and lava. Needs `figures` and the atlas bases of EFFEKTE.BSH and SCHATTEN.BSH.
    const std::vector<EffectFigure>* effects = nullptr;
    /// Animation phases of the production buildings; without them the saved phases are shown.
    const ProductionPhases* production_phases = nullptr;
};

/// Up to two draw commands of one figure (shadow + body, hull + flag) and the tile row after
/// whose flat pass they are drawn.
struct FigureSprites {
    std::int32_t row = 0;
    std::int32_t depth = 0;  ///< Y of the original (0x00451CC5): order within the row, smaller first
    std::array<DrawCommand, 4> parts{};
    std::uint8_t count = 0;
};

/// Working memory of build_world_draw_list. A caller that draws every frame keeps one, so that a
/// frame allocates nothing once its containers have grown.
struct DrawListScratch {
    std::vector<FigureSprites> figures;
    std::vector<DrawCommand> overlays;
    std::vector<std::pair<std::uint64_t, TileMark>> marks;  ///< by island and tile
    std::vector<DrawCommand> marked;
    std::vector<DrawCommand> outline;
};

/// Shadow and body of a land figure (exposed for tests). The original keeps every figure at
/// Y = y + Posoffs[1] * w/64 (0x00451CC5), which is also its depth; the height z lifts body and
/// shadow by trunc(z * w/2) without changing Y. Body: Gfx + animation (0x0044F8F0), centred, bottom
/// at Y + Posoffs[0] * w/64 - lift; shadow: figure `Schatten` from SCHATTEN.BSH, centred on
/// y - lift (cases 8-0x12 of 0x0044FA90).
[[nodiscard]] bool land_figure_sprites(const FigureInstance& figure, const sim::World& world, const WorldView& view,
                                       const WorldLayers& layers, FigureSprites& out);

/// Body and shadow of an effect figure at time `time_ms` (exposed for tests). The shadow shows
/// while the figure is less than 5 half tile widths above the ground. The original centres the
/// lava with the size of the carriers' sheet (U-008); Open1602 uses its own image.
[[nodiscard]] bool effect_figure_sprites(const EffectFigure& figure, const sim::World& world, const WorldView& view,
                                         const WorldLayers& layers, std::uint64_t time_ms, FigureSprites& out);

/// Draw list including ships (drawn after the flat pass of their tile row, like the figures in
/// the original, 0x0044FA90) and the ownership tint.
void build_world_draw_list(const sim::World& world, const assets::BuildingTable& buildings, const AnimationClock& clock,
                           const WorldView& view, const Viewport& viewport, const WorldLayers& layers,
                           std::vector<DrawCommand>& out);
/// Same, with working memory kept by the caller.
void build_world_draw_list(const sim::World& world, const assets::BuildingTable& buildings, const AnimationClock& clock,
                           const WorldView& view, const Viewport& viewport, const WorldLayers& layers,
                           std::vector<DrawCommand>& out, DrawListScratch& scratch);

/// Sprite, position and flag of one ship (exposed for tests).
struct ShipSprites {
    std::int32_t row = 0;  ///< tile row (view space) after whose flat pass the ship is drawn
    std::int32_t depth = 0;
    DrawCommand wave;  ///< bow wave (BUGH) while sailing, under the hull
    DrawCommand hull;  ///< the damaged hull below 50 % health
    DrawCommand flag;  ///< the owner's flag, or the white flag of a surrendered ship
    DrawCommand sink;  ///< UNTERGANG: the splash of a sinking ship
    bool has_wave = false;
    bool has_flag = false;
    bool has_sink = false;
};
[[nodiscard]] bool ship_sprites(const sim::Ship& ship, const sim::World& world, const WorldView& view,
                                const WorldLayers& layers, std::uint64_t time_ms, ShipSprites& out);

/// Sprites and row of a building figure at time `time_ms` (exposed for tests). False if it is not
/// shown: no graphics, no banner flag, smoke of an idle building, status OK, not the local player.
[[nodiscard]] bool building_figure_sprites(const BuildingFigure& figure, const sim::World& world,
                                           const assets::BuildingTable& buildings, const WorldView& view,
                                           const WorldLayers& layers, std::uint64_t time_ms, FigureSprites& out);

/// Same, but fills `out` (cleared first) so a per-frame caller can reuse its allocation.
void build_world_draw_list(const sim::World& world, const assets::BuildingTable& buildings, const AnimationClock& clock,
                           const WorldView& view, const Viewport& viewport, const SpriteExtent& extent,
                           std::vector<DrawCommand>& out);

}  // namespace o1602::render
