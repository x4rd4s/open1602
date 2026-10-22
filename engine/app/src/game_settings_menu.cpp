// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_settings_menu.hpp"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>

#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

using Id = ui::Form::Id;
using Tone = ui::Form::Tone;
using ui::Align;
using ui::Rect;

constexpr Id kItem = 1;  // + SetupItem
constexpr Id kOriginal = 50;
constexpr Id kDefaults = 51;
constexpr Id kBack = 52;
constexpr Id kStart = 53;
constexpr Id kVictory = 54;     // the page of the victory conditions
constexpr Id kVictoryOff = 55;  // all of them off

// The frame and the bottom row, as the multiplayer menu's.
constexpr Rect kScreen{16, 16, 992, 736};
constexpr Rect kHeading{40, 28, 760, 40};
constexpr std::int32_t kBottomY = 690;
constexpr std::int32_t kHintY = 592;  ///< the hint's top when the groups end above it

constexpr std::int32_t kTop = 84;
constexpr std::int32_t kGap = 8;
constexpr std::int32_t kRowH = 36;
constexpr std::int32_t kLabelW = 236;
constexpr std::int32_t kValueW = 196;

struct Group {
    std::string_view heading;
    SetupItem first;
    SetupItem last;  ///< inclusive
    std::int32_t x;  ///< the column; the groups of a column follow each other from the top
};

constexpr std::int32_t kLeft = 40;
constexpr std::int32_t kRight = 524;

// Economy, limits and disasters on the left, the world and the opponents on the right.
constexpr Group kGroups[] = {
    {"setup.group.economy", SetupItem::Refund, SetupItem::TraderPrices, kLeft},
    {"setup.group.limits", SetupItem::Ships, SetupItem::GameSpeed, kLeft},
    {"setup.group.disasters", SetupItem::Droughts, SetupItem::Volcanoes, kLeft},
    {"setup.group.world", SetupItem::Map, SetupItem::Resources, kRight},
    {"setup.group.opponents", SetupItem::ClassicAi, SetupItem::Pirates, kRight},
};

// The victory conditions, after the endless games of Anno 1503, 1701 and 1404: population and
// wealth on the left, power and the end of the game on the right.
constexpr Group kVictoryGroups[] = {
    {"setup.group.population", SetupItem::WinInhabitants, SetupItem::WinClassInhabitants, kLeft},
    {"setup.group.wealth", SetupItem::WinMoney, SetupItem::WinBalance, kLeft},
    {"setup.group.power", SetupItem::WinIslands, SetupItem::WinMonument, kRight},
    {"setup.group.end", SetupItem::WinTimeLimit, SetupItem::WinComputers, kRight},
};

std::string_view title_key(SetupScope scope) {
    switch (scope) {
        case SetupScope::MultiplayerEndless:
            return "setup.title.multiplayer";
        case SetupScope::Scenario:
            return "setup.title.scenario";
        case SetupScope::Savegame:
            return "setup.title.savegame";
        case SetupScope::Endless:
            break;
    }
    return "setup.title";
}

std::string_view hint_key(SetupScope scope) {
    switch (scope) {
        case SetupScope::MultiplayerEndless:
            return "setup.hint.multiplayer";
        case SetupScope::Scenario:
            return "setup.hint.scenario";
        case SetupScope::Savegame:
            return "setup.hint.savegame";
        case SetupScope::Endless:
            break;
    }
    return "setup.hint";
}

}  // namespace

bool GameSettingsMenu::back() noexcept {
    if (m_page == Page::Victory) {
        m_page = Page::Settings;
        return true;
    }
    return false;
}

std::vector<ui::DrawItem> GameSettingsMenu::draw(const std::array<const ui::Font*, 3>& fonts) {
    layout();
    return m_form.draw(fonts);
}

GameSettingsMenu::Result GameSettingsMenu::mouse(std::int32_t x, std::int32_t y, bool down, ui::MouseButton button) {
    layout();
    const auto id = m_form.mouse(x, y, down, button);
    if (!id) {
        return Result::None;
    }
    const bool editable = m_mode != Mode::View;
    if (*id >= kItem && *id < kItem + static_cast<Id>(kSetupItems)) {
        const auto item = static_cast<SetupItem>(*id - kItem);
        if (editable && setup_item_open(m_setup, m_scope, item)) {
            if (m_form.clicked_with() == ui::MouseButton::Right) {
                choose_previous(m_setup, item);
            } else {
                choose_next(m_setup, item);
            }
        }
    } else if (*id == kVictory) {
        m_page = Page::Victory;
    } else if (*id == kVictoryOff && editable) {
        m_setup.rules.victory = {};
    } else if (*id == kOriginal && editable) {
        m_setup = keep_decided(EndlessSetup::original(m_scope));
    } else if (*id == kDefaults && editable) {
        m_setup = keep_decided(EndlessSetup::defaults(m_scope));
    } else if (*id == kBack) {
        return back() ? Result::None : Result::Back;
    } else if (*id == kStart && editable) {
        return Result::Start;
    }
    return Result::None;
}

