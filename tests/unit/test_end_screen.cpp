// SPDX-License-Identifier: GPL-3.0-or-later
// The end screen (ENDE.GAD): the coats of arms by colour of the original, and Open1602's strip of
// all players in the order of their points for games with more players or further colours.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../test_framework.hpp"
#include "end_screen.hpp"

#include "o1602/assets/gad.hpp"
#include "o1602/render/banner.hpp"
#include "o1602/sim/player.hpp"
#include "o1602/sim/score.hpp"
#include "o1602/ui/localization.hpp"
#include "o1602/ui/panel.hpp"

using namespace o1602;
using app::EndScreen;

namespace {

constexpr std::array<const ui::Font*, ui::kFontSlots> kNoFonts = {nullptr, nullptr, nullptr};
constexpr std::int32_t kSlot0 = 36051;
constexpr std::int32_t kSlotX = 416;  // ENDE.GAD: 416, 467, 517, 568
constexpr std::int32_t kSlotStep = 51;
constexpr std::int32_t kSlotY = 686;
constexpr std::int32_t kArmsGfx = 82;
// The arrows from TRANSFER.GAD (19 x 41, 20 x 40) sit 6 pixels beside the outer coats of arms,
// centred on them: their top left corners.
constexpr std::int32_t kLeftArrowX = kSlotX - 6 - 19;
constexpr std::int32_t kRightArrowX = kSlotX + 3 * kSlotStep + 40 + 6;
constexpr std::int32_t kArrowY = kSlotY + 26 - 20;

assets::Gadget gadget(std::int32_t id, assets::GadgetKind kind, std::int32_t x, std::int32_t y, std::int32_t w,
                      std::int32_t h) {
    assets::Gadget g;
    g.id = id;
    g.kind = kind;
    g.pos = {x, y};
    g.size = {w, h};
    return g;
}

/// The gadgets of ENDE.GAD the screen fills in, where the original has them.
assets::GadgetLayout ende() {
    assets::GadgetLayout l;
    l.gadgets.push_back(gadget(36001, assets::GadgetKind::Graphic, 61, 693, 302, 63));
    l.gadgets.push_back(gadget(36002, assets::GadgetKind::Graphic, 679, 693, 308, 57));
    l.gadgets.push_back(gadget(36004, assets::GadgetKind::TextRight, 891, 645, 0, 0));
    assets::Gadget arms = gadget(36005, assets::GadgetKind::Graphic, 492, 42, 32, 40);
    arms.block = 1;
    arms.gfx = 78;
    l.gadgets.push_back(arms);
    assets::Gadget name = gadget(36006, assets::GadgetKind::TextCentre, 512, 130, 0, 0);
    name.offset = {0, 30};
    l.gadgets.push_back(name);
    const std::int32_t base = static_cast<std::int32_t>(l.gadgets.size());
    for (std::int32_t i = 0; i < 4; ++i) {
        assets::Gadget g = gadget(kSlot0 + i, assets::GadgetKind::Graphic, kSlotX + kSlotStep * i, kSlotY, 40, 52);
        g.block = 1;
        g.gfx = kArmsGfx + i;
        g.press_offset = 4;
        g.radio = true;
        g.base = base;
        l.gadgets.push_back(g);
    }
    return l;
}

/// TRANSFER.GAD's goods arrows.
assets::GadgetLayout transfer() {
    assets::GadgetLayout l;
    assets::Gadget left = gadget(45007, assets::GadgetKind::Graphic, 5, 425, 19, 41);
    left.gfx = 457;
    left.press_offset = 2;
    assets::Gadget right = gadget(45008, assets::GadgetKind::Graphic, 236, 426, 20, 40);
    right.gfx = 458;
    right.press_offset = 2;
    l.gadgets.push_back(left);
    l.gadgets.push_back(right);
    return l;
}

/// `totals[i]` points for the i-th player with a start (slots 0-3, then 7 on), in its own colour.
sim::EndScreenSummary summary(const std::vector<std::int64_t>& totals) {
    sim::EndScreenSummary s;
    s.outcome = sim::GameOutcome::Lost;
    for (std::size_t i = 0; i < totals.size(); ++i) {
        sim::EndScreenRow r;
        r.player = sim::player_slot(i);
        r.banner = static_cast<sim::BannerId>(r.player);
        r.name = "P" + std::to_string(i);
        r.sheet.total = totals[i];
        s.rows.push_back(r);
    }
    return s;
}

/// The original's banners and those of players five on.
const render::BannerTable& banners() {
    static const render::BannerTable table = [] {
        render::BannerTable t = render::BannerTable::original();
        t.add_more_players();
        return t;
    }();
    return table;
}

EndScreen make_screen(bool arrows = true) {
    const assets::GadgetLayout t = transfer();
    EndScreen screen(ende(), arrows ? &t : nullptr);
    screen.set_banners(&banners());
    return screen;
}

void click(EndScreen& screen, std::int32_t x, std::int32_t y) {
    (void)screen.mouse(x, y, true);
    (void)screen.mouse(x, y, false);
}

void click_slot(EndScreen& screen, std::int32_t slot) {
    click(screen, kSlotX + kSlotStep * slot + 20, kSlotY + 26);
}

/// The sprites drawn at a gadget's top left (the coats of arms and arrows have no draw offset).
std::vector<std::int32_t> sprites_at(const std::vector<ui::DrawItem>& items, std::int32_t x, std::int32_t y) {
    std::vector<std::int32_t> out;
    for (const ui::DrawItem& d : items) {
        if (d.kind == ui::DrawItem::Kind::Sprite && d.x == x && d.y == y) {
            out.push_back(d.gfx);
        }
    }
    return out;
}

/// The colour a sprite at a gadget's top left is recoloured to, nothing if it is drawn as it is.
std::optional<assets::Rgba> tint_at(const std::vector<ui::DrawItem>& items, std::int32_t x, std::int32_t y) {
    for (const ui::DrawItem& d : items) {
        if (d.kind == ui::DrawItem::Kind::Sprite && d.x == x && d.y == y && d.tint) {
            return d.tint->colour;
        }
    }
    return std::nullopt;
}

bool has_text(const std::vector<ui::DrawItem>& items, std::string_view text) {
    for (const ui::DrawItem& d : items) {
        if (d.kind == ui::DrawItem::Kind::Text && d.text == text) {
            return true;
        }
    }
    return false;
}

bool arrows_drawn(const std::vector<ui::DrawItem>& items) {
    return !sprites_at(items, kLeftArrowX, kArrowY).empty() || !sprites_at(items, kRightArrowX, kArrowY).empty();
}

}  // namespace

