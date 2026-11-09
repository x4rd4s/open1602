// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's objects (ADR 0013): the page "Objects" places the build menu's buildings for a
// player without costs, ships and land units, and takes them away, each click or drag one step of
// undo.

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "catchment_view.hpp"
#include "map_input.hpp"
#include "map_viewer.hpp"
#include "names.hpp"
#include "templates.hpp"

#include "o1602/assets/game_data.hpp"
#include "o1602/assets/own_figures.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/editor/cities.hpp"
#include "o1602/editor/objects.hpp"
#include "o1602/editor/operations.hpp"
#include "o1602/editor/players.hpp"
#include "o1602/editor/resources.hpp"
#include "o1602/editor/special_objects.hpp"
#include "o1602/editor/terrain.hpp"
#include "o1602/editor/units.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/growth.hpp"
#include "o1602/sim/logistics.hpp"
#include "o1602/sim/ships.hpp"
#include "o1602/ui/build_menu.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

/// The button number of the natives' and pirates' group (no button of the build menu has it).
constexpr std::int32_t kLandscapeGroup = 31099;

/// The group of the natives' and pirates' buildings, which no build menu lists: the original's
/// list after the menu's (Text.cod [LANDSCHAFT]) without its two forests, which the terrain has.
const ui::BuildCategory& landscape_group() {
    static const ui::BuildCategory group = [] {
        ui::BuildCategory c;
        c.button = kLandscapeGroup;
        c.names = "LANDSCHAFT";
        std::vector<ui::BuildButton> row;
        for (std::size_t i = 0; i < assets::id::kLandscape.size(); ++i) {
            const std::uint32_t id = assets::id::kLandscape[i];
            if (id != assets::id::island_shape::kForestNorth.front() &&
                id != assets::id::island_shape::kForestSouth.front()) {
                row.push_back(ui::BuildButton{id, 0, static_cast<std::int32_t>(i), {}, {}});
            }
        }
        c.rows.push_back(std::move(row));
        return c;
    }();
    return group;
}

/// The build menu's categories that list buildings (not the demolish tool), in the menu's order,
/// then the natives' and pirates' buildings.
std::vector<const ui::BuildCategory*> object_groups() {
    std::vector<const ui::BuildCategory*> groups;
    for (const ui::BuildCategory& c : ui::build_menu().categories) {
        if (!c.demolish && c.size() > 0) {
            groups.push_back(&c);
        }
    }
    groups.push_back(&landscape_group());
    return groups;
}

/// The number of groups of the tool "Build": the build menu's, the natives' and pirates', and last
/// the special objects (special_pieces()), which are placed without owner.
std::size_t object_group_count() {
    return object_groups().size() + 1;
}

/// Whether `group` is the group of the special objects.
bool special_group(std::size_t group) {
    return group % object_group_count() == object_group_count() - 1;
}

/// The name of a special object, in the player's language: a field by its crop and stage, a ruin or
/// building by its number.
std::string special_name(const editor::SpecialPiece& p, const assets::BuildingTable& buildings) {
    using Kind = editor::SpecialKind;
    const assets::BuildingType* b = buildings.by_id(p.id);
    const std::string number = std::to_string(p.id);
    if (p.kind == Kind::Growing || p.kind == Kind::Ripe || p.kind == Kind::Withered) {
        const std::uint8_t ware = b != nullptr ? sim::crop_ware(buildings, *b) : 0;
        const assets::BuildingType* ripe =
            b != nullptr && p.kind == Kind::Withered ? sim::ripe_of_withered(buildings, *b) : nullptr;
        const std::uint8_t crop = ripe != nullptr ? ripe->ware : ware;
        const std::string name = crop >= assets::ware::kCornField && crop <= assets::ware::kCocoaField
                                     ? ui::tr("editor.special.crop." + std::to_string(crop))
                                     : ui::tr("editor.special.crop", {number});
        if (p.kind == Kind::Growing) {
            return ui::tr("editor.special.growing", {name, std::to_string(p.stage + 1), std::to_string(p.stages)});
        }
        return ui::tr(p.kind == Kind::Ripe ? "editor.special.ripe" : "editor.special.withered", {name});
    }
    if (p.kind == Kind::Ruin) {
        return ui::tr(
            b != nullptr && b->kind == assets::kind::kBeachRuin ? "editor.special.beach_ruin" : "editor.special.ruin",
            {number});
    }
    if (p.id == assets::id::kUnusedBuildings[0]) {
        return ui::tr("editor.special.outpost", {number});
    }
    return ui::tr("editor.special.building", {number});
}

/// Why a special object is not placed, in the player's language.
std::string special_refusal(editor::SpecialRefusal refusal) {
    using R = editor::SpecialRefusal;
    switch (refusal) {
        case R::NoIsland:
            return ui::tr("editor.objects.refused.island");
        case R::Ground:
            return ui::tr("editor.objects.refused.special_ground");
        case R::Taken:
            return ui::tr("editor.objects.refused.special_taken");
        case R::Unit:
            return ui::tr("editor.objects.refused.unit_near");
        case R::None:
            break;
    }
    return {};
}

/// The natives and the pirates a map can still get (editor::can_add_party): their slots stand in
/// the list of owners before the world has them.
constexpr std::array<editor::Neutral, 2> kParties = {editor::Neutral::Natives, editor::Neutral::Pirates};

/// The party whose slot `owner` is while the world lacks it, if it is one.
std::optional<editor::Neutral> missing_party(const sim::World& world, sim::PlayerId owner) {
    for (const editor::Neutral party : kParties) {
        if (owner == editor::party_slot(party) && editor::can_add_party(world, party)) {
            return party;
        }
    }
    return std::nullopt;
}

