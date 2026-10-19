// SPDX-License-Identifier: GPL-3.0-or-later
//
// The original's game numbers in the simulation's namespace (review point 27). They live in
// assets/game_data.hpp, where the loaders can use them too.
#pragma once

#include "o1602/assets/game_data.hpp"

namespace o1602::sim {

namespace ware = assets::ware;
namespace kind = assets::kind;
namespace prod = assets::prod;
namespace figure = assets::figure;
namespace id = assets::id;
using assets::base_price;
using assets::is_soldier_ware;
using assets::is_trade_ware;
using assets::kBasePrices;

}  // namespace o1602::sim
