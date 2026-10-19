// SPDX-License-Identifier: GPL-3.0-or-later
// What the mouse and the keys mean on the map before anything is ordered
// (engine/app/src/map_gestures.hpp).

#include <vector>

#include "../test_framework.hpp"
#include "map_gestures.hpp"

using namespace o1602;

TEST_CASE("map gestures: a left press becomes a selection rectangle after 5 pixels") {
    app::LeftPress press;
    CHECK(!press.at());
    press.move(50.0, 50.0);  // not pressed on the map: nothing
    CHECK(!press.rubber_band());

    press.press(100.0, 100.0);
    REQUIRE(press.at().has_value());
    press.move(103.0, 103.0);  // 4.2 px
    CHECK(!press.rubber_band());
    press.move(103.0, 104.0);  // 5 px
    CHECK(press.rubber_band());
    press.move(100.0, 100.0);  // back: it stays a rectangle
    CHECK(press.rubber_band());

    press.reset();
    CHECK(!press.at());
    CHECK(!press.rubber_band());
}

TEST_CASE("map gestures: the right button drags the map, almost in place it is a click") {
    app::MapDrag drag;
    CHECK(!drag.move(10.0, 10.0));  // no button: the map stays
    drag.press();
    CHECK(drag.move(1.0, -2.0));
    CHECK(drag.release());  // 3 px: a click
    CHECK(!drag.dragging());

    drag.press();
    CHECK(drag.move(3.0, 0.0));
    CHECK(drag.move(0.0, -1.0));
    CHECK(!drag.release());  // 4 px: a drag
    // Pressed on the panel, released on the map: judged by the last drag.
    CHECK(!drag.release());
}

TEST_CASE("map gestures: a group key twice within half a second") {
    app::GroupKey key;
    CHECK(!key.press(1, 1000));
    CHECK(key.press(1, 1400));
    CHECK(!key.press(2, 1500));  // another group
    CHECK(!key.press(2, 2100));  // too late
    key.reset();
    CHECK(!key.press(2, 2200));
}

TEST_CASE("map gestures: the next idle unit after the one selected") {
    using Ref = ui::UnitRef;
    const std::vector<Ref> idle = {{Ref::Kind::Ship, 4}, {Ref::Kind::Ship, 7}, {Ref::Kind::Ship, 9}};
    CHECK(!app::next_idle({}, std::nullopt).has_value());
    CHECK(app::next_idle(idle, std::nullopt) == std::optional<std::size_t>(0));
    CHECK(app::next_idle(idle, Ref{Ref::Kind::Ship, 7}) == std::optional<std::size_t>(2));
    CHECK(app::next_idle(idle, Ref{Ref::Kind::Ship, 9}) == std::optional<std::size_t>(0));  // round again
    CHECK(app::next_idle(idle, Ref{Ref::Kind::Ship, 5}) == std::optional<std::size_t>(0));  // a busy one
}