/// The players a building, ship or unit can be placed for: humans and computers, then the natives
/// and the pirates; for a ship the free trader last, else the neutral buildings' player last. The
/// natives, the pirates and the neutral buildings' player stand there by their slots while the
/// world has none of them (a map of open sea): they come with the first choice of them,
/// MapViewer::editor_neutral_owner. A trade cart only for humans and computers; `existing` only
/// players the world has.
std::vector<sim::PlayerId> object_owners(const sim::World& world, bool ship = false, bool cart = false,
                                         bool existing = false) {
    std::vector<sim::PlayerId> owners;
    for (std::size_t i = 0; i < world.players().size(); ++i) {
        const auto p = static_cast<sim::PlayerId>(i);
        const sim::PlayerRole role = world.players()[i].role;
        if ((role == sim::PlayerRole::Human || role == sim::PlayerRole::Computer) &&
            world.players()[i].status != sim::PlayerStatus::Absent) {  // not a slot that only holds a place
            owners.push_back(p);
        }
    }
    if (cart) {
        return owners;
    }
    // The natives and the pirates by slot, those to come where they will stand: choosing one
    // keeps its place in the list.
    std::vector<sim::PlayerId> parties;
    for (std::size_t i = 0; i < world.players().size(); ++i) {
        const auto p = static_cast<sim::PlayerId>(i);
        if (world.players()[i].role == sim::PlayerRole::Neutral &&
            (world.acts_as(p, sim::FactionBehaviour::Natives) || world.acts_as(p, sim::FactionBehaviour::Pirates))) {
            parties.push_back(p);
        }
    }
    // The parties to come only beside a human or computer, whom they stand towards.
    for (const editor::Neutral party : kParties) {
        if (!owners.empty() && !existing && editor::can_add_party(world, party)) {
            parties.push_back(editor::party_slot(party));
        }
    }
    std::sort(parties.begin(), parties.end());
    owners.insert(owners.end(), parties.begin(), parties.end());
    if (ship) {
        const sim::PlayerId trader = world.first_of(sim::FactionBehaviour::Trader);
        if (trader != sim::kNoPlayer) {
            owners.push_back(trader);
        }
    } else if (!owners.empty()) {
        const sim::PlayerId neutral = editor::neutral_player(world);
        if (neutral != sim::kNoPlayer || !existing) {
            owners.push_back(neutral != sim::kNoPlayer ? neutral : editor::kNeutralSlot);
        }
    }
    return owners;
}

/// Whether `owner` is the neutral buildings' player, or its slot before the world has it.
bool neutral_owner(const sim::World& world, sim::PlayerId owner) {
    return world.acts_as(owner, sim::FactionBehaviour::Neutral) ||
           (owner == editor::kNeutralSlot && world.player(owner) == nullptr);
}

/// The owner as the column names it.
std::string owner_label(const sim::World& world, sim::PlayerId owner) {
    if (neutral_owner(world, owner)) {
        return ui::tr("editor.objects.owner_neutral");
    }
    const std::optional<editor::Neutral> missing = missing_party(world, owner);
    if (world.acts_as(owner, sim::FactionBehaviour::Natives) || missing == editor::Neutral::Natives) {
        return ui::tr("editor.objects.owner_natives");
    }
    if (world.acts_as(owner, sim::FactionBehaviour::Pirates) || missing == editor::Neutral::Pirates) {
        return ui::tr("editor.objects.owner_pirates");
    }
    const sim::Player* p = world.player(owner);
    return p != nullptr ? MapViewer::editor_player_label(*p) : std::string("-");
}

/// The name of a city the natives or pirates found in the editor; empty for the others, whose
/// cities take the next of the list of cities.
std::string own_city_name(const sim::World& world, sim::PlayerId owner) {
    if (world.acts_as(owner, sim::FactionBehaviour::Natives)) {
        return ui::tr("editor.city.natives");
    }
    if (world.acts_as(owner, sim::FactionBehaviour::Pirates)) {
        return ui::tr("editor.city.pirates");
    }
    if (world.acts_as(owner, sim::FactionBehaviour::Neutral)) {
        return ui::tr("editor.city.neutral");
    }
    return {};
}

/// Why a building is not placed, in the player's language.
std::string build_refusal(sim::BuildRefusal refusal) {
    using R = sim::BuildRefusal;
    switch (refusal) {
        case R::OutsideIsland:
            return ui::tr("editor.objects.refused.island");
        case R::Terrain:
        case R::Overlap:
        case R::UnitInTheWay:
        case R::Infertile:
            return ui::tr("editor.objects.refused.terrain");
        case R::Territory:
            return ui::tr("editor.objects.refused.territory");
        case R::ForeignCity:
            return ui::tr("editor.objects.refused.foreign");
        case R::CityLimit:
            return ui::tr("editor.objects.refused.city_limit");
        case R::SecondKontor:
            return ui::tr("editor.objects.refused.second_kontor");
        case R::NoCoast:
            return ui::tr("editor.objects.refused.coast");
        case R::NoDeposit:
        case R::NoRock:
            return ui::tr("editor.objects.refused.mine");
        case R::NoRiver:
            return ui::tr("editor.objects.refused.river");
        case R::Neutral:
            return ui::tr("editor.objects.refused.neutral");
        default:
            return ui::tr("editor.objects.refused.other", {sim::refusal_name(refusal)});
    }
}

}  // namespace

std::optional<editor::SpecialPiece> MapViewer::editor_special_piece() const {
    if (!m_editor || m_editor->objects.tool != EditorObjects::Tool::Build || !special_group(m_editor->objects.group)) {
        return std::nullopt;
    }
    const std::vector<editor::SpecialPiece> pieces = editor::special_pieces(m_buildings);
    if (pieces.empty()) {
        return std::nullopt;
    }
    return pieces[std::min(m_editor->objects.piece, pieces.size() - 1)];
}

