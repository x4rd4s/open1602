// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's dialog "Goals" (ADR 0013): the scenario's missions with all the original's goals and
// loss conditions, who holds which, and their texts; editor::goals has the rules.

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "map_viewer.hpp"
#include "names.hpp"

#include "o1602/assets/game_data.hpp"
#include "o1602/editor/goals.hpp"
#include "o1602/editor/players.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

using editor::GoalField;

/// The label of each field, by editor::GoalField.
constexpr std::array<const char*, editor::kGoalFields> kGoalLabels = {
    "editor.goals.cities",        "editor.goals.inhabitants",    "editor.goals.balance",
    "editor.goals.trade_balance", "editor.goals.defeated",       "editor.goals.defeat_player",
    "editor.goals.castle",        "editor.goals.cathedral",      "editor.goals.goods1",
    "editor.goals.goods1_amount", "editor.goals.goods2",         "editor.goals.goods2_amount",
    "editor.goals.islands1",      "editor.goals.islands2",       "editor.goals.city1",
    "editor.goals.city_class",    "editor.goals.city_count",     "editor.goals.city2",
    "editor.goals.city_class",    "editor.goals.city_count",     "editor.goals.city3",
    "editor.goals.city_class",    "editor.goals.city_count",     "editor.goals.every_city",
    "editor.goals.city_class",    "editor.goals.city_count",     "editor.goals.target",
    "editor.goals.target_city",   "editor.goals.protected_city", "editor.goals.protected_cities",
    "editor.goals.computers",     "editor.goals.message",
};

std::string number(std::int64_t n) {
    return std::to_string(n);
}

}  // namespace

std::string MapViewer::editor_goal_text(const sim::Mission& m, std::size_t field) const {
    const auto f = static_cast<GoalField>(field);
    const sim::World& world = m_editor->document.world();
    const std::int64_t v = editor::goal_value(m, f);
    const bool asked = editor::goal_asked(m, f);
    const auto off = [] { return ui::tr("editor.goals.off"); };
    const auto who = [&](std::int64_t slot_plus_one) {
        const sim::Player* p = world.player(static_cast<sim::PlayerId>(slot_plus_one - 1));
        return p != nullptr ? player_name(*p) : ui::tr("editor.goals.nobody");
    };
    switch (f) {
        case GoalField::Balance:
        case GoalField::TradeBalance:
            return asked ? ui::tr("editor.goals.per_minute", {number(v)}) : off();
        case GoalField::DefeatPlayer:
            return v == 0 ? off() : who(v);
        case GoalField::Castle:
        case GoalField::Cathedral:
        case GoalField::Computers:
            return ui::tr(asked ? "editor.goals.yes" : "editor.goals.no");
        case GoalField::Goods1:
        case GoalField::Goods2: {
            if (v == 0) {
                return off();
            }
            const auto ware = static_cast<std::uint8_t>(v);
            if (const assets::OwnGood* own = world.goods().own(ware)) {
                return own->name(ui::current_language());
            }
            const assets::TextTable* texts = m_hud ? m_hud->texts() : nullptr;
            const std::vector<std::string> names =
                texts != nullptr ? texts->section("WARE") : std::vector<std::string>{};
            return ware < names.size() ? names[ware] : number(ware);
        }
        case GoalField::Goods1Amount:
        case GoalField::Goods2Amount:
            return ui::tr("editor.goals.tons", {number(v)});
        case GoalField::Islands1:
        case GoalField::Islands2: {
            namespace ware = assets::ware;
            switch (v) {
                case 0:
                    return off();
                case ware::kTobaccoField:
                    return ui::tr("editor.crop.tobacco");
                case ware::kSpiceField:
                    return ui::tr("editor.crop.spices");
                case ware::kSugarField:
                    return ui::tr("editor.crop.sugar");
                case ware::kCottonField:
                    return ui::tr("editor.crop.cotton");
                case ware::kWineField:
                    return ui::tr("editor.crop.wine");
                case ware::kCocoaField:
                    return ui::tr("editor.crop.cocoa");
                case ware::kOre:
                    return ui::tr("editor.goals.ore");
                case ware::kGold:
                    return ui::tr("editor.goals.gold");
                default:
                    return number(v);
            }
        }
        case GoalField::City1Class:
        case GoalField::City2Class:
        case GoalField::City3Class:
        case GoalField::EveryCityClass:
            return ui::tr("editor.goals.class." + number(v % editor::kGoalClasses));
        case GoalField::City1Count:
        case GoalField::City2Count:
        case GoalField::City3Count:
        case GoalField::EveryCityCount:
            return v == 0 ? off() : number(v);
        case GoalField::Target:
            return v == 0 ? ui::tr("editor.goals.any_opponent") : who(v);
        case GoalField::Message:
            return ui::tr(v == editor::kMessageColourWin ? "editor.goals.message.colour" : "editor.goals.message.done");
        default:
            return asked && v != 0 ? number(v) : off();
    }
}

