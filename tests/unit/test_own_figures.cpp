// SPDX-License-Identifier: GPL-3.0-or-later
// Figures a package adds: own_figures.toml, the numbers the figure table gives them, the buildings
// that name them as their workers, their values from figures.toml and the savegame section OWNF
// (engine/assets/include/o1602/assets/own_figures.hpp, docs/modding/own-figures.md).

#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/figure_looks.hpp"
#include "o1602/assets/own_buildings.hpp"
#include "o1602/assets/own_figures.hpp"
#include "o1602/sim/balance.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

/// Swordsmen in the four colours (1 to 4) and the carrier (80), who walks with eight directions.
assets::FigureTable make_figures() {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 1\n  Kind: 1\n  Gfx: 10\n  Blocknr: 1\n"
        "  Nummer: 2\n  Kind: 1\n  Gfx: 11\n  Blocknr: 1\n"
        "  Nummer: 3\n  Kind: 1\n  Gfx: 12\n  Blocknr: 1\n"
        "  Nummer: 4\n  Kind: 1\n  Gfx: 13\n  Blocknr: 1\n"
        "  Nummer: 80\n  Kind: 3\n  Gfx: 100\n  Blocknr: 3\n  Rotate: 2\n  Schatten: 30\n  Speed: 30\n"
        "  Nummer: 30\n  Gfx: 5\n  Blocknr: 8\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A bakery of the original, whose carrier a package's bakery replaces.
assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20503\n  Kind: 14\n  Gfx: 10\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Ware: 14\n    Figurnr: 80\n    Figuranz: 1\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

assets::PackageImage image(std::string id, std::uint32_t first, std::uint32_t count) {
    assets::PackageImage i;
    i.id = std::move(id);
    i.first = first;
    i.count = count;
    i.width = 32;
    i.height = 32;
    return i;
}

}  // namespace

TEST_CASE("own figures: a package adds figures that copy one of the original") {
    auto figures = make_figures();
    std::vector<assets::OwnFigure> own;
    std::vector<std::string> warnings;
    const std::vector<assets::PackageImage> images = {image("monks:monk", 40000, 15)};
    assets::apply_own_figures_file(figures, own, "monks",
                                   "[monk]\n"
                                   "like = \"anno1602:carrier\"\n"
                                   "image = \"monks:monk\"\n"
                                   "shadow = false\n"
                                   "[guard]\n"
                                   "like = \"anno1602:2\"\n"
                                   "[brother]\n"
                                   "like = \"monks:monk\"\n",
                                   warnings, images);
    CHECK(warnings.empty());
    REQUIRE(own.size() == 3);
    CHECK(own[0].id == "monks:monk");
    // The numbers are the highest free ones, in the order the file names them.
    CHECK(own[0].number == assets::kMaxFigures - 1);
    CHECK(own[1].number == assets::kMaxFigures - 2);
    CHECK(own[2].number == assets::kMaxFigures - 3);
    CHECK(assets::find_own_figure(own, "monks:guard") == &own[1]);
    CHECK(assets::find_own_figure(own, "monks:nobody") == nullptr);

    const assets::FigureType* monk = figures.get(own[0].number);
    REQUIRE(monk != nullptr);
    CHECK(monk->number == own[0].number);
    CHECK(monk->speed == figures.get(80)->speed);  // the carrier's values
    CHECK(monk->sheet == assets::FigureSheet::Stadtfld);
    CHECK(monk->gfx == 40000);
    CHECK(assets::draws_package_image(*monk));
    CHECK(monk->shadow == 0);
    CHECK(figures.get(80)->gfx == 100);  // the original's stays as it is
    const assets::FigureType* guard = figures.get(own[1].number);
    REQUIRE(guard != nullptr);
    CHECK(guard->gfx == 11);                          // without an image the picture of the one it copies
    CHECK(figures.get(own[2].number)->gfx == 40000);  // a copy of another package's figure

    // What names one figure: the original's by name or number, a package's by its id.
    CHECK(assets::figure_number(own, figures, "anno1602:carrier") == 80U);
    CHECK(assets::figure_number(own, figures, "anno1602:0x50") == 80U);
    CHECK(assets::figure_number(own, figures, "monks:monk") == own[0].number);
    CHECK(!assets::figure_number(own, figures, "anno1602:swordsman"));  // four colours
    CHECK(!assets::figure_number(own, figures, "anno1602:500"));        // no such figure
    CHECK(!assets::figure_number(own, figures, "monks:abbot"));
}

