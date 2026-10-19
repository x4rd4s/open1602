// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/land_figures.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace o1602::render {

namespace {
constexpr std::int32_t kLandPosoffs = 20;  // HIGHBODEN
constexpr std::int32_t kBeachRuin = 29;    // HAUS Kind STRANDRUINE
constexpr std::int32_t kPier = 30;
}  // namespace

double spawn_height(const sim::World& world, const assets::BuildingTable& buildings, double x, double y,
                    bool piers_as_land) {
    if (x < 0.0 || y < 0.0) {
        return 0.0;
    }
    const auto wx = static_cast<std::uint32_t>(x);
    const auto wy = static_cast<std::uint32_t>(y);
    if (!world.contains(wx, wy)) {
        return 0.0;
    }
    const sim::World::Cell& cell = world.cell(wx, wy);
    if (cell.island == sim::World::kSea || cell.island >= world.islands().size()) {
        return 0.0;
    }
    const sim::WorldIsland& island = world.islands()[cell.island];
    const std::uint32_t lx = wx - island.x;
    const std::uint32_t ly = wy - island.y;
    if (!island.map.contains(static_cast<std::int32_t>(lx), static_cast<std::int32_t>(ly))) {
        return 0.0;
    }
    const sim::MapCell& c = island.map.top(lx, ly);
    const assets::BuildingType* b = c.empty() ? nullptr : buildings.by_sprite(c.sprite);
    if (b == nullptr) {
        return 0.0;
    }
    if (piers_as_land && (b->kind == kBeachRuin || b->kind == kPier)) {
        return kLandPosoffs * kHeightPerPosoffs;
    }
    return b->posoffs * kHeightPerPosoffs;
}

namespace {

// Wares of the crops (Text.cod order).
constexpr std::uint8_t kGrain = 45;
constexpr std::uint8_t kTobacco = 46;
constexpr std::uint8_t kSpices = 47;
constexpr std::uint8_t kSugar = 48;
constexpr std::uint8_t kCotton = 49;
constexpr std::uint8_t kGrapes = 50;
constexpr std::uint8_t kCocoa = 51;
constexpr std::uint8_t kTree = 53;

/// Share hidden by a growing crop of `ware` at growth stage 0, 1, 2, 3, 4 and later (tables at
/// 0x004514FC, 0x00451520-0x00451550).
std::uint16_t growing_cut(std::uint8_t ware, std::uint8_t stage) noexcept {
    struct Row {
        std::uint8_t ware;
        std::array<std::uint16_t, 5> by_stage;
    };
    static constexpr std::array<Row, 8> kRows = {{
        {kGrain, {0, 10, 10, 45, 55}},
        {kTobacco, {0, 0, 48, 48, 57}},
        {kSpices, {0, 0, 48, 48, 57}},
        {kSugar, {0, 0, 40, 65, 80}},
        {kCotton, {55, 55, 55, 55, 55}},
        {kGrapes, {70, 70, 70, 70, 70}},
        {kCocoa, {145, 145, 145, 145, 145}},
        {kTree, {0, 0, 40, 65, kCropHidden}},
    }};
    for (const Row& r : kRows) {
        if (r.ware == ware && r.ware != 0) {
            return r.by_stage[std::min<std::size_t>(stage, 4)];
        }
    }
    return 0;  // meadows and anything else
}

/// Share hidden by a ripe crop of `ware` (0x00451560).
std::uint16_t ripe_cut(std::uint8_t ware) noexcept {
    switch (ware) {
        case kGrain:
        case kCotton:
            return 55;
        case kTobacco:
        case kSpices:
            return 57;
        case kSugar:
            return 80;
        case kGrapes:
            return 72;
        case kCocoa:
            return 145;
        case kTree:
            return kCropHidden;
        default:
            return 0;
    }
}

}  // namespace

std::uint16_t crop_cut(const sim::World& world, const assets::BuildingTable& buildings, double x, double y) {
    if (x < 0.0 || y < 0.0) {
        return 0;
    }
    const auto wx = static_cast<std::uint32_t>(x);
    const auto wy = static_cast<std::uint32_t>(y);
    if (!world.contains(wx, wy)) {
        return 0;
    }
    const sim::World::Cell& wc = world.cell(wx, wy);
    if (wc.island == sim::World::kSea || wc.island >= world.islands().size()) {
        return 0;
    }
    const sim::MapCell& cell = world.islands()[wc.island].map.top(wc.local_x, wc.local_y);
    const assets::BuildingType* b = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
    if (b == nullptr) {
        return 0;
    }
    switch (b->kind) {
        case 1:   // road
        case 11:  // ground
        case 12:  // ruin
        case 13:  // square
        case 17:  // river corner
        case 18:  // bridge
        case 23:  // beach
        case 29:  // beach ruin
        case 30:  // pier
            return 0;
        default:
            break;
    }
    constexpr std::int32_t kRipe = 9;
    constexpr std::int32_t kGrowing = 10;
    constexpr std::int32_t kQuarry = 11;
    if (b->prod_kind == kGrowing) {
        // The crop is the ware of the ripe record that follows the growing one (HAUS + 0xA9).
        const assets::BuildingType* ripe = buildings.by_record(b->record + 1);
        return growing_cut(ripe != nullptr ? ripe->ware : 0, cell.anim_phase);
    }
    if (b->prod_kind == kRipe) {
        return ripe_cut(b->ware);
    }
    return b->prod_kind == kQuarry ? 0 : kCropHidden;
}

