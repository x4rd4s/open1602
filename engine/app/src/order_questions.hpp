// SPDX-License-Identifier: GPL-3.0-or-later
//
// The questions before orders that cannot be taken back (scuttling a ship, disbanding a unit,
// breaking a peace treaty, an attack that means war). Without SDL, so that the tests have them.
#pragma once

#include <optional>
#include <string>

#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/target.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::app {

/// The player an order's target belongs to (kNoPlayer: nobody's, or the target is gone).
[[nodiscard]] sim::PlayerId target_owner(const sim::World& world, const sim::Target& target);

/// The question `local` is asked before `command` is given, in the current language
/// (Windows-1252, as the fonts of the game have it), or none.
[[nodiscard]] std::optional<std::string> order_question(const sim::World& world, const assets::FigureTable& figures,
                                                        sim::PlayerId local, const sim::Command& command);

}  // namespace o1602::app