TEST_CASE("end screen: up to four players in the original's colours keep a coat of arms per colour") {
    EndScreen screen = make_screen();
    sim::EndScreenSummary s = summary({100, 300, 200});
    screen.open(s, false);
    CHECK(!screen.strip());
    const auto items = screen.draw(kNoFonts);
    // Red, blue and yellow at their places, white has nobody; red (the local player) is chosen.
    CHECK(sprites_at(items, kSlotX, kSlotY) == std::vector<std::int32_t>{kArmsGfx + 4});
    CHECK(sprites_at(items, kSlotX + kSlotStep, kSlotY) == std::vector<std::int32_t>{kArmsGfx + 1});
    CHECK(sprites_at(items, kSlotX + 2 * kSlotStep, kSlotY) == std::vector<std::int32_t>{kArmsGfx + 2});
    CHECK(sprites_at(items, kSlotX + 3 * kSlotStep, kSlotY).empty());
    CHECK(!arrows_drawn(items));
    CHECK(!has_text(items, "Rank 1 of 3"));
    // The coat of arms of a colour shows that player's sheet; the keys go by the rows.
    click_slot(screen, 2);
    CHECK(screen.shown_row() == 2);
    screen.step(1);
    CHECK(screen.shown_row() == 0);
}

TEST_CASE("end screen: eight players get a strip in the order of their points with arrows to scroll") {
    ui::set_language(ui::kFallbackLanguage);
    EndScreen screen = make_screen();
    screen.open(summary({500, 800, 100, 700, 900, 300, 600, 200}), false);
    REQUIRE(screen.strip());
    CHECK(screen.strip_order() == (std::vector<std::size_t>{4, 1, 3, 6, 0, 5, 7, 2}));
    CHECK(screen.rank(4) == 1);
    CHECK(screen.rank(0) == 5);
    // The local player (5th) is in view: the strip scrolled to places 2-5.
    CHECK(screen.shown_row() == 0);
    CHECK(screen.strip_first() == 1);
    auto items = screen.draw(kNoFonts);
    CHECK(has_text(items, "Rank 5 of 8"));
    for (const char* place : {"#2", "#3", "#4", "#5"}) {
        CHECK(has_text(items, place));
    }
    CHECK(!has_text(items, "#1"));
    // The seventh player has no coat of arms of its own in the original: white's, in its colour.
    CHECK(sprites_at(items, kSlotX + 2 * kSlotStep, kSlotY) == std::vector<std::int32_t>{kArmsGfx + 3});
    REQUIRE(banners().get(sim::player_slot(6)) != nullptr);
    CHECK(tint_at(items, kSlotX + 2 * kSlotStep, kSlotY) == banners().get(sim::player_slot(6))->colour);
    CHECK(!tint_at(items, kSlotX + kSlotStep, kSlotY));  // yellow is drawn as it is
    // The local player's coat of arms (4th gadget) is the chosen one.
    CHECK(sprites_at(items, kSlotX + 3 * kSlotStep, kSlotY) == std::vector<std::int32_t>{kArmsGfx + 4});
    CHECK(arrows_drawn(items));

    // The arrows scroll by one place and stop at the ends.
    click(screen, kLeftArrowX + 8, kArrowY + 20);
    CHECK(screen.strip_first() == 0);
    click(screen, kLeftArrowX + 8, kArrowY + 20);
    CHECK(screen.strip_first() == 0);
    for (int i = 0; i < 10; ++i) {
        click(screen, kRightArrowX + 8, kArrowY + 20);
    }
    CHECK(screen.strip_first() == 4);
    CHECK(screen.shown_row() == 0);  // scrolling shows nobody else
    // A coat of arms shows its player's sheet: the last place is row 2.
    click_slot(screen, 3);
    CHECK(screen.shown_row() == 2);
    items = screen.draw(kNoFonts);
    CHECK(has_text(items, "Rank 8 of 8"));

    // The keys go by points and take the strip along, round the ends.
    screen.step(1);
    CHECK(screen.shown_row() == 4);
    CHECK(screen.strip_first() == 0);
    screen.step(-1);
    CHECK(screen.shown_row() == 2);
    CHECK(screen.strip_first() == 4);
}

