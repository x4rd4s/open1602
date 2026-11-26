// SPDX-License-Identifier: GPL-3.0-or-later
// The computer players' data sheet of the economy (ai_sheet.hpp, P2.1): every figure the
// building table's, by the simulation's own rules, for the original's classes and producers and
// for those a package adds.

#include <cstdint>
#include <vector>

#include "../../engine/sim/src/ai/ai_sheet.hpp"
#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/sim/consumption.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;
using o1602::sim::ai::Sheet;

namespace {

/// Three classes of haeuser.cod's kind and a fourth as a package adds one (another Maxwohn, a good
/// past the original's, a good listed twice, as the table keeps it); a workshop from a raw material, a farm from two, a
/// package's producer with an id of its own, and a building without a cycle.
assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}, {"BGRUPPE", 5}, {"BGRUPPE_WARE", 6}};
    const auto doc = assets::cod::parse(
        "Nahrung: 1.3\n"
        "Objekt: BGRUPPE\n"
        "  Nummer: 0\n  Prozent: 0\n  Maxwohn: 2\n  Steuer: 1.4\n"
        "  Nummer: 1\n  Prozent: 47\n  Maxwohn: 6\n  Steuer: 1.6\n"
        "  Objekt: BGRUPPE_WARE\n    Ware: 19, 0.6\n    Ware: 18, 0.5\n  EndObj\n"
        "  Nummer: 2\n  Prozent: 71\n  Maxwohn: 15\n  Steuer: 2.1\n"
        "  Objekt: BGRUPPE_WARE\n    Ware: 19, 0.7\n    Ware: 18, 0.6\n    Ware: 15, 0.5\n    Ware: 16, 0.5\n"
        "  EndObj\n"
        "  Nummer: 3\n  Prozent: 90\n  Maxwohn: 23\n  Steuer: 3.3\n"
        "  Objekt: BGRUPPE_WARE\n    Ware: 59, 0.9\n    Ware: 19, 0.4\n    Ware: 19, 0.3\n  EndObj\n"
        "EndObj\n"
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 14\n  Gfx: 0\n  Size: 2, 2\n  Objekt: HAUS_PRODTYP\n    Kind: 3\n"
        "    Ware: 19\n    Rohstoff: 6\n    Prodmenge: 1\n    Rohmenge: 2\n    Interval: 40\n    Kosten: 5, 2\n"
        "    Arbeiter: 2\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Werkzeug: 3\n    Holz: 4\n    Money: 100\n  EndObj\n"
        "  @Nummer: 1\n  Id: 20102\n  Kind: 14\n  Gfx: 1\n  Size: 2, 2\n  Objekt: HAUS_PRODTYP\n    Kind: 3\n"
        "    Ware: 18\n    Rohstoff: 7\n    Rohmenge: 3\n    Workstoff: 8\n    Workmenge: 1\n    Prodmenge: 2\n"
        "    Interval: 70\n    Kosten: 30, 12\n  EndObj\n"
        "  @Nummer: 2\n  Id: 21000\n  Kind: 14\n  Gfx: 2\n  Size: 2, 2\n  Objekt: HAUS_PRODTYP\n    Kind: 3\n"
        "    Ware: 59\n    Prodmenge: 1\n    Interval: 33\n  EndObj\n"
        "  @Nummer: 3\n  Id: 20201\n  Kind: 14\n  Gfx: 3\n  Size: 1, 1\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

}  // namespace

TEST_CASE("ai sheet: every class of the table, with its room and the goods it asks for") {
    const auto b = make_buildings();
    const Sheet sheet(b);
    REQUIRE(sheet.classes().size() == b.resident_classes().size());
    REQUIRE(sheet.classes().size() == 4);
    for (std::size_t k = 0; k < b.resident_classes().size(); ++k) {
        const assets::ResidentClass& c = b.resident_classes()[k];
        CHECK(sheet.max_per_house(k) == c.max_per_house);
        CHECK(sheet.classes()[k].share == c.share);
        CHECK(sheet.classes()[k].needs == c.needs);
    }
    CHECK(sheet.max_per_house(3) == 23);
    CHECK(sheet.max_per_house(4) == 0);  // no such class
    // Every entry of a good counts; a good of a package as any other.
    const auto listed = [&](std::size_t k, std::uint8_t w) {
        std::uint64_t sum = 0;
        for (const assets::ResidentClass::Need& n : b.resident_classes()[k].needs) {
            sum += n.ware == w ? n.amount : 0U;
        }
        return sum;
    };
    for (std::size_t k = 0; k < b.resident_classes().size(); ++k) {
        for (const std::uint8_t w :
             {std::uint8_t{15}, std::uint8_t{16}, std::uint8_t{18}, std::uint8_t{19}, std::uint8_t{59}}) {
            CHECK(sheet.need_amount(k, w) == listed(k, w));
        }
    }
    CHECK(sheet.need_amount(3, 59) != 0);
    CHECK(sheet.need_amount(3, 19) != 0);
    CHECK(sheet.need_amount(0, 19) == 0);
    CHECK(sheet.need_amount(9, 19) == 0);
    CHECK(sheet.food_ware() == b.food_ware());
    CHECK(sheet.food_need() == b.food_need());
}

