// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's players (ADR 0013): the page "Players" shows the humans' and computers' slots and
// changes what each is and starts with, adds slots up to sim::kMaxPlayers and takes the last away, each
// change a step of undo.

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "map_viewer.hpp"
#include "names.hpp"

#include "o1602/assets/localized_names.hpp"
#include "o1602/editor/players.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

/// The colour of a banner as the multiplayer lobby names it, Windows-1252.
std::string colour_name(sim::BannerId banner) {
    const auto banners = editor::player_banners();
    const auto it = std::find(banners.begin(), banners.end(), banner);
    if (it == banners.end()) {
        return std::to_string(banner + 1);
    }
    return player_colour_name(static_cast<std::size_t>(it - banners.begin()));
}

}  // namespace

std::string MapViewer::editor_player_label(const sim::Player& p) {
    // "Nameless", the name given to whoever named none, tells the players apart no better: such a
    // player goes by its colour, as the others do ("Red player").
    return p.name.empty() || p.name == ui::tr("menu.default_name") ? colour_player_name(p.banner, {}) : p.name;
}

void MapViewer::editor_players_model(EditorModel& m) const {
    const sim::World& world = m_editor->document.world();
    for (const sim::PlayerId slot : editor::player_slots(world)) {
        const sim::Player& p = world.players()[slot];
        EditorPlayer e;
        e.slot = slot;
        e.name = editor_player_label(p);
        e.own_name = p.name;
        e.human = p.role == sim::PlayerRole::Human;
        e.banner = p.banner;
        e.money = p.money;
        e.monuments_allowed = p.monuments_allowed;
        e.arches_allowed = p.arches_allowed;
        const auto stage = editor::unlock_stage(p, m_buildings.infra_levels());
        e.unlock_stage = stage ? static_cast<std::int8_t>(*stage) : std::int8_t{-1};
        e.colour = colour_name(p.banner);
        e.faction = p.faction;
        e.attitude_flags = p.scenario.flags & editor::kAttitudeFlags;
        e.own_development = p.scenario.own_development;
        e.rival_development = p.scenario.rival_development;
        e.own_inhabitants = p.scenario.own_inhabitants;
        e.rival_inhabitants = p.scenario.rival_inhabitants;
        e.first_island = static_cast<std::uint8_t>(p.first_island);
        for (std::size_t i = 0; i < world.islands().size(); ++i) {
            if (p.first_island == sim::FirstIsland::Fixed &&
                world.islands()[i].header.number == p.first_island_number) {
                e.first_island_index = static_cast<std::int32_t>(i);
            }
        }
        // A package's faction by the name its package gives it, else by its own id ("league" of
        // "example:league"): the table has little room. A saved game's factions have no names: the
        // installed packages' have.
        const sim::Faction& faction = world.faction(p);
        const auto installed = std::find_if(m_factions.begin(), m_factions.end(),
                                            [&](const sim::Faction& f) { return f.id == faction.id; });
        const auto& names = !faction.names.empty() || installed == m_factions.end() ? faction.names : installed->names;
        e.faction_name =
            p.faction == sim::faction::kSettlers
                ? ui::tr("editor.players.settlers")
                : assets::name_in(names, ui::current_language(), faction.id.substr(faction.id.find(':') + 1));
        m.player_slots.push_back(std::move(e));
    }
    for (EditorPlayer& e : m.player_slots) {
        for (const EditorPlayer& other : m.player_slots) {
            const editor::Relations r = editor::relations(world, e.slot, other.slot);
            e.relations.push_back(EditorPlayer::Standing{r.peace, r.trade});
        }
    }
    for (const editor::Neutral party : editor::neutral_parties(world)) {
        m.neutrals.push_back(static_cast<EditorNeutral>(party));
    }
    m.native_villages = editor::has_native_villages(world);
    for (EditorPlayer& e : m.player_slots) {
        for (const EditorNeutral party : m.neutrals) {
            const editor::NeutralRelations r =
                editor::neutral_relations(world, e.slot, static_cast<editor::Neutral>(party));
            e.towards.push_back(EditorPlayer::Towards{r.peace, r.trade, r.goodwill, r.lasting});
        }
    }
    m.slot_factions = editor::slot_factions(world);
    m.can_add_player = m.player_slots.size() < sim::kMaxPlayers;
    // The last player, as long as it owns nothing (else the refusal says so); any slot, an
    // original's scenario's four too.
    m.can_remove_player = !m.player_slots.empty();
}