std::optional<std::pair<std::uint16_t, sim::TilePos>> MapViewer::editor_special_origin(std::int64_t x,
                                                                                       std::int64_t y) const {
    const sim::World& world = m_state->world;
    const auto piece = editor_special_piece();
    const assets::BuildingType* b = piece ? m_buildings.by_id(piece->id) : nullptr;
    if (b == nullptr || x < 0 || y < 0 || x >= world.width() || y >= world.height()) {
        return std::nullopt;
    }
    const sim::World::Cell& c = world.cell(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    if (c.island == sim::World::kSea) {
        return std::nullopt;
    }
    std::uint32_t w = 1;
    std::uint32_t h = 1;
    sim::rotated_size(*b, m_editor->objects.rotation, w, h);
    // The footprint about the mouse, as for a building.
    return std::pair{c.island,
                     sim::TilePos{static_cast<std::int32_t>(c.local_x) - static_cast<std::int32_t>((w - 1) / 2),
                                  static_cast<std::int32_t>(c.local_y) - static_cast<std::int32_t>((h - 1) / 2)}};
}

editor::SpecialRefusal MapViewer::editor_special_fits(std::int64_t x, std::int64_t y) const {
    const auto piece = editor_special_piece();
    const auto origin = editor_special_origin(x, y);
    if (!piece || !origin) {
        return editor::SpecialRefusal::NoIsland;
    }
    return editor::special_fits(m_state->world, m_buildings, origin->first, origin->second, *piece,
                                m_editor->objects.rotation);
}

const assets::BuildingType* MapViewer::editor_object_piece() const {
    if (!m_editor) {
        return nullptr;
    }
    if (special_group(m_editor->objects.group)) {
        const auto piece = editor_special_piece();
        return piece ? m_buildings.by_id(piece->id) : nullptr;
    }
    const auto groups = object_groups();
    if (groups.empty()) {
        return nullptr;
    }
    const ui::BuildCategory& c = *groups[m_editor->objects.group % groups.size()];
    const auto id = c.at(std::min(m_editor->objects.piece, c.size() - 1));
    return id ? m_buildings.by_id(*id) : nullptr;
}

void MapViewer::editor_object_model(EditorModel& m) const {
    m.objects = m_editor->objects;
    const auto groups = object_groups();
    m.object_groups = object_group_count();
    const bool special = special_group(m_editor->objects.group);
    if (special) {
        const std::vector<editor::SpecialPiece> pieces = editor::special_pieces(m_buildings);
        m.object_pieces = pieces.size();
        m.object_group = ui::tr("editor.objects.group.special");
        if (!pieces.empty()) {
            const editor::SpecialPiece& p = pieces[std::min(m_editor->objects.piece, pieces.size() - 1)];
            m.object_name = special_name(p, m_buildings);
        }
    } else if (!groups.empty()) {
        const ui::BuildCategory& c = *groups[m_editor->objects.group % groups.size()];
        m.object_pieces = c.size();
        const std::size_t index = std::min(m_editor->objects.piece, c.size() - 1);
        const std::vector<std::string> none;
        const assets::TextTable* texts = m_hud ? m_hud->texts() : nullptr;
        std::string name = texts != nullptr ? c.name(index, texts->section(c.names)) : c.name(index, none);
        if (name.empty()) {
            const assets::BuildingType* b = m_buildings.by_id(c.at(index).value_or(0));
            name = b != nullptr ? editor_building_name(*b, m_buildings, texts)
                                : ui::tr("editor.prop.building_number", {std::to_string(c.at(index).value_or(0))});
        }
        m.object_name = std::move(name);
        m.object_group = ui::tr("editor.objects.group." + std::to_string(c.button));
    }
    if (const assets::BuildingType* b = editor_object_piece()) {
        m.object_turns = b->rotate > 0 || b->size_x != b->size_y;
    }
    using Tool = EditorObjects::Tool;
    const Tool tool = m_editor->objects.tool;
    m.object_ownerless = tool == Tool::Build && special;
    if (tool == Tool::Template) {
        m.object_pieces = m_templates ? m_templates->all().size() : 0;
        m.object_turns = true;
        const BuildTemplate* t = editor_template();
        m.object_name = t != nullptr ? t->name : std::string();
    }
    if (tool == Tool::Resource) {
        m.object_pieces = editor::kResources;
        m.object_turns = false;
        m.object_name =
            ui::tr("editor.objects.resource." + std::to_string(std::min(m.objects.resource, editor::kResources - 1)));
    }
    if (tool == Tool::Ship || tool == Tool::Unit) {
        const std::vector<std::uint16_t> figures = editor_figures(tool);
        m.object_pieces = figures.size();
        m.object_turns = true;
        m.object_name.clear();
        if (!figures.empty()) {
            const std::size_t index =
                std::min(tool == Tool::Ship ? m.objects.ship : m.objects.unit, figures.size() - 1);
            const std::uint16_t figure = figures[index];
            const char* key = tool == Tool::Ship ? "editor.objects.ship." : "editor.objects.unit.";
            if (const assets::OwnShip* own =
                    tool == Tool::Ship ? assets::find_own_ship(m_own_ships, figure) : nullptr) {
                m.object_name = own->name(ui::current_language());
            } else if (const assets::OwnUnit* unit =
                           tool == Tool::Unit ? assets::find_own_unit(m_own_units, figure) : nullptr) {
                m.object_name = unit->name(ui::current_language());
            } else if (figure == assets::figure::kPirate) {
                m.object_name = ui::tr("editor.objects.ship.pirate");
            } else if (figure == assets::figure::kTrader) {
                m.object_name = ui::tr("editor.objects.ship.trader");
            } else if (figure == assets::figure::kSpearman) {
                m.object_name = ui::tr("editor.objects.unit.spearman");
            } else if (tool == Tool::Unit && figure == assets::figure::kTradeCart) {
                m.object_name = ui::tr("editor.objects.unit.cart");
            } else if (index < 4) {
                m.object_name = ui::tr(key + std::to_string(index));
            } else {
                m.object_name = std::to_string(figure);
            }
        }
    }
    m.recent_objects = m_editor->recent_objects;
    const auto owners = object_owners(m_editor->document.world(), tool == Tool::Ship, editor_cart());
    m.object_owners = owners.size();
    if (!owners.empty()) {
        const sim::World& world = m_editor->document.world();
        const sim::PlayerId owner = owners[m_editor->objects.owner % owners.size()];
        m.object_neutral = neutral_owner(world, owner);
        m.object_owner = owner_label(world, owner);
    }
}

std::optional<render::TileLighting> MapViewer::editor_lighting() const {
    const EditorPanel* panel = m_hud ? m_hud->editor_panel() : nullptr;
    if (m_editor->objects.tool != EditorObjects::Tool::Build || panel == nullptr ||
        panel->page() != EditorPanel::Page::Objects || m_editor->placing) {
        return std::nullopt;
    }
    const assets::BuildingType* b = editor_object_piece();
    if (b == nullptr || special_group(m_editor->objects.group)) {
        return std::nullopt;
    }
    const sim::World& world = m_editor->document.world();
    const auto owners = object_owners(world);
    if (owners.empty()) {
        return std::nullopt;
    }
    const sim::PlayerId owner = owners[m_editor->objects.owner % owners.size()];
    if (world.player(owner) == nullptr || neutral_owner(world, owner)) {
        return std::nullopt;  // the neutral buildings have no land of their own
    }
    render::TileLighting l = render::TileLighting::territory(owner);
    const auto tile = m_input->tile_at_mouse();
    if (tile && shows_catchment(*b) && world.contains(tile->x, tile->y) &&
        !m_hud->covers(static_cast<std::int32_t>(m_input->mouse_x()), static_cast<std::int32_t>(m_input->mouse_y()))) {
        const sim::World::Cell& c = world.cell(tile->x, tile->y);
        if (c.island != sim::World::kSea) {
            l.outline(world_catchment(c.island, sim::catchment_of(*b, m_editor->objects.rotation,
                                                                  sim::TilePos{static_cast<std::int32_t>(c.local_x),
                                                                               static_cast<std::int32_t>(c.local_y)})));
        }
    }
    return l;
}

const sim::City* MapViewer::editor_picked_city() const {
    if (!m_editor || m_editor->pick.kind != EditorPick::Kind::Building) {
        return nullptr;
    }
    const sim::World& world = m_editor->document.world();
    const EditorPick& pick = m_editor->pick;
    if (pick.island >= world.islands().size()) {
        return nullptr;
    }
    const auto at = sim::building_at(world, m_buildings, pick.island,
                                     {static_cast<std::int32_t>(pick.x), static_cast<std::int32_t>(pick.y)});
    return at ? world.city(pick.island, at->city) : nullptr;
}

void MapViewer::editor_city_model(EditorModel& m) const {
    const sim::City* c = editor_picked_city();
    if (c == nullptr) {
        return;
    }
    const sim::World& world = m_editor->document.world();
    EditorCity city;
    city.name = c->name;
    city.owner = owner_label(world, c->owner);
    const auto* panel = m_hud ? m_hud->editor_panel() : nullptr;
    const bool trading = panel != nullptr && (panel->page() == EditorPanel::Page::CityTrade ||
                                              panel->page() == EditorPanel::Page::CityWare);
    if (panel != nullptr && panel->page() != EditorPanel::Page::CityGoods &&
        panel->page() != EditorPanel::Page::Selection && !trading) {
        m.city = std::move(city);  // the stock only where it shows: its room takes a look at every building
        return;
    }
    city.room = editor::city_room(world, m_buildings, c->island, c->index);
    const assets::TextTable* texts = m_hud ? m_hud->texts() : nullptr;
    const std::vector<std::string> names = texts != nullptr ? texts->section("WARE") : std::vector<std::string>{};
    for (const std::uint8_t ware : world.goods().trade_wares()) {
        EditorGood g;
        g.ware = ware;
        if (const assets::OwnGood* own = world.goods().own(ware)) {
            g.name = own->name(ui::current_language());
        } else {
            g.name = ware < names.size() ? names[ware] : std::to_string(ware);
        }
        g.amount = ware < c->goods.size() ? c->goods[ware] : 0;
        if (trading) {
            EditorTrade t;
            t.ware = ware;
            t.name = g.name;
            t.stock = g.amount;
            if (const sim::City::Trade* trade = c->trade_of(ware)) {
                t.mode = trade->sell  ? EditorTradeMode::Sell
                         : trade->buy ? EditorTradeMode::Buy
                                      : EditorTradeMode::None;
                t.price = trade->sell ? trade->sell_price : trade->buy_price;
                t.amount = trade->sell ? trade->keep : trade->limit;
            }
            const sim::TradePriceRange range = sim::trade_price_range(world, ware);
            t.min_price = range.min;
            t.max_price = range.max;
            city.trade.push_back(std::move(t));
        }
        city.goods.push_back(std::move(g));
    }
    city.natives = world.acts_as(c->owner, sim::FactionBehaviour::Natives);
    city.pirates = world.acts_as(c->owner, sim::FactionBehaviour::Pirates);
    if (trading) {
        const sim::TradeSlots slots = sim::trade_slots(world, m_buildings, *c);
        city.sells = slots.sells;
        city.buys = slots.buys;
        city.max_sells = slots.max_sells;
        city.max_buys = slots.max_buys;
    }
    m.city = std::move(city);
}

void MapViewer::editor_ship_model(EditorModel& m) const {
    if (m_editor->pick.kind != EditorPick::Kind::Ship) {
        return;
    }
    const sim::World& world = m_editor->document.world();
    const sim::Ship* s = world.ship(m_editor->pick.id);
    if (s == nullptr) {
        return;
    }
    const assets::TextTable* texts = m_hud ? m_hud->texts() : nullptr;
    const std::vector<std::string> names = texts != nullptr ? texts->section("WARE") : std::vector<std::string>{};
    const auto ware_name = [&](std::uint8_t ware) {
        if (const assets::OwnGood* own = world.goods().own(ware)) {
            return own->name(ui::current_language());
        }
        return ware < names.size() ? names[ware] : std::to_string(ware);
    };
    EditorShip ship;
    ship.name = ship_name(*s);
    ship.room = sim::hold_capacity(*s);
    for (const sim::CargoSlot& slot : s->cargo) {
        ship.holds.push_back(EditorGood{slot.ware, slot.ware != 0 ? ware_name(slot.ware) : std::string(),
                                        slot.ware != 0 ? slot.amount : 0U});
    }
    for (const std::uint8_t ware : world.goods().trade_wares()) {
        ship.wares.push_back(EditorGood{ware, ware_name(ware), 0});
    }
    m.ship = std::move(ship);
}

void MapViewer::editor_ship_request(const EditorRequest& request) {
    if (m_editor->pick.kind != EditorPick::Kind::Ship) {
        return;
    }
    auto op = editor::set_ship_cargo(m_editor->document.world(), m_editor->pick.id, request.index, request.ware,
                                     request.amount);
    if (!op) {
        editor_notice(op.error().message, true, op.error().message);
        return;
    }
    m_editor->document.perform(std::move(op).value());
    m_editor->notice.clear();
}

void MapViewer::editor_city_request(const EditorRequest& request) {
    const sim::City* c = editor_picked_city();
    if (c == nullptr) {
        return;
    }
    const sim::World& world = m_editor->document.world();
    const std::uint16_t island = c->island;
    const std::uint8_t index = c->index;
    using Kind = EditorRequest::Kind;
    if (request.kind == Kind::RenameCity) {
        auto op = editor::rename_city(world, island, index, request.name);
        if (!op) {
            editor_notice(op.error().message, true, op.error().message);
            return;
        }
        m_editor->document.perform(std::move(op).value());
        editor_notice(ui::tr("editor.city.renamed", {request.name}), false, "city renamed");
    } else if (request.kind == Kind::CityGoods) {
        auto op = editor::set_city_goods(world, m_buildings, island, index, static_cast<std::uint8_t>(request.index),
                                         request.amount);
        if (op) {
            m_editor->document.perform(std::move(op).value());
            m_editor->notice.clear();
        }
    } else if (request.kind == Kind::CityTrade) {
        sim::TradeSetting setting;
        setting.ware = static_cast<std::uint8_t>(request.index);
        setting.mode = request.trade.mode == EditorTradeMode::Sell  ? sim::TradeSetting::Mode::Sell
                       : request.trade.mode == EditorTradeMode::Buy ? sim::TradeSetting::Mode::Buy
                                                                    : sim::TradeSetting::Mode::None;
        setting.price = request.trade.price;
        setting.amount = request.trade.amount;
        auto op = editor::set_city_trade(world, m_buildings, island, index, setting);
        if (op) {
            m_editor->document.perform(std::move(op).value());
            m_editor->notice.clear();
        } else if (op.error().code == annokit::core::ErrorCode::Unsupported &&
                   setting.mode != sim::TradeSetting::Mode::None && !setting.price && !setting.amount) {
            editor_notice(ui::tr("editor.trade.no_slot"), true, op.error().message);
        }
    } else if (request.kind == Kind::CityTradeReset) {
        auto op = editor::reset_city_trade(world, island, index);
        if (op) {
            m_editor->document.perform(std::move(op).value());
            editor_notice(ui::tr("editor.trade.reset"), false, "city trade reset");
        }
    } else if (request.kind == Kind::CityOwner) {
        // The next player, the natives or the pirates that can take the city, forward or back.
        const auto owners = object_owners(world, false, false, true);
        const auto now = std::find(owners.begin(), owners.end(), c->owner);
        const std::size_t start = now == owners.end() ? 0 : static_cast<std::size_t>(now - owners.begin());
        for (std::size_t step = 1; step < owners.size(); ++step) {
            const std::size_t i =
                request.index == 1 ? (start + step) % owners.size() : (start + owners.size() - step) % owners.size();
            auto op = editor::change_city_owner(world, island, index, owners[i]);
            if (op) {
                m_editor->document.perform(std::move(op).value());
                editor_islands_changed();
                const sim::Player* p = m_editor->document.world().player(owners[i]);
                editor_notice(
                    ui::tr("editor.city.owner_changed", {p != nullptr ? editor_player_label(*p) : std::string()}),
                    false, "city owner changed");
                return;
            }
        }
        editor_notice(ui::tr("editor.city.no_owner"), true, "no player can take the city");
    }
}

void MapViewer::editor_objects_request(const EditorRequest& request) {
    EditorObjects o = request.objects;
    o.rotation &= 3U;
    if (o.tool == EditorObjects::Tool::Template && m_editor->objects.tool != EditorObjects::Tool::Template) {
        template_store().load();  // saved in a game meanwhile
    }
    m_editor->objects = o;
    m_editor->notice.clear();
    editor_neutral_owner();
}

void MapViewer::editor_neutral_owner() {
    const sim::World& world = m_editor->document.world();
    const sim::PlayerId owner = editor_object_owner();
    // The natives or the pirates chosen on a map without them: the world gets their slot.
    if (const std::optional<editor::Neutral> party = missing_party(world, owner)) {
        auto op = editor::add_party(world, *party);
        if (!op) {
            editor_notice(op.error().message, true, op.error().message);
            return;
        }
        m_editor->document.perform(std::move(op).value());
        return;
    }
    if (owner != editor::kNeutralSlot || world.player(owner) != nullptr) {
        return;
    }
    // The neutral buildings chosen for the first time: the world gets their player, which then
    // stands in the list of owners where its slot stood.
    auto op = editor::add_neutral(world);
    if (!op) {
        editor_notice(op.error().message, true, op.error().message);
        return;
    }
    m_editor->document.perform(std::move(op).value());
}

const BuildTemplate* MapViewer::editor_template() const {
    if (!m_editor || !m_templates || m_templates->all().empty()) {
        return nullptr;
    }
    const std::vector<BuildTemplate>& all = m_templates->all();
    return &all[std::min(m_editor->objects.stamp, all.size() - 1)];
}

std::optional<sim::BuildOrder> MapViewer::editor_template_order(std::int64_t x, std::int64_t y) const {
    const sim::World& world = m_editor->document.world();
    const BuildTemplate* t = editor_template();
    const sim::PlayerId owner = editor_object_owner();
    if (t == nullptr || owner == sim::kNoPlayer || x < 0 || y < 0 || x >= world.width() || y >= world.height()) {
        return std::nullopt;
    }
    const sim::World::Cell& c = world.cell(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    if (c.island == sim::World::kSea) {
        return std::nullopt;
    }
    BuildTool::Layout layout = t->layout;
    layout.rotate(m_editor->objects.rotation & 3);
    // The template hangs at the mouse by its middle, as in the copy tool's hand, and keeps its
    // buildings' rotations.
    sim::BuildOrder order;
    order.player = owner;
    order.island = c.island;
    order.free = true;
    order.orient_to_road = false;
    const std::int32_t ox = static_cast<std::int32_t>(c.local_x) - (layout.width - 1) / 2;
    const std::int32_t oy = static_cast<std::int32_t>(c.local_y) - (layout.height - 1) / 2;
    for (const BuildTool::Layout::Item& item : layout.items) {
        if (m_buildings.by_id(item.building) != nullptr) {
            order.items.push_back(sim::BuildItem{item.building, ox + item.x, oy + item.y, item.rotation, 0});
        }
    }
    if (order.items.empty()) {
        return std::nullopt;
    }
    return order;
}

void MapViewer::editor_place_template(std::int64_t x, std::int64_t y) {
    (void)template_store();  // read on first use
    editor_neutral_owner();  // an undo took it away
    if (editor_object_owner() == sim::kNoPlayer) {
        editor_notice(ui::tr("editor.objects.note.no_players"), true, "no owner");
        return;
    }
    if (editor_template() == nullptr) {
        editor_notice(ui::tr("editor.objects.note.no_templates"), true, "no template");
        return;
    }
    const auto order = editor_template_order(x, y);
    if (!order) {
        editor_notice(ui::tr("editor.objects.refused.island"), true, "template not on an island");
        return;
    }
    const std::size_t items = order->items.size();
    editor::PlacedBuildings placed =
        editor::place_buildings(m_editor->document.world(), m_buildings, *order, m_city_names,
                                own_city_name(m_editor->document.world(), order->player));
    if (!placed.step) {
        if (!placed.plan.items.empty()) {
            editor_notice(build_refusal(placed.plan.items.front().refusal), true, "template refused");
        }
        return;
    }
    m_editor->document.perform(std::move(placed.step));
    editor_islands_changed();
    m_editor->notice.clear();
    editor_remember_object();
    if (placed.plan.placed < items) {
        // Some of it did not fit: say how much stands.
        editor_notice(
            ui::tr("editor.objects.template_part", {std::to_string(placed.plan.placed), std::to_string(items)}), false,
            "template placed in part");
    }
}

std::optional<sim::BuildOrder> MapViewer::editor_object_order(std::int64_t x, std::int64_t y) const {
    const sim::World& world = m_state->world;
    const assets::BuildingType* b = editor_object_piece();
    const auto owners = object_owners(world);
    if (special_group(m_editor->objects.group) || b == nullptr || owners.empty() || x < 0 || y < 0 ||
        x >= world.width() || y >= world.height()) {
        return std::nullopt;
    }
    const sim::World::Cell& c = world.cell(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    if (c.island == sim::World::kSea) {
        return std::nullopt;
    }
    std::uint32_t fw = 0;
    std::uint32_t fh = 0;
    sim::rotated_size(*b, m_editor->objects.rotation, fw, fh);
    sim::BuildOrder order;
    order.player = owners[m_editor->objects.owner % owners.size()];
    order.island = c.island;
    order.free = true;
    // The footprint about the mouse, as the build tool lays it.
    const auto item = [&](std::uint8_t rotation, std::int32_t dx, std::int32_t dy) {
        std::uint32_t w = 0;
        std::uint32_t h = 0;
        sim::rotated_size(*b, rotation, w, h);
        return sim::BuildItem{b->id, static_cast<std::int32_t>(c.local_x) - static_cast<std::int32_t>((w - 1) / 2) + dx,
                              static_cast<std::int32_t>(c.local_y) - static_cast<std::int32_t>((h - 1) / 2) + dy,
                              rotation, 0};
    };
    order.items.push_back(item(m_editor->objects.rotation, 0, 0));
    // A building that needs a coast, river or rock face where it stands exactly (a Kontor, a
    // shipyard, a mill, a mine): the nearest place and turn under the mouse where it fits.
    const sim::BuildRefusal refused = sim::plan_build(world, m_buildings, order).items.front().refusal;
    if (refused == sim::BuildRefusal::NoCoast || refused == sim::BuildRefusal::NoRiver ||
        refused == sim::BuildRefusal::NoRock) {
        const auto half = static_cast<std::int32_t>(std::max(fw, fh) / 2);
        for (std::int32_t d = 0; d <= half; ++d) {
            for (std::int32_t dy = -d; dy <= d; ++dy) {
                for (std::int32_t dx = -d; dx <= d; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != d) {
                        continue;
                    }
                    for (std::uint8_t r = 0; r < 4; ++r) {
                        sim::BuildOrder tried = order;
                        tried.items.front() =
                            item(static_cast<std::uint8_t>((m_editor->objects.rotation + r) & 3U), dx, dy);
                        if (sim::plan_build(world, m_buildings, tried).placed > 0) {
                            return tried;
                        }
                    }
                }
            }
        }
    }
    return order;
}

bool MapViewer::editor_place_object(std::int64_t x, std::int64_t y, bool tell) {
    editor_neutral_owner();  // an undo took it away
    if (const auto piece = editor_special_piece()) {
        // A special object: without owner, on open land outside every city.
        const auto origin = editor_special_origin(x, y);
        if (!origin) {
            if (tell) {
                editor_notice(ui::tr("editor.objects.refused.island"), true, "special object not on an island");
            }
            return false;
        }
        auto op = editor::place_special(m_state->world, m_buildings, origin->first, origin->second, *piece,
                                        m_editor->objects.rotation);
        if (!op) {
            if (tell) {
                editor_notice(special_refusal(editor::special_fits(m_state->world, m_buildings, origin->first,
                                                                   origin->second, *piece, m_editor->objects.rotation)),
                              true, op.error().message);
            }
            return false;
        }
        (*op)->apply(m_state->world);
        m_editor->object_steps.push_back(std::move(op).value());
        return true;
    }
    const auto order = editor_object_order(x, y);
    if (!order) {
        if (tell) {
            const bool owners = !object_owners(m_state->world).empty();
            editor_notice(ui::tr(owners ? "editor.objects.refused.island" : "editor.objects.note.no_players"), true,
                          "building not placed");
        }
        return false;
    }
    editor::PlacedBuildings placed = editor::place_buildings(m_state->world, m_buildings, *order, m_city_names,
                                                             own_city_name(m_state->world, order->player));
    if (!placed.step) {
        if (tell && !placed.plan.items.empty()) {
            editor_notice(build_refusal(placed.plan.items.front().refusal), true, "building refused");
        }
        return false;
    }
    placed.step->apply(m_state->world);
    m_editor->object_steps.push_back(std::move(placed.step));
    return true;
}

bool MapViewer::editor_objects_stroke(std::optional<render::TilePos> tile, Stroke phase) {
    switch (phase) {
        case Stroke::Begin: {
            editor_end_objects();
            if (!tile) {
                return true;
            }
            if (m_editor->objects.tool == EditorObjects::Tool::Remove) {
                editor_remove_object(*tile);
                return true;
            }
            if (m_editor->objects.tool == EditorObjects::Tool::Resource) {
                editor_place_resource(*tile);
                return true;
            }
            if (m_editor->objects.tool == EditorObjects::Tool::Template) {
                editor_place_template(tile->x, tile->y);  // one at a click
                return true;
            }
            if (m_editor->objects.tool != EditorObjects::Tool::Build) {
                editor_place_figure(tile->x, tile->y);  // one at a click, not dragged
                return true;
            }
            m_editor->object_at = {tile->x, tile->y};
            m_editor->object_from = {tile->x, tile->y};
            m_editor->object_drag = true;
            (void)editor_place_object(tile->x, tile->y, true);
            if (!m_editor->object_steps.empty()) {
                editor_islands_changed();
            }
            return true;
        }
        case Stroke::Continue: {
            if (!m_editor->object_drag || !tile || !m_editor->object_at) {
                return m_editor->object_drag;
            }
            const assets::BuildingType* b = editor_object_piece();
            if (b == nullptr) {
                return true;
            }
            std::uint32_t fw = 0;
            std::uint32_t fh = 0;
            sim::rotated_size(*b, m_editor->objects.rotation, fw, fh);
            const auto [lx, ly] = *m_editor->object_at;
            // Shift held: a straight row, along the way the drag went further from where it began.
            std::int64_t tx = tile->x;
            std::int64_t ty = tile->y;
            if ((SDL_GetModState() & SDL_KMOD_SHIFT) != 0 && m_editor->object_from) {
                const auto [fx, fy] = *m_editor->object_from;
                if (std::abs(tx - fx) >= std::abs(ty - fy)) {
                    ty = fy;
                } else {
                    tx = fx;
                }
            }
            // Dragged: the next one where it no longer covers the last, along the way.
            bool placed = false;
            for (const auto& [px, py] : editor::line_tiles(lx, ly, tx, ty)) {
                const auto [ax, ay] = *m_editor->object_at;
                if (std::abs(px - ax) < static_cast<std::int64_t>(fw) &&
                    std::abs(py - ay) < static_cast<std::int64_t>(fh)) {
                    continue;
                }
                m_editor->object_at = {px, py};
                placed = editor_place_object(px, py, false) || placed;
            }
            if (placed) {
                editor_islands_changed();
            }
            return true;
        }
        case Stroke::End:
            editor_end_objects();
            return true;
    }
    return false;
}

std::vector<std::uint16_t> MapViewer::editor_figures(EditorObjects::Tool tool) const {
    std::vector<std::uint16_t> figures;
    if (tool == EditorObjects::Tool::Ship) {
        for (const std::uint16_t f : assets::figure::kShips) {
            figures.push_back(f);
        }
        for (const auto& [number, type] : m_figures.package_ships()) {
            figures.push_back(static_cast<std::uint16_t>(number));  // below kMaxFigures
        }
        if (m_figures.get(assets::figure::kPirate) != nullptr) {
            figures.push_back(assets::figure::kPirate);  // for the pirates
        }
        if (m_figures.get(assets::figure::kTrader) != nullptr &&
            m_editor->document.world().first_of(sim::FactionBehaviour::Trader) != sim::kNoPlayer) {
            figures.push_back(assets::figure::kTrader);  // for the free trader
        }
    } else if (tool == EditorObjects::Tool::Unit) {
        for (const std::uint32_t family : m_figures.unit_families()) {
            figures.push_back(static_cast<std::uint16_t>(family));
        }
        if (m_figures.get(assets::figure::kSpearman) != nullptr &&
            m_editor->document.world().first_of(sim::FactionBehaviour::Natives) != sim::kNoPlayer) {
            figures.push_back(assets::figure::kSpearman);  // for the natives
        }
        if (m_figures.get(assets::figure::kTradeCart) != nullptr) {
            figures.push_back(assets::figure::kTradeCart);  // a player's trade cart, on its roads
        }
    }
    return figures;
}

std::uint16_t MapViewer::editor_figure() const {
    const EditorObjects& o = m_editor->objects;
    const std::vector<std::uint16_t> figures = editor_figures(o.tool);
    if (figures.empty()) {
        return 0;
    }
    return figures[std::min(o.tool == EditorObjects::Tool::Ship ? o.ship : o.unit, figures.size() - 1)];
}

bool MapViewer::editor_cart() const {
    return m_editor->objects.tool == EditorObjects::Tool::Unit && editor_figure() == assets::figure::kTradeCart;
}

sim::PlayerId MapViewer::editor_object_owner() const {
    const auto owners =
        object_owners(m_editor->document.world(), m_editor->objects.tool == EditorObjects::Tool::Ship, editor_cart());
    return owners.empty() ? sim::kNoPlayer : owners[m_editor->objects.owner % owners.size()];
}

editor::UnitRefusal MapViewer::editor_figure_fits(std::int64_t x, std::int64_t y) const {
    const sim::World& world = m_editor->document.world();
    if (m_editor->objects.tool == EditorObjects::Tool::Ship) {
        return editor::ship_fits(world, m_buildings, x, y);
    }
    if (editor_cart()) {
        return editor::cart_fits(world, m_buildings, editor_object_owner(), x, y);
    }
    return editor::unit_fits(world, m_buildings, x, y);
}

std::string MapViewer::editor_figure_refusal(editor::UnitRefusal refused, const std::string& why) const {
    using R = editor::UnitRefusal;
    const bool ship = m_editor->objects.tool == EditorObjects::Tool::Ship;
    switch (refused) {
        case R::None:
            return {};
        case R::NoWater:
            return ui::tr("editor.objects.refused.water");
        case R::NoGround:
        case R::Outside:
            return ui::tr(ship ? "editor.objects.refused.water" : "editor.objects.refused.ground");
        case R::Taken:
            return ui::tr(
                ship ? "editor.objects.refused.ship_near"
                     : (editor_cart() ? "editor.objects.refused.cart_near" : "editor.objects.refused.unit_near"));
        case R::NoRoad:
            return ui::tr("editor.objects.refused.cart_road");
        case R::NoMarket:
            return ui::tr("editor.objects.refused.cart_market");
        case R::CartLimit:
            return ui::tr("editor.objects.refused.cart_limit");
        case R::NoOwner:
            if (editor_cart()) {
                return ui::tr("editor.objects.refused.cart_owner");
            }
            break;
        case R::NoType:
            break;
    }
    if (!ship && editor_figure() == assets::figure::kSpearman &&
        !m_editor->document.world().acts_as(editor_object_owner(), sim::FactionBehaviour::Natives)) {
        return ui::tr("editor.objects.refused.spearman");
    }
    if (ship && (editor_figure() == assets::figure::kTrader ||
                 m_editor->document.world().acts_as(editor_object_owner(), sim::FactionBehaviour::Trader))) {
        return ui::tr("editor.objects.refused.trader");  // his ship and no other, for him and no other
    }
    return ui::tr("editor.objects.refused.other", {why.empty() ? std::string(editor::refusal_name(refused)) : why});
}

void MapViewer::editor_place_figure(std::int64_t x, std::int64_t y) {
    editor_neutral_owner();  // an undo took it away
    const sim::World& world = m_editor->document.world();
    const bool ship = m_editor->objects.tool == EditorObjects::Tool::Ship;
    const sim::PlayerId owner = editor_object_owner();
    if (owner == sim::kNoPlayer) {
        editor_notice(ui::tr("editor.objects.note.no_players"), true, "no owner");
        return;
    }
    const auto direction = static_cast<std::uint8_t>((m_editor->objects.rotation & 3U) * 2U);
    const bool cart = editor_cart();
    auto op = ship   ? editor::place_ship(world, m_buildings, m_figures, owner, editor_figure(), x, y, direction)
              : cart ? editor::place_cart(world, m_buildings, m_figures, owner, x, y, direction)
                     : editor::place_unit(world, m_buildings, m_figures, owner, editor_figure(), x, y, direction);
    if (!op) {
        // Where the place would do, the type or its owner refused it.
        const editor::UnitRefusal refused = editor_figure_fits(x, y);
        const std::string text = editor_figure_refusal(
            refused == editor::UnitRefusal::None ? editor::UnitRefusal::NoType : refused, op.error().message);
        editor_notice(text, true, op.error().message);
        return;
    }
    m_editor->document.perform(std::move(op).value());
    editor_islands_changed();
    m_editor->notice.clear();
    editor_remember_object();
    // A computer disbands its soldiers on an island without a city of its own in peacetime: say so.
    const sim::World& placed = m_editor->document.world();
    const sim::Player* p = placed.player(owner);
    if (!ship && !cart && p != nullptr && p->role == sim::PlayerRole::Computer &&
        placed.contains(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y))) {
        const std::uint16_t island = placed.cell(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)).island;
        const bool city = std::any_of(placed.cities().begin(), placed.cities().end(),
                                      [&](const sim::City& c) { return c.owner == owner && c.island == island; });
        if (!city) {
            editor_notice(ui::tr("editor.objects.unit_disbanded"), false, "computer unit without a city");
        }
    }
}