EndlessSetup GameSettingsMenu::keep_decided(EndlessSetup preset) const {
    // The presets are rules; the victory conditions, the game's goal, stay as chosen.
    preset.rules.victory = m_setup.rules.victory;
    if (m_scope == SetupScope::MultiplayerEndless) {
        // The players are the lobby's, whatever the preset says.
        preset = multiplayer_setup(preset, m_setup.world.players);
    }
    return preset;
}

void GameSettingsMenu::layout() {
    ui::Form& f = m_form;
    f.clear();
    f.panel(kScreen);
    const bool victory = m_page == Page::Victory;
    f.label(kHeading, ui::tr(victory ? "setup.title.victory" : title_key(m_scope)), Align::Left, Tone::Heading);
    const bool editable = m_mode != Mode::View;
    std::int32_t left_y = kTop;
    std::int32_t right_y = kTop;
    // A group's frame and heading; the top of its first row.
    const auto group = [&](std::string_view heading, std::int32_t rows, std::int32_t x) {
        std::int32_t& y0 = x < kScreen.x + kScreen.w / 2 ? left_y : right_y;
        f.panel(Rect{x, y0, kLabelW + kValueW + 28, 44 + rows * kRowH});
        f.label(Rect{x + 12, y0 + 6, kLabelW + kValueW, 32}, ui::tr(heading), Align::Left, Tone::Heading);
        const std::int32_t first = y0 + 40;
        y0 += 44 + rows * kRowH + kGap;
        return first;
    };
    // A row's label; the place of its value.
    const auto row = [&](std::int32_t x, std::int32_t y, const std::string& label, bool used) {
        f.label(Rect{x + 12, y, kLabelW, kRowH - 4}, label, Align::Left, used ? Tone::Normal : Tone::Dim);
        return Rect{x + 16 + kLabelW, y, kValueW, kRowH - 6};
    };
    for (const Group& g : victory ? std::span<const Group>(kVictoryGroups) : std::span<const Group>(kGroups)) {
        std::vector<SetupItem> items;
        for (auto i = static_cast<std::int32_t>(g.first); i <= static_cast<std::int32_t>(g.last); ++i) {
            if (setup_item_offered(m_scope, static_cast<SetupItem>(i))) {
                items.push_back(static_cast<SetupItem>(i));
            }
        }
        if (items.empty()) {
            continue;
        }
        const std::int32_t top = group(g.heading, static_cast<std::int32_t>(items.size()), g.x);
        for (std::size_t i = 0; i < items.size(); ++i) {
            const SetupItem item = items[i];
            const bool used = setup_item_used(m_setup, item);
            const Rect value = row(g.x, top + static_cast<std::int32_t>(i) * kRowH, setup_label(item), used);
            if (editable) {
                f.cycle_button(kItem + static_cast<Id>(item), value, setup_value(m_setup, item),
                               setup_item_open(m_setup, m_scope, item));
            } else {
                f.label(value, setup_value(m_setup, item), Align::Left, used ? Tone::Normal : Tone::Dim);
            }
        }
    }
    if (!victory && victory_offered(m_scope)) {
        // Below the disasters: the way to the page of the victory conditions, with what is chosen
        // (to look at too, for a game that is only shown).
        const std::int32_t top = group("setup.group.victory", 1, kLeft);
        const Rect value = row(kLeft, top, ui::tr("setup.victory"), m_setup.rules.victory.active());
        f.button(kVictory, value, ui::tr("setup.victory.open", {victory_summary(m_setup.rules.victory)}));
    }
    // The hint below the lower column; three lines of it fit above the buttons.
    const std::int32_t hint_y = std::max({kHintY, left_y, right_y});
    f.paragraph(Rect{40, hint_y, 944, kBottomY - 4 - hint_y},
                ui::tr(victory ? "setup.hint.victory" : hint_key(m_scope)), Tone::Dim);
    f.button(kBack, Rect{40, kBottomY, 180, 44}, ui::tr("setup.back"));
    if (!editable) {
        return;
    }
    if (victory) {
        f.button(kVictoryOff, Rect{520, kBottomY, 240, 44}, ui::tr("setup.victory.off"), true,
                 !m_setup.rules.victory.active());
    } else {
        f.button(kOriginal, Rect{260, kBottomY, 240, 44}, ui::tr("setup.original"), true,
                 same_setup(m_setup, keep_decided(EndlessSetup::original(m_scope)), m_scope));
        f.button(kDefaults, Rect{520, kBottomY, 240, 44}, ui::tr("setup.defaults"), true,
                 same_setup(m_setup, keep_decided(EndlessSetup::defaults(m_scope)), m_scope));
    }
    f.button(kStart, Rect{804, kBottomY, 180, 44}, ui::tr(m_mode == Mode::Apply ? "setup.apply" : "setup.start"), true,
             true);
}

}  // namespace o1602::app