TEST_CASE("own figures: a table that is wrong is passed over with a warning") {
    auto figures = make_figures();
    std::vector<assets::OwnFigure> own;
    std::vector<std::string> warnings;
    const std::vector<assets::PackageImage> images = {image("monks:small", 40000, 4)};
    assets::apply_own_figures_file(figures, own, "monks",
                                   "[nothing]\n"
                                   "image = \"monks:small\"\n"
                                   "[unit]\n"
                                   "like = \"anno1602:swordsman\"\n"
                                   "[tiny]\n"
                                   "like = \"anno1602:carrier\"\n"
                                   "image = \"monks:small\"\n"
                                   "[unknown]\n"
                                   "like = \"anno1602:carrier\"\n"
                                   "image = \"monks:none\"\n"
                                   "[Bad-Name]\n"
                                   "like = \"anno1602:carrier\"\n"
                                   "[ok]\n"
                                   "like = \"anno1602:carrier\"\n"
                                   "colour = 3\n",
                                   warnings, images);
    // Only "ok" is added; its unknown field is passed over.
    REQUIRE(own.size() == 1);
    CHECK(own[0].id == "monks:ok");
    CHECK(warnings.size() == 6);
}

TEST_CASE("own figures: a package's building names its workers, figures.toml their values") {
    auto figures = make_figures();
    auto buildings = make_buildings();
    std::vector<assets::OwnFigure> own_figures;
    std::vector<std::string> warnings;
    assets::apply_own_figures_file(figures, own_figures, "monks", "[monk]\nlike = \"anno1602:carrier\"\n", warnings);
    std::vector<assets::OwnBuilding> own;
    assets::apply_own_buildings_file(buildings, own, "monks",
                                     "[abbey]\n"
                                     "like = \"anno1602:20503\"\n"
                                     "figure = \"monks:monk\"\n"
                                     "[mill]\n"
                                     "like = \"anno1602:20503\"\n"
                                     "figure = \"anno1602:cart\"\n",
                                     warnings, {}, {}, {}, {}, &figures, own_figures);
    // The cart is not in this table: the mill is passed over.
    CHECK(warnings.size() == 1);
    REQUIRE(own.size() == 1);
    CHECK(buildings.by_id(own[0].number)->worker_figure == own_figures[0].number);

    sim::Balance balance;
    warnings.clear();
    sim::apply_figures_file(balance, "monks", "[\"monks:monk\"]\nspeed = 60\n", warnings, own_figures);
    CHECK(warnings.empty());
    REQUIRE(balance.figures.size() == 1);
    CHECK(balance.figures[0].number == own_figures[0].number);
    const auto original = figures;
    sim::apply_figure_values(figures, original, balance);
    CHECK(figures.get(own_figures[0].number)->speed == 60);
    CHECK(figures.get(80)->speed == original.get(80)->speed);
}

TEST_CASE("own figures: a game keeps the numbers of the figures its packages added") {
    auto figures = make_figures();
    const auto buildings = make_buildings();
    std::vector<assets::OwnFigure> own;
    std::vector<std::string> warnings;
    assets::apply_own_figures_file(figures, own, "monks", "[monk]\nlike = \"anno1602:carrier\"\n", warnings);
    sim::World world(60, 40);
    world.set_own_figures(own);
    const auto bytes = sim::write_game(world, buildings);
    REQUIRE(bytes.ok());
    auto read = sim::read_game(*bytes, buildings, nullptr, &figures);
    REQUIRE(read.ok());
    REQUIRE(read->own_figures().size() == 1);
    CHECK(read->own_figures()[0] == own[0]);

    // Without that figure the game does not load: its buildings' carriers would name nothing.
    const auto plain = make_figures();
    CHECK(!sim::read_game(*bytes, buildings, nullptr, &plain).ok());
}