TEST_CASE("end screen: a player in a further colour takes the strip, without arrows when all fit") {
    EndScreen screen = make_screen();
    sim::EndScreenSummary s = summary({100, 200});
    s.rows[1].banner = sim::kFirstExtraBanner;
    screen.open(s, false);
    REQUIRE(screen.strip());
    const auto items = screen.draw(kNoFonts);
    CHECK(sprites_at(items, kSlotX, kSlotY) == std::vector<std::int32_t>{kArmsGfx + 3});
    CHECK(tint_at(items, kSlotX, kSlotY) == banners().get(sim::kFirstExtraBanner)->colour);
    CHECK(sprites_at(items, kSlotX + kSlotStep, kSlotY) == std::vector<std::int32_t>{kArmsGfx + 4});
    CHECK(sprites_at(items, kSlotX + 2 * kSlotStep, kSlotY).empty());
    CHECK(!arrows_drawn(items));
}

TEST_CASE("end screen: players with equal points share a rank") {
    EndScreen screen = make_screen();
    screen.open(summary({300, 500, 300, 100, 500}), false);
    CHECK(screen.rank(1) == 1);
    CHECK(screen.rank(4) == 1);
    CHECK(screen.rank(0) == 3);
    CHECK(screen.rank(2) == 3);
    CHECK(screen.rank(3) == 5);
    // Among equals the strip keeps the order of the rows (the local player first).
    CHECK(screen.strip_order() == (std::vector<std::size_t>{1, 4, 0, 2, 3}));
}

