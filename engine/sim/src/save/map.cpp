// SPDX-License-Identifier: GPL-3.0-or-later
//
// The map: MAPW (own savegame) with the world's size and both layers of every island tile, TILE
// (chunk in a .GAM) with the tiles loading the original's chunks would not give back.
#include <map>
#include <string>

#include "section_io.hpp"

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/island.hpp"

namespace o1602::sim::save {

using core::ErrorCode;
using core::make_error;

namespace {

/// A tile's picture in the file: the building's id and the part of it, 0 and 0 for none.
struct CellPicture {
    std::uint32_t id = 0;
    std::uint16_t sub = 0;
};

CellPicture picture_of(const assets::BuildingTable& buildings, const MapCell& c) {
    const assets::BuildingType* b = c.empty() ? nullptr : buildings.by_sprite(c.sprite);
    return b != nullptr ? CellPicture{b->id, static_cast<std::uint16_t>(c.sprite - b->gfx)} : CellPicture{};
}

/// A tile of TILE: the picture, then the other fields of the cell.
template <class A, class C>
void io_cell(A& a, C& c, const assets::BuildingTable& buildings) {
    CellPicture p;
    if constexpr (!A::kReading) {
        p = picture_of(buildings, c);
    }
    a.u32(p.id);
    a.u16(p.sub);
    a.u8(c.rotation);
    a.u8(c.anim_phase);
    a.u8(c.owner);
    a.u8(c.city);
    a.u8(c.random);
    if constexpr (A::kReading) {
        if (p.id != 0) {
            const assets::BuildingType* b = buildings.by_id(p.id);
            if (b == nullptr || p.sub >= b->tile_count()) {
                a.fail();  // the game's data changed, or the part lies outside the building
                return;
            }
            c.sprite = static_cast<std::uint16_t>(b->gfx + p.sub);
        }
    }
}

template <class A, class T>
void io_tile(A& a, T& t, const assets::BuildingTable& buildings) {
    a.u8(t.island);
    a.u16(t.x);
    a.u16(t.y);
    a.boolean(t.ground);
    io_cell(a, t.cell, buildings);
}

bool same_cell(const MapCell& a, const MapCell& b) noexcept {
    return a.sprite == b.sprite && a.rotation == b.rotation && a.anim_phase == b.anim_phase && a.owner == b.owner &&
           a.city == b.city && a.random == b.random;
}

/// What of an island header the game uses after loading. The rest of the header mirrors state the
/// island itself keeps (drought, craters, discovered by, ...), or fields of the original's file the
/// game does not use (`raw`); neither goes into Open1602's own savegame.
template <class A, class H>
void io_header(A& a, H& h) {
    a.string(h.chunk_name);
    a.u8(h.number);
    a.u8(h.width);
    a.u8(h.height);
    a.u8(h.flags);
    a.u16(h.pos_x);
    a.u16(h.pos_y);
    a.u16(h.template_number);
    a.u16(h.template_size);
    a.u8(h.climate);
    a.boolean(h.stored_complete);
}

/// One layer of an island in MAPW, field by field over the island (all ids, then all parts, ...),
/// which zlib packs well. Rotation and animation phase share a byte.
struct Layer {
    std::vector<std::uint32_t> ids;
    std::vector<std::uint16_t> subs;
    std::vector<std::uint8_t> rotation_anim;
    std::vector<std::uint8_t> owner;
    std::vector<std::uint8_t> city;
    std::vector<std::uint8_t> random;

    explicit Layer(std::size_t n) : ids(n), subs(n), rotation_anim(n), owner(n), city(n), random(n) {}
};

/// The columns of a layer, without a length: the island's size gives it.
template <class A, class L>
void io_layer(A& a, L& layer) {
    for (auto& v : layer.ids) {
        a.u32(v);
    }
    for (auto& v : layer.subs) {
        a.u16(v);
    }
    for (auto* column : {&layer.rotation_anim, &layer.owner, &layer.city, &layer.random}) {
        for (auto& v : *column) {
            a.u8(v);
        }
    }
}

Layer layer_of(const IslandMap& map, bool ground, const assets::BuildingTable& buildings) {
    Layer layer(std::size_t{map.width()} * map.height());
    std::size_t k = 0;
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x, ++k) {
            const MapCell& c = ground ? map.ground(x, y) : map.top(x, y);
            const CellPicture p = picture_of(buildings, c);
            layer.ids[k] = p.id;
            layer.subs[k] = p.sub;
            layer.rotation_anim[k] = static_cast<std::uint8_t>(c.rotation | c.anim_phase << 2);
            layer.owner[k] = c.owner;
            layer.city[k] = c.city;
            layer.random[k] = c.random;
        }
    }
    return layer;
}

/// Puts a layer read into the map; an error names a building the game's data does not have.
core::Status set_layer(IslandMap& map, bool ground, const Layer& layer, const assets::BuildingTable& buildings) {
    const std::uint32_t w = map.width();
    for (std::size_t k = 0; k < layer.ids.size(); ++k) {
        MapCell c;
        const std::uint32_t id = layer.ids[k];
        if (id != 0) {
            const assets::BuildingType* b = buildings.by_id(id);
            if (b == nullptr) {
                return make_error(ErrorCode::InvalidFormat,
                                  "save: building " + std::to_string(id) + " is not in the game's data");
            }
            if (layer.subs[k] >= b->tile_count()) {
                return make_error(ErrorCode::InvalidFormat, "save: part " + std::to_string(layer.subs[k]) +
                                                                " outside building " + std::to_string(id));
            }
            c.sprite = static_cast<std::uint16_t>(b->gfx + layer.subs[k]);
        }
        c.rotation = static_cast<std::uint8_t>(layer.rotation_anim[k] & 3U);
        c.anim_phase = static_cast<std::uint8_t>(layer.rotation_anim[k] >> 2 & 0x0FU);
        c.owner = layer.owner[k];
        c.city = layer.city[k];
        c.random = layer.random[k];
        const auto x = static_cast<std::uint32_t>(k % w);
        const auto y = static_cast<std::uint32_t>(k / w);
        if (ground) {
            map.set_ground_cell(x, y, c);
        } else {
            map.set_top_cell(x, y, c);
        }
    }
    return core::Ok{};
}

}  // namespace