void MapViewer::editor_place_resource(render::TilePos tile) {
    const sim::World& world = m_editor->document.world();
    const sim::World::Cell& c = world.cell(static_cast<std::uint32_t>(tile.x), static_cast<std::uint32_t>(tile.y));
    const auto resource = static_cast<editor::Resource>(std::min(m_editor->objects.resource, editor::kResources - 1));
    const sim::TilePos local{static_cast<std::int32_t>(c.local_x), static_cast<std::int32_t>(c.local_y)};
    if (c.island == sim::World::kSea) {
        editor_notice(ui::tr("editor.objects.refused.resource.0"), true, "resource not on an island");
        return;
    }
    auto op = editor::place_resource(world, m_buildings, c.island, local, resource);
    if (!op) {
        const editor::ResourceRefusal refused = editor::resource_fits(world, m_buildings, c.island, local, resource);
        editor_notice(ui::tr("editor.objects.refused.resource." + std::to_string(static_cast<int>(refused) - 1)), true,
                      op.error().message);
        return;
    }
    m_editor->document.perform(std::move(op).value());
    editor_islands_changed();
    m_editor->notice.clear();
    editor_remember_object();
}

void MapViewer::editor_remove_object(render::TilePos tile) {
    const sim::World& world = m_editor->document.world();
    if (const auto unit = m_input->unit_at_mouse()) {
        auto op = unit->kind == ui::UnitRef::Kind::Ship ? editor::remove_ship(world, unit->id)
                                                        : editor::remove_unit(world, unit->id);
        if (op) {
            m_editor->document.perform(std::move(op).value());
            editor_islands_changed();
            m_editor->notice.clear();
            return;
        }
    }
    const sim::World::Cell& c = world.cell(static_cast<std::uint32_t>(tile.x), static_cast<std::uint32_t>(tile.y));
    if (c.island == sim::World::kSea) {
        editor_notice(ui::tr("editor.objects.refused.nothing"), true, "nothing to take away");
        return;
    }
    const sim::TilePos local{static_cast<std::int32_t>(c.local_x), static_cast<std::int32_t>(c.local_y)};
    // A treasure, deer or vein there before the building over it.
    if (auto riches = editor::remove_resource(world, m_buildings, c.island, local)) {
        m_editor->document.perform(std::move(riches).value());
        editor_islands_changed();
        m_editor->notice.clear();
        return;
    }
    auto op = editor::remove_building(
        world, m_buildings, c.island,
        sim::TilePos{static_cast<std::int32_t>(c.local_x), static_cast<std::int32_t>(c.local_y)});
    if (!op) {
        editor_notice(ui::tr(op.error().code == annokit::core::ErrorCode::NotFound ? "editor.objects.refused.nothing"
                                                                                   : "editor.objects.refused.remove"),
                      true, op.error().message);
        return;
    }
    m_editor->document.perform(std::move(op).value());
    editor_islands_changed();
    m_editor->notice.clear();
}

