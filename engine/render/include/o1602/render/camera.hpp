// SPDX-License-Identifier: GPL-3.0-or-later
//
// Map camera: which part of the world is on screen. Pure logic without platform dependency, so
// the viewer and later the game share it and the tests cover it.
//
// Zoom and rotation changes keep the tile under a chosen screen point (centre or mouse cursor)
// in place, which the original only does for the screen centre.
#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include "o1602/render/world_renderer.hpp"

namespace o1602::render {

struct TilePos {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    friend bool operator==(const TilePos&, const TilePos&) = default;
};

/// World tile whose diamond contains the world pixel (px, py), or nothing outside the map.
[[nodiscard]] std::optional<TilePos> world_tile_at(const WorldView& view, double px, double py);
/// The same point in world tiles as fractions (tile (x, y) covers x..x+1, y..y+1), also outside
/// the map: for the frame of the visible part on the minimap.
[[nodiscard]] std::pair<double, double> world_point_at(const WorldView& view, double px, double py);

/// The integer pixel scale for a display scale (Window::display_scale): rounded, 1..max_scale.
[[nodiscard]] std::int32_t pixel_scale_for(float display_scale, std::int32_t max_scale) noexcept;

class Camera {
public:
    Camera() = default;

    /// Size of the world in tiles (default: the original 500 x 350).
    void set_world_size(std::uint32_t width, std::uint32_t height);

    /// Screen size in physical pixels and integer pixel scale (1 = one sprite pixel per screen pixel).
    void set_screen(std::int32_t width, std::int32_t height, std::int32_t pixel_scale);

    [[nodiscard]] const WorldView& view() const noexcept { return m_view; }
    [[nodiscard]] std::int32_t pixel_scale() const noexcept { return m_scale; }

    /// Visible part of the world in world pixels of the current zoom level.
    [[nodiscard]] Viewport viewport() const;

    /// Offset for drawing, in screen pixels and rounded so that sprites land on whole pixels:
    /// screen position = world pixel * pixel_scale - origin.
    void screen_origin(std::int32_t& x, std::int32_t& y) const;

    void center_on(TilePos tile);
    /// Scrolls by a distance in screen pixels.
    void scroll(double dx, double dy);

    /// Changes zoom level / rotation / pixel scale, keeping the tile under screen point (sx, sy).
    void set_zoom(Zoom zoom, double sx, double sy);
    void set_rotation(std::uint8_t rotation, double sx, double sy);
    void set_pixel_scale(std::int32_t scale, double sx, double sy);

    /// World tile under a screen point.
    [[nodiscard]] std::optional<TilePos> tile_at_screen(double sx, double sy) const;
    /// World point (in fractional tiles) under a screen point, also beyond the map's edge.
    [[nodiscard]] std::pair<double, double> point_at_screen(double sx, double sy) const;
    /// Screen point of a world pixel (units drawn over the map).
    void world_to_screen(double px, double py, double& sx, double& sy) const;

private:
    void screen_to_world(double sx, double sy, double& px, double& py) const;
    /// Moves the camera so that world pixel (px, py) appears at screen point (sx, sy).
    void place(double px, double py, double sx, double sy);
    /// Keeps the screen centre inside the map.
    void clamp();
    /// Applies a view change, keeping whatever is under (sx, sy) in place.
    template <class Change>
    void change_view(double sx, double sy, Change change);

    WorldView m_view{};
    double m_center_x = 0.0;  ///< world pixel at the screen centre
    double m_center_y = 0.0;
    std::int32_t m_width = 640;
    std::int32_t m_height = 480;
    std::int32_t m_scale = 1;
};

}  // namespace o1602::render