/// The whole map: the world's size and every island with both layers of all its tiles.
void write_map(Writer& out, const World& w, const assets::BuildingTable& buildings) {
    out.u32(w.width());
    out.u32(w.height());
    out.list(w.islands(), 64, [&](const WorldIsland& i) {
        io_header(out, i.header);
        out.u32(i.x);
        out.u32(i.y);
        out.u32(i.map.width());
        out.u32(i.map.height());
        for (const bool ground : {false, true}) {
            const Layer layer = layer_of(i.map, ground, buildings);
            io_layer(out, layer);
        }
    });
}

/// A world with the size and islands of the MAPW section, nothing else yet. Not through
/// Reader::list: each island is checked and placed as it is read.
core::Result<World> read_map(Reader& in, const assets::BuildingTable& buildings) {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    in.u32(width);
    in.u32(height);
    if (!in.ok() || width == 0 || height == 0 || width > World::kMaxSide || height > World::kMaxSide) {
        return make_error(ErrorCode::InvalidFormat, "save: impossible world size");
    }
    World world(width, height);
    const std::uint32_t islands = in.in().count(64);
    for (std::uint32_t i = 0; i < islands && in.ok(); ++i) {
        assets::IslandHeader header;
        io_header(in, header);
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t w = 0;
        std::uint32_t h = 0;
        in.u32(x);
        in.u32(y);
        in.u32(w);
        in.u32(h);
        // Two layers of 10 bytes per tile must be there before the island is allocated.
        if (!in.ok() || w == 0 || h == 0 || w > width || h > height ||
            std::uint64_t{w} * h * 2 * 10 > in.in().remaining()) {
            return make_error(ErrorCode::InvalidFormat, "save: damaged island");
        }
        IslandMap map(w, h);
        for (const bool ground : {false, true}) {
            Layer layer(std::size_t{w} * h);
            io_layer(in, layer);
            if (auto status = set_layer(map, ground, layer, buildings); !status) {
                return status.error();
            }
        }
        if (!in.ok()) {
            break;
        }
        WorldIsland island{std::move(header), std::move(map), x, y};
        if (auto status = world.add_island(std::move(island)); !status) {
            return status.error();
        }
    }
    if (!in.ok()) {
        return make_error(ErrorCode::InvalidFormat, "save: damaged map");
    }
    return world;
}

/// Tiles that loading the original's chunks would not give back.
void write_tiles(Writer& out, const World& w, const assets::BuildingTable& buildings,
                 std::span<const std::uint8_t> before) {
    std::map<std::uint8_t, IslandMap> reloaded;
    if (auto file = assets::ChunkFile::from_bytes(core::Bytes(before.begin(), before.end()))) {
        if (auto islands = assets::load_islands(*file)) {
            for (const assets::Island& island : *islands) {
                reloaded.emplace(island.header.number, IslandMap::from_island(island, buildings));
            }
        }
    }
    std::vector<Decoded::TileOverride> list;
    for (const WorldIsland& i : w.islands()) {
        const auto it = reloaded.find(i.header.number);
        const IslandMap* old =
            it != reloaded.end() && it->second.width() == i.map.width() && it->second.height() == i.map.height()
                ? &it->second
                : nullptr;
        for (std::uint16_t y = 0; y < i.map.height(); ++y) {
            for (std::uint16_t x = 0; x < i.map.width(); ++x) {
                if (old == nullptr || !same_cell(old->top(x, y), i.map.top(x, y))) {
                    list.push_back({i.header.number, x, y, false, i.map.top(x, y)});
                }
                if (old == nullptr || !same_cell(old->ground(x, y), i.map.ground(x, y))) {
                    list.push_back({i.header.number, x, y, true, i.map.ground(x, y)});
                }
            }
        }
    }
    out.list(list, 17, [&](const Decoded::TileOverride& t) { io_tile(out, t, buildings); });
}

std::vector<Decoded::TileOverride> read_tiles(Reader& in, const assets::BuildingTable& buildings) {
    std::vector<Decoded::TileOverride> list;
    in.list(list, 17, [&](Decoded::TileOverride& t) { io_tile(in, t, buildings); });
    return list;
}

/// FNV-1a over what makes the game's buildings (id, picture, size) for the META section.
std::uint64_t buildings_fingerprint(const assets::BuildingTable& buildings) {
    std::uint64_t h = 0xCBF29CE484222325ULL;
    const auto mix = [&](std::uint32_t v) {
        for (int k = 0; k < 4; ++k) {
            h ^= (v >> (8 * k)) & 0xFFU;
            h *= 0x100000001B3ULL;
        }
    };
    for (const assets::BuildingType& b : buildings.types()) {
        mix(b.id);
        mix(b.gfx);
        mix(b.size_x | b.size_y << 16);
    }
    return h;
}

}  // namespace o1602::sim::save
