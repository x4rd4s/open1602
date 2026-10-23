// SPDX-License-Identifier: GPL-3.0-or-later
// The chunk container and every record reader of the scenario and savegame formats (.SCP island
// templates, .SZS/.SZM scenarios, .GAM savegames): islands (INSEL5/INSELHAUS) and their maps,
// missions, traders, players, ships, the world plan, the random table, the scenario header, the
// game state of a savegame (cities, warehouses, production, ... TIMERS) and the map size.
#include <cstdint>
#include <string>
#include <vector>

#include "fuzz_support.hpp"

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/island.hpp"
#include "o1602/assets/savegame.hpp"
#include "o1602/assets/scenario.hpp"
#include "o1602/sim/island_map.hpp"
#include "o1602/sim/savegame_extension.hpp"
#include "o1602/sim/world_generator.hpp"

using namespace o1602;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    auto file = assets::ChunkFile::from_bytes(core::Bytes(data, data + size));
    if (!file) {
        return 0;
    }
    for (const auto& chunk : file->chunks()) {
        fuzz::require(chunk.offset <= size && chunk.size <= size - chunk.offset, "chunk inside the file");
    }
    if (auto islands = assets::load_islands(*file)) {
        std::vector<std::string> warnings;
        const assets::Island* base = islands->empty() ? nullptr : &islands->front();
        for (const auto& island : *islands) {
            (void)island.header.template_path();
            // The map of each island, once on its own and once on the first as its template.
            (void)sim::IslandMap::from_island(island, fuzz::buildings(), &warnings);
            (void)sim::IslandMap::from_island(island, fuzz::buildings(), &warnings, base);
        }
    }
    (void)assets::load_missions(*file);
    (void)assets::load_traders(*file);
    (void)assets::load_players(*file);
    (void)assets::load_ships(*file);
    (void)assets::load_scenario_plan(*file);
    (void)assets::load_random_table(*file);
    (void)assets::load_mission_number(*file);
    (void)assets::load_scenario_header(*file);
    (void)assets::load_game_state(*file);
    (void)sim::map_size(*file);
    (void)sim::plans_islands(*file);
    return 0;
}