void MapViewer::editor_end_objects() {
    if (!m_editor) {
        return;
    }
    m_editor->object_drag = false;
    m_editor->object_at.reset();
    m_editor->object_from.reset();
    if (m_editor->object_steps.empty()) {
        return;
    }
    editor_remember_object();
    if (m_editor->object_steps.size() == 1) {
        m_editor->document.record(std::move(m_editor->object_steps.front()));
    } else {
        m_editor->document.record(std::make_unique<editor::Steps>(std::move(m_editor->object_steps), "building_place"));
    }
    m_editor->object_steps.clear();
    m_editor->notice.clear();
}

void MapViewer::editor_remember_object() {
    // The object in hand first among the last ones placed; the same one again only once.
    EditorModel m;
    editor_object_model(m);
    if (m.object_name.empty()) {
        return;
    }
    // The same object: the same tool and piece, whatever its owner or turn.
    const auto key = [](const EditorObjects& o) {
        using Tool = EditorObjects::Tool;
        switch (o.tool) {
            case Tool::Template:
                return std::pair<std::size_t, std::size_t>{o.stamp, 0};
            case Tool::Ship:
                return std::pair<std::size_t, std::size_t>{o.ship, 0};
            case Tool::Unit:
                return std::pair<std::size_t, std::size_t>{o.unit, 0};
            case Tool::Resource:
                return std::pair<std::size_t, std::size_t>{o.resource, 0};
            default:
                return std::pair<std::size_t, std::size_t>{o.group, o.piece};
        }
    };
    const auto same = [&](const EditorRecent& r) {
        return r.objects.tool == m_editor->objects.tool && key(r.objects) == key(m_editor->objects);
    };
    auto& recent = m_editor->recent_objects;
    recent.erase(std::remove_if(recent.begin(), recent.end(), same), recent.end());
    recent.insert(recent.begin(), EditorRecent{m_editor->objects, m.object_name});
    if (recent.size() > kEditorRecent) {
        recent.resize(kEditorRecent);
    }
}

