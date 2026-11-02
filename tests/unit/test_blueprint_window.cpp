// SPDX-License-Identifier: GPL-3.0-or-later
// The window of a blueprint (Open1602): what it shows and what its buttons ask for.

#include <array>
#include <string>

#include "../test_framework.hpp"
#include "blueprint_window.hpp"

using namespace o1602;
using app::BlueprintModel;
using app::BlueprintWindow;

namespace {

constexpr std::array<const ui::Font*, ui::kFontSlots> kNoFonts = {nullptr, nullptr, nullptr};

BlueprintModel model() {
    BlueprintModel m;
    m.valid = true;
    m.name = "Schmied";
    m.city = "Abemsberg";
    m.cost.money = 150;
    m.cost.wood = 3 * 32;
    m.cost.bricks = 2 * 32 + 1;  // a part of a ton counts as a whole one
    m.city_count = 4;
    m.city_cost.money = 400;
    return m;
}

bool shows(const std::vector<ui::DrawItem>& items, const std::string& text) {
    for (const ui::DrawItem& d : items) {
        if (d.kind == ui::DrawItem::Kind::Text && d.text.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

/// Presses and lets go at a point of the window (design units, scale 1).
BlueprintWindow::Result click(BlueprintWindow& w, const BlueprintModel& m, std::int32_t x, std::int32_t y) {
    (void)w.mouse(x, y, true, m);
    return w.mouse(x, y, false, m);
}

}  // namespace

TEST_CASE("blueprint window: shows the building, its costs and those of its city") {
    BlueprintWindow w;
    const BlueprintModel m = model();
    const auto items = w.draw(m, kNoFonts);
    CHECK(shows(items, "Schmied"));
    CHECK(BlueprintWindow::bill_text(m.cost) == "150 gold, 3 t wood, 3 t bricks");
    CHECK(BlueprintWindow::bill_text(m.city_cost) == "400 gold");
    CHECK(shows(items, "All 4 in Abemsberg"));
    CHECK(shows(items, "400 gold"));
}

TEST_CASE("blueprint window: its buttons build one, build the city's, switch the city's builder and remove") {
    BlueprintWindow w;
    const BlueprintModel m = model();
    (void)w.draw(m, kNoFonts);
    CHECK(click(w, m, 100, 120) == BlueprintWindow::Result::Build);
    CHECK(click(w, m, 100, 245) == BlueprintWindow::Result::BuildCity);
    CHECK(click(w, m, 100, 288) == BlueprintWindow::Result::Automatic);
    CHECK(click(w, m, 100, BlueprintWindow::kHeight - 35) == BlueprintWindow::Result::Remove);
    CHECK(click(w, m, BlueprintWindow::kWidth - 80, 20) == BlueprintWindow::Result::Close);
    CHECK(click(w, m, 100, 60) == BlueprintWindow::Result::None);
}