TEST_CASE("ai sheet: the taxes are the simulation's, class by class and rate by rate") {
    const auto b = make_buildings();
    const Sheet sheet(b);
    sim::City city;
    city.inhabitants = {13, 77, 31, 9};
    city.tax_rate = {64, 128, 192, 100};
    std::int64_t sum = 0;
    for (std::size_t k = 0; k < city.inhabitants.size(); ++k) {
        sum += sheet.tax(k, city.inhabitants[k], city.tax_rate[k]);
    }
    CHECK(sum == sim::tax_income(city, b));
    CHECK(sum > 0);
    CHECK(sheet.tax(4, 100, 128) == 0);  // no such class
}

TEST_CASE("ai sheet: what the classes ask for is what the city asks for in a tick") {
    const auto b = make_buildings();
    const Sheet sheet(b);
    sim::City city;
    city.capacity = 30 * 32;
    city.inhabitants = {13, 77, 31, 9};
    city.tax_rate = {128, 128, 128, 128};
    city.satisfaction.assign(4, 128);
    for (const std::uint8_t w : std::vector<std::uint8_t>{14, 15, 16, 18, 19, 59}) {
        city.ensure_ware(w);
    }
    sim::consume(city, b, nullptr);
    // The demand of one tick from nothing, after its decay by 15/16.
    const auto asked = [&](std::uint8_t w) {
        std::uint64_t units = 0;
        for (std::size_t k = 0; k < city.inhabitants.size(); ++k) {
            units += std::uint64_t{city.inhabitants[k]} *
                     (w == sheet.food_ware() ? sheet.food_need() : sheet.need_amount(k, w));
        }
        return units;
    };
    for (const std::uint8_t w : std::vector<std::uint8_t>{14, 15, 16, 18, 19, 59}) {
        CHECK(city.supply[w].demand == asked(w) * 15 / 16);
    }
    // Per minute: six ticks, 256 units to the 1/32 t.
    CHECK(Sheet::per_minute(256) == 6);
    CHECK(Sheet::per_minute(255) == 5);
    CHECK(Sheet::per_minute(asked(19)) == asked(19) * 6 / 256);
}

TEST_CASE("ai sheet: every building with a cycle, at its table's rates and costs") {
    const auto b = make_buildings();
    const Sheet sheet(b);
    std::size_t cycles = 0;
    for (const assets::BuildingType& t : b.types()) {
        const Sheet::Producer* p = sheet.producer(t);
        if (t.interval == 0) {
            CHECK(p == nullptr);
            continue;
        }
        ++cycles;
        REQUIRE(p != nullptr);
        CHECK(p == sheet.producer(t.id));
        CHECK(p->type == &t);
        CHECK(p->ware == t.ware);
        CHECK(p->raw == t.raw_ware);
        CHECK(p->raw2 == t.raw2_ware);
        CHECK(p->output == std::uint64_t{t.produce_amount} * 60 / t.interval);
        CHECK(p->raw_use == std::uint64_t{t.raw_amount} * 60 / t.interval);
        CHECK(p->raw2_use == std::uint64_t{t.raw2_amount} * 60 / t.interval);
        CHECK(p->running_cost == t.running_cost);
        CHECK(p->idle_cost == t.idle_cost);
        CHECK(p->build_cost.money == t.build_cost.money);
        CHECK(p->build_cost.tools == t.build_cost.tools);
        CHECK(p->build_cost.wood == t.build_cost.wood);
        CHECK(p->workers == t.workers);
    }
    CHECK(cycles == 3);
    CHECK(sheet.producers().size() == 3);
    // The package's producer by its own id; a building without a cycle and an unknown id: none.
    REQUIRE(sheet.producer(21000) != nullptr);
    CHECK(sheet.producer(21000)->ware == 59);
    CHECK(sheet.producer(20201) == nullptr);
    CHECK(sheet.producer(29999) == nullptr);
    // The table's figures as read: one ton of cloth in 40 s from two of wool, 5 gold on, 2 off.
    const Sheet::Producer* weaver = sheet.producer(20101);
    REQUIRE(weaver != nullptr);
    CHECK(weaver->output == 32U * 60 / 40);
    CHECK(weaver->raw_use == 64U * 60 / 40);
    CHECK(weaver->running_cost == 5);
    CHECK(weaver->idle_cost == 2);
    CHECK(weaver->build_cost.money == 100);
    CHECK(weaver->workers == 2);
}