void LandFigures::clear() {
    m_instances.clear();
    m_states.clear();
}

void LandFigures::add(const FigureInstance& instance, const assets::FigureTable& figures, std::uint8_t anim,
                      std::uint32_t seed) {
    FigureInstance f = instance;
    AnimState state;
    if (const assets::FigureType* type = figures.get(f.figure)) {
        state = start_animation(*type, type->anim(anim) != nullptr ? anim : 0, seed);
    }
    f.anim = state.anim;
    f.frame = state.frame;
    m_instances.push_back(f);
    m_states.push_back(state);
}

void LandFigures::advance(std::uint32_t dt_ms, const assets::FigureTable& figures) {
    for (std::size_t i = 0; i < m_instances.size(); ++i) {
        if (const assets::FigureType* type = figures.get(m_instances[i].figure)) {
            advance_animation(m_states[i], *type, dt_ms);
            m_instances[i].anim = m_states[i].anim;
            m_instances[i].frame = m_states[i].frame;
        }
    }
}

void AnimalFigures::reset(const sim::World& world, const assets::BuildingTable& buildings,
                          const assets::FigureTable& figures) {
    clear();
    for (const sim::Animal& a : world.animals()) {
        const sim::WorldIsland& island = world.islands()[a.island];
        FigureInstance f;
        f.x = island.x + a.x + 0.5;
        f.y = island.y + a.y + 0.5;
        f.figure = a.figure;
        f.z = spawn_height(world, buildings, f.x, f.y);
        const std::uint32_t seed = (island.x + a.x) * 73856093U ^ (island.y + a.y) * 19349663U;
        f.direction = static_cast<std::uint8_t>((seed >> 3) & 7U);
        add(f, figures, kGrazeAnim, seed);
    }
}

void SoldierFigures::reset(const sim::World& world, const assets::BuildingTable& buildings,
                           const assets::FigureTable& figures) {
    clear();
    m_sim_anims.clear();
    m_index.clear();
    sync(world, buildings, figures);
}

void SoldierFigures::sync(const sim::World& world, const assets::BuildingTable& buildings,
                          const assets::FigureTable& figures) {
    // The last frame's animations, as they ran since.
    std::swap(m_states, m_last_states);
    std::swap(m_sim_anims, m_last_sim_anims);
    std::swap(m_index, m_last_index);
    m_instances.clear();
    m_states.clear();
    m_sim_anims.clear();
    m_index.clear();
    for (const sim::Soldier& s : world.soldiers()) {
        if (s.motion.phase == sim::SoldierMotion::Phase::Healing) {
            continue;
        }
        FigureInstance f;
        f.x = s.x();
        f.y = s.y();
        f.figure = s.figure;
        f.direction = s.direction;
        f.owner = s.owner;
        f.z = spawn_height(world, buildings, f.x, f.y, true);  // 0x0045E1F0 with 0x00451180
        f.sheet = assets::FigureSheet::Soldat;                 // draw kind 4 (0x0044FA90)
        AnimState state;
        std::uint8_t sim_anim = 0;
        const std::size_t last = lookup(m_last_index, s.id);
        const assets::FigureType* type = figures.get(s.figure);
        if (last != kNone && m_last_sim_anims[last] == s.anim) {
            state = m_last_states[last];
            sim_anim = s.anim;
        } else if (type != nullptr) {
            const std::uint8_t anim = type->anim(s.anim) != nullptr ? s.anim : 0;
            // A unit that was already there starts its animation from the first frame; one that
            // appears (loaded, released) somewhere in it, so that neighbours do not move in step.
            state = start_animation(*type, anim, s.id);
            if (last != kNone) {
                state.frame = 0;
                state.elapsed_ms = 0;
            }
            sim_anim = s.anim;
        }
        f.anim = state.anim;
        f.frame = state.frame;
        m_index.push_back(Entry{s.id, static_cast<std::uint32_t>(m_instances.size())});
        m_instances.push_back(f);
        m_states.push_back(state);
        m_sim_anims.push_back(sim_anim);
    }
    // The soldiers come in the order of their ids, so this hardly moves anything.
    std::stable_sort(m_index.begin(), m_index.end(), [](const Entry& a, const Entry& b) { return a.id < b.id; });
}

std::size_t SoldierFigures::lookup(const std::vector<Entry>& index, std::uint32_t id) noexcept {
    const auto it =
        std::lower_bound(index.begin(), index.end(), id, [](const Entry& e, std::uint32_t v) { return e.id < v; });
    return it != index.end() && it->id == id ? it->index : kNone;
}

const FigureInstance* SoldierFigures::find(std::uint32_t id) const noexcept {
    const std::size_t i = lookup(m_index, id);
    return i != kNone ? &m_instances[i] : nullptr;
}

}  // namespace o1602::render