void MapViewer::editor_players_request(const EditorRequest& request) {
    const sim::World& world = m_editor->document.world();
    using Kind = EditorRequest::Kind;
    if (request.kind == Kind::AddPlayer || request.kind == Kind::RemovePlayer) {
        auto op = request.kind == Kind::AddPlayer ? editor::add_player(world) : editor::remove_last_player(world);
        if (!op) {
            editor_notice(ui::tr(request.kind == Kind::AddPlayer ? "editor.players.refused.add"
                                                                 : "editor.players.refused.remove"),
                          true, op.error().message);
            return;
        }
        m_editor->document.perform(std::move(op).value());
        editor_islands_changed();
        editor_notice(ui::tr(request.kind == Kind::AddPlayer ? "editor.players.added" : "editor.players.removed"),
                      false, request.kind == Kind::AddPlayer ? "player added" : "player removed");
        return;
    }
    if (request.kind == Kind::SetRelations) {
        if (request.player.relations.empty()) {
            return;
        }
        const EditorPlayer::Standing s = request.player.relations.front();
        auto op = editor::set_relations(world, request.player.slot, static_cast<sim::PlayerId>(request.index),
                                        editor::Relations{s.peace, s.trade});
        if (!op) {
            editor_notice(ui::tr("editor.relations.refused"), true, op.error().message);
            return;
        }
        m_editor->document.perform(std::move(op).value());
        m_editor->notice.clear();
        std::fprintf(stderr, "editor: relations of players %u and %u changed\n",
                     static_cast<unsigned>(request.player.slot), static_cast<unsigned>(request.index));
        return;
    }
    if (request.kind == Kind::SetNeutral) {
        if (request.player.towards.empty() || request.index > static_cast<std::size_t>(editor::Neutral::Trader)) {
            return;
        }
        const EditorPlayer::Towards& t = request.player.towards.front();
        auto op = editor::set_neutral_relations(world, request.player.slot, static_cast<editor::Neutral>(request.index),
                                                editor::NeutralRelations{t.peace, t.trade, t.goodwill, t.lasting});
        if (!op) {
            editor_notice(ui::tr("editor.relations.refused"), true, op.error().message);
            return;
        }
        m_editor->document.perform(std::move(op).value());
        m_editor->notice.clear();
        std::fprintf(stderr, "editor: relations of player %u towards party %u changed\n",
                     static_cast<unsigned>(request.player.slot), static_cast<unsigned>(request.index));
        return;
    }
    // The slot: the one the request names, or the one the page shows (a name typed).
    const std::vector<sim::PlayerId> slots = editor::player_slots(world);
    if (slots.empty()) {
        return;
    }
    sim::PlayerId slot = request.player.slot;
    if (request.index == 3 || request.index == 6) {
        const EditorPanel* panel = m_hud ? m_hud->editor_panel() : nullptr;
        slot = slots[std::min(panel != nullptr ? panel->chosen_player() : 0, slots.size() - 1)];
    }
    const sim::Player* p = world.player(slot);
    if (p == nullptr) {
        return;
    }
    if (request.index == 7) {
        // Open1602 (feedback 16, V1): the buildings open from the start, up to a stage.
        auto op = editor::set_unlock_stage(world, slot, m_buildings.infra_levels(),
                                           static_cast<std::uint8_t>(std::max<int>(request.player.unlock_stage, 0)));
        if (!op) {
            editor_notice(ui::tr("editor.players.refused.change"), true, op.error().message);
            return;
        }
        m_editor->document.perform(std::move(op).value());
        m_editor->notice.clear();
        std::fprintf(stderr, "editor: buildings of player %u open to stage %d\n", static_cast<unsigned>(slot),
                     static_cast<int>(request.player.unlock_stage));
        return;
    }
    editor::PlayerValues v = editor::player_values(*p);
    if (request.index == 3) {
        v.name = request.name;
    } else if (request.index == 6) {
        // The money as typed: digits, a minus in front for a debt (the page checked them).
        long long typed = 0;
        const char* const end = request.name.data() + request.name.size();
        if (const auto [at, ec] = std::from_chars(request.name.data(), end, typed); ec != std::errc{} || at != end) {
            return;
        }
        v.money =
            static_cast<std::int32_t>(std::clamp<long long>(typed, editor::kMinStartMoney, editor::kMaxStartMoney));
    } else if (request.index == 5) {
        v.attitude.flags = request.player.attitude_flags & editor::kAttitudeFlags;
        v.attitude.own_development = request.player.own_development;
        v.attitude.rival_development = request.player.rival_development;
        v.attitude.own_inhabitants = request.player.own_inhabitants;
        v.attitude.rival_inhabitants = request.player.rival_inhabitants;
    } else if (request.index == 8) {
        // Open1602: where a computer founds its first city; an island by its number, which stays
        // when islands before it are taken out.
        v.first_island = static_cast<sim::FirstIsland>(
            std::min<std::uint8_t>(request.player.first_island, static_cast<std::uint8_t>(sim::FirstIsland::Last)));
        const auto i = static_cast<std::size_t>(std::max(request.player.first_island_index, 0));
        if (v.first_island == sim::FirstIsland::Fixed) {
            if (i >= world.islands().size()) {
                return;
            }
            v.first_island_number = world.islands()[i].header.number;
        }
    } else if (request.index == 1 || request.index == 2) {
        // The next colour, or the one before; one another player has is exchanged with it.
        const auto banners = editor::player_banners();
        const auto it = std::find(banners.begin(), banners.end(), v.banner);
        const std::size_t at = it == banners.end() ? 0 : static_cast<std::size_t>(it - banners.begin());
        v.banner = banners[request.index == 1 ? (at + 1) % banners.size() : (at + banners.size() - 1) % banners.size()];
    } else if (request.index == 4) {
        v.faction = request.player.faction;
    } else {
        v.role = request.player.human ? sim::PlayerRole::Human : sim::PlayerRole::Computer;
        v.money = std::clamp(request.player.money, editor::kMinStartMoney, editor::kMaxStartMoney);
        v.monuments_allowed = request.player.monuments_allowed;
        v.arches_allowed = request.player.arches_allowed;
    }
    auto op = editor::set_player(world, slot, v);
    if (!op) {
        editor_notice(ui::tr("editor.players.refused.change"), true, op.error().message);
        return;
    }
    m_editor->document.perform(std::move(op).value());
    m_editor->notice.clear();
    std::fprintf(stderr, "editor: player %u changed\n", static_cast<unsigned>(slot));
}

}  // namespace o1602::app