std::string MapViewer::editor_hover_refusal(std::int64_t x, std::int64_t y) const {
    const EditorPanel* panel = m_hud ? m_hud->editor_panel() : nullptr;
    if (!m_editor || panel == nullptr || panel->page() != EditorPanel::Page::Objects || m_editor->placing ||
        m_editor->object_drag) {
        return {};
    }
    const sim::World& world = m_state->world;
    if (x < 0 || y < 0 || x >= world.width() || y >= world.height()) {
        return {};
    }
    using Tool = EditorObjects::Tool;
    const Tool tool = m_editor->objects.tool;
    if (tool == Tool::Build && special_group(m_editor->objects.group)) {
        return special_refusal(editor_special_fits(x, y));
    }
    if (tool == Tool::Build) {
        const auto order = editor_object_order(x, y);
        if (!order) {
            return ui::tr(object_owners(world).empty() ? "editor.objects.note.no_players"
                                                       : "editor.objects.refused.island");
        }
        sim::BuildPlan plan = sim::plan_build(world, m_buildings, *order);
        (void)editor::refuse_second_city(world, *order, plan);
        return !plan.items.empty() && !plan.items.front().ok() ? build_refusal(plan.items.front().refusal)
                                                               : std::string();
    }
    if (tool == Tool::Ship || tool == Tool::Unit) {
        if (editor_cart() && editor_object_owner() == sim::kNoPlayer) {
            return ui::tr("editor.objects.note.no_players");
        }
        return editor_figure_refusal(editor_figure_fits(x, y));
    }
    if (tool == Tool::Resource) {
        const sim::World::Cell& c = world.cell(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
        if (c.island == sim::World::kSea) {
            return ui::tr("editor.objects.refused.resource.0");
        }
        const auto resource =
            static_cast<editor::Resource>(std::min(m_editor->objects.resource, editor::kResources - 1));
        const sim::TilePos local{static_cast<std::int32_t>(c.local_x), static_cast<std::int32_t>(c.local_y)};
        const editor::ResourceRefusal refused = editor::resource_fits(world, m_buildings, c.island, local, resource);
        return refused == editor::ResourceRefusal::None
                   ? std::string()
                   : ui::tr("editor.objects.refused.resource." + std::to_string(static_cast<int>(refused) - 1));
    }
    return {};
}

}  // namespace o1602::app