void MapViewer::editor_goals_model(EditorModel& m) const {
    const sim::World& world = m_editor->document.world();
    const std::vector<sim::PlayerId> slots = editor::player_slots(world);
    m.max_missions = editor::kMaxMissions;
    static_assert(editor::kMaxMissions == 16 && editor::kMaxMissionText == 1000, "the panel's (kMissionTabs)");
    for (std::size_t mi = 0; mi < world.missions().size(); ++mi) {
        const sim::Mission& mission = world.missions()[mi];
        EditorMission e;
        e.text = mission.text;
        for (std::size_t f = 0; f < editor::kGoalFields; ++f) {
            e.goals.push_back(EditorRule{ui::tr(kGoalLabels[f]), editor_goal_text(mission, f),
                                         editor::goal_used(mission, static_cast<GoalField>(f))});
        }
        for (const sim::PlayerId s : slots) {
            e.holders.push_back(world.players()[s].mission == mi);
        }
        m.missions.push_back(std::move(e));
    }
}

void MapViewer::editor_goals_request(const EditorRequest& request) {
    using Kind = EditorRequest::Kind;
    const sim::World& world = m_editor->document.world();
    core::Result<std::unique_ptr<editor::Operation>> op = core::make_error(core::ErrorCode::NotFound, "nothing to do");
    switch (request.kind) {
        case Kind::MissionAdd: {
            sim::Mission fresh;
            fresh.success_message = editor::kMessageAccomplished;
            op = editor::set_mission(world, world.missions().size(), std::move(fresh));
            break;
        }
        case Kind::MissionRemove:
            op = editor::remove_mission(world, request.index);
            break;
        case Kind::MissionHolder: {
            const std::vector<sim::PlayerId> slots = editor::player_slots(world);
            if (request.amount >= slots.size()) {
                return;
            }
            const sim::PlayerId slot = slots[request.amount];
            const bool holds = world.players()[slot].mission == request.index;
            op = editor::set_mission_holder(world, slot,
                                            holds ? sim::kNoMission : static_cast<sim::MissionId>(request.index));
            break;
        }
        case Kind::GoalStep: {
            if (request.index >= world.missions().size() || request.goal >= editor::kGoalFields) {
                return;
            }
            sim::Mission m = world.missions()[request.index];
            editor::step_goal(world, m, static_cast<GoalField>(request.goal), request.amount != 0);
            op = editor::set_mission(world, request.index, std::move(m));
            break;
        }
        case Kind::MissionText: {
            if (request.index >= world.missions().size()) {
                return;
            }
            sim::Mission m = world.missions()[request.index];
            m.text = request.name;
            op = editor::set_mission(world, request.index, std::move(m));
            break;
        }
        default:
            return;
    }
    if (!op) {
        editor_notice(ui::tr("editor.goals.refused"), true, op.error().message);
        return;
    }
    m_editor->document.perform(std::move(op).value());
    m_editor->notice.clear();
    std::fprintf(stderr, "editor: missions changed (%zu)\n", m_editor->document.world().missions().size());
}

}  // namespace o1602::app
