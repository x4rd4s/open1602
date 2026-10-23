// SPDX-License-Identifier: GPL-3.0-or-later
// The .cod parser and the two tables built from it: haeuser.cod -> BuildingTable, figuren.cod ->
// FigureTable. The first byte is the mode: bit 0 picks the predefined symbols and the table (0
// haeuser, 1 figuren), bit 1 says the rest is the encoded file as the game ships it (decode_cod
// first), else it is decoded text. So the original files are seeds as they are: "\x02" + haeuser.cod,
// "\x03" + figuren.cod.
#include <cstdint>
#include <string>

#include "fuzz_support.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/cod_predefined.hpp"
#include "o1602/assets/figure_table.hpp"

using namespace o1602;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < 1) {
        return 0;
    }
    const std::uint8_t mode = data[0];
    const std::span<const std::uint8_t> rest(data + 1, size - 1);
    std::string decoded;
    std::string_view text = fuzz::as_text(rest);
    if ((mode & 2U) != 0) {
        decoded = assets::decode_cod(rest);
        text = decoded;
    }
    if ((mode & 1U) == 0) {
        const auto doc = assets::cod::parse(text, assets::cod_predefined_haeuser());
        auto table = assets::BuildingTable::from_cod(doc);
        if (table) {
            // Every type the table holds can be looked up by its id.
            for (const auto& type : table->types()) {
                fuzz::require(table->by_id(type.id) != nullptr, "building of the table looked up by its id");
            }
        }
    } else {
        const auto doc = assets::cod::parse(text, assets::cod_predefined_figuren());
        (void)assets::FigureTable::from_cod(doc);
    }
    return 0;
}