TEST_CASE("end screen: the view during the game shows the own sheet only, as the original") {
    ui::set_language(ui::kFallbackLanguage);
    EndScreen screen = make_screen();
    screen.open(summary({500, 800, 100, 700, 900}), true);
    const auto items = screen.draw(kNoFonts);
    for (std::int32_t i = 0; i < 4; ++i) {
        CHECK(sprites_at(items, kSlotX + kSlotStep * i, kSlotY).empty());
    }
    CHECK(!arrows_drawn(items));
    CHECK(!has_text(items, "Rank 3 of 5"));
    screen.step(1);
    CHECK(screen.shown_row() == 0);
}

TEST_CASE("end screen: a spectator's sheets during the game show every player, the leader first") {
    // Feedback 16, V3: a spectator, or anyone in a multiplayer game, chooses every player's sheet
    // while the game goes on; a spectator has none of its own and starts with the leader.
    ui::set_language(ui::kFallbackLanguage);
    EndScreen screen = make_screen();
    sim::EndScreenSummary s = summary({500, 800, 100, 700, 900});
    s.local = sim::kNoPlayer;
    screen.open(s, true, true);
    REQUIRE(screen.strip());
    CHECK(screen.choosing());
    CHECK(screen.shown_row() == 4);  // 900 points
    const auto items = screen.draw(kNoFonts);
    CHECK(has_text(items, "Rank 1 of 5"));
    CHECK(!sprites_at(items, kSlotX, kSlotY).empty());
    screen.step(1);
    CHECK(screen.shown_row() == 1);  // 800, the next by points
    // A player in a multiplayer game starts with their own sheet.
    sim::EndScreenSummary own = summary({500, 800, 100, 700, 900});
    own.local = own.rows[0].player;
    screen.open(own, true, true);
    CHECK(screen.shown_row() == 0);
    CHECK(screen.choosing());
}

TEST_CASE("end screen: new figures keep the player shown and the strip where it was") {
    EndScreen screen = make_screen();
    sim::EndScreenSummary s = summary({500, 800, 100, 700, 900, 300});
    s.local = sim::kNoPlayer;
    screen.open(s, true, true);
    screen.step(1);
    REQUIRE(screen.shown_row() == 1);  // 800
    // Player 1 falls behind everyone, player 2 leads: still player 1's sheet, at its new rank.
    sim::EndScreenSummary later = summary({500, 50, 1000, 700, 900, 300});
    later.local = sim::kNoPlayer;
    screen.refresh(later);
    CHECK(screen.shown_row() == 1);
    CHECK(screen.rank(1) == 6);
    CHECK(screen.rank(2) == 1);
    CHECK(screen.choosing());
}

TEST_CASE("end screen: without TRANSFER.GAD the strip still pages with the keys") {
    EndScreen screen = make_screen(false);
    screen.open(summary({1, 2, 3, 4, 5, 6}), false);
    REQUIRE(screen.strip());
    CHECK(!arrows_drawn(screen.draw(kNoFonts)));
    CHECK(screen.strip_first() == 2);  // the local player has the fewest points
    screen.step(1);                    // round to the first place
    CHECK(screen.shown_row() == 5);
    CHECK(screen.strip_first() == 0);
}

TEST_CASE("end screen: the ranks are in the installation's language") {
    ui::set_language("de");
    EndScreen screen = make_screen();
    screen.open(summary({1, 2, 3, 4, 5}), false);
    const auto items = screen.draw(kNoFonts);
    CHECK(has_text(items, "Platz 5 von 5"));
    CHECK(has_text(items, "5."));
    ui::set_language(ui::kFallbackLanguage);
}
