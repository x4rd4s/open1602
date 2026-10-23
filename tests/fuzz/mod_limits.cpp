// SPDX-License-Identifier: GPL-3.0-or-later
//
// o1602_mod_limits [bomb | objfill]
//
// Measures how the loaders of modded data treat values at and beyond the limits of
// docs/modding/data-limits.md: synthetic haeuser.cod, figuren.cod and .gad files, one value
// changed at a time. Prints one line per case: what the document says and what the loader did.
// Run it in a UBSan build to see the values that overflow inside the loader. A building table that
// loads with extreme values is then used in a small running game (built, saved, loaded, validated).
//
// "bomb" also runs the inputs that need a lot of memory (a GAD Nummer of 2 * 10^8, then nested
// ObjFill), "objfill" only the nested ObjFill;
// run those under a memory limit (ulimit -v).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "fuzz_support.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/cod_predefined.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/gad.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/validate.hpp"

using namespace o1602;

namespace {

const assets::CodPredefinedSymbol kHaus[] = {{"HAUS", 1}};
const assets::CodPredefinedSymbol kFigur[] = {{"FIGUR", 1}};

/// One HAUS record (Nummer 0, Id 20101 unless `body` sets them) with `body` inside, plus a ground
/// tile so that a table that loads has something to stand on.
std::string haus(std::string_view body, std::string_view nummer = "0") {
    std::string text = "Objekt: HAUS\n  @Nummer: ";
    text += nummer;
    text += "\n";
    if (body.find("Id:") == std::string_view::npos) {
        text += "  Id: 20101\n";
    }
    text += "  Kind: 11\n";
    text += body;
    text += "\nEndObj\n";
    return text;
}

struct Outcome {
    bool ok = false;
    std::string error;
    std::size_t diagnostics = 0;
    std::optional<assets::BuildingType> first;
};

Outcome load_buildings(const std::string& text) {
    const auto doc = assets::cod::parse(text, kHaus);
    Outcome o;
    o.diagnostics = doc.diagnostics.size();
    auto table = assets::BuildingTable::from_cod(doc);
    o.ok = table.ok();
    if (!table) {
        o.error = table.error().message;
    } else if (!table->types().empty()) {
        o.first = table->types().front();
    }
    return o;
}

void row(const char* file, const char* value, const char* documented, const std::string& observed) {
    std::printf("| %s | %s | %s | %s |\n", file, value, documented, observed.c_str());
}

std::string verdict(const Outcome& o) {
    if (!o.ok) {
        return "refused (" + o.error + ")";
    }
    return "loaded, " + std::to_string(o.diagnostics) + " diagnostics";
}

void buildings_cases() {
    // Id
    for (const char* id : {"19999", "20000", "23500", "23501", "-5"}) {
        const Outcome o = load_buildings(haus(std::string("  Id: ") + id));
        row("haeuser.cod", (std::string("Id ") + id).c_str(), "20000..23500, else refused", verdict(o));
    }
    // Nummer
    for (const char* n : {"65535", "65536", "-1", "2147483647"}) {
        const Outcome o = load_buildings(haus("", n));
        std::string obs = verdict(o);
        if (o.first) {
            obs += ", record=" + std::to_string(o.first->record);
        }
        row("haeuser.cod", (std::string("Nummer ") + n).c_str(), "0..65535, else refused", obs);
    }
    // Size
    for (const char* s : {"1, 1", "255, 255", "256, 1", "0, 0", "-3, 2"}) {
        const Outcome o = load_buildings(haus(std::string("  Size: ") + s));
        std::string obs = verdict(o);
        if (o.first) {
            obs += ", size=" + std::to_string(o.first->size_x) + "x" + std::to_string(o.first->size_y);
        }
        row("haeuser.cod", (std::string("Size ") + s).c_str(), "1..255 each way, else refused", obs);
    }
    // Gfx + tiles beyond the 16-bit sprite range
    for (const char* g : {"65534", "65535", "-1"}) {
        const Outcome o = load_buildings(haus(std::string("  Gfx: ") + g + "\n  Size: 1, 1"));
        row("haeuser.cod", (std::string("Gfx ") + g).c_str(), "(not documented) sprite < 65535", verdict(o));
    }
    // Fields of HAUS_PRODTYP / HAUS
    struct Field {
        const char* line;
        const char* documented;
        std::string (*show)(const assets::BuildingType&);
    };
    const Field fields[] = {
        {"  Objekt: HAUS_PRODTYP\n    Arbeiter: 70000\n  EndObj", "65535: clamped",
         [](const assets::BuildingType& b) { return "workers=" + std::to_string(b.workers); }},
        {"  Objekt: HAUS_PRODTYP\n    Maxlager: 200000000\n  EndObj", "range of the field: clamped",
         [](const assets::BuildingType& b) { return "max_stock=" + std::to_string(b.max_stock); }},
        {"  Objekt: HAUS_PRODTYP\n    Ware: 300\n  EndObj", "range of the field: clamped",
         [](const assets::BuildingType& b) { return "ware=" + std::to_string(b.ware); }},
        {"  Objekt: HAUS_PRODTYP\n    Rohstoff: 257\n  EndObj", "range of the field: clamped",
         [](const assets::BuildingType& b) { return "raw_ware=" + std::to_string(b.raw_ware); }},
        {"  Objekt: HAUS_PRODTYP\n    Figuranz: 256\n  EndObj", "range of the field: clamped",
         [](const assets::BuildingType& b) { return "worker_count=" + std::to_string(b.worker_count); }},
        {"  Objekt: HAUS_PRODTYP\n    Figurnr: 65536\n  EndObj", "range of the field: clamped",
         [](const assets::BuildingType& b) { return "worker_figure=" + std::to_string(b.worker_figure); }},
        {"  Objekt: HAUS_PRODTYP\n    BGruppe: 256\n  EndObj", "range of the field: clamped",
         [](const assets::BuildingType& b) { return "resident_class=" + std::to_string(b.resident_class); }},
        {"  Objekt: HAUS_PRODTYP\n    Erzbergnr: 300\n  EndObj", "range of the field: clamped",
         [](const assets::BuildingType& b) { return "ore_mountain=" + std::to_string(b.ore_mountain); }},
        {"  Objekt: HAUS_PRODTYP\n    Prodmenge: 999999999999.0\n  EndObj", "range of the field: clamped",
         [](const assets::BuildingType& b) { return "produce_amount=" + std::to_string(b.produce_amount); }},
        {"  Objekt: HAUS_PRODTYP\n    Interval: 2147483647\n  EndObj", "range of the field: clamped",
         [](const assets::BuildingType& b) { return "interval=" + std::to_string(b.interval); }},
        {"  Ruinenr: 300", "range of the field: clamped",
         [](const assets::BuildingType& b) { return "ruin=" + std::to_string(b.ruin); }},
        {"  RandAnz: -1", "range of the field: clamped",
         [](const assets::BuildingType& b) { return "rand_count=" + std::to_string(b.rand_count); }},
        {"  RandAnz: 70000", "range of the field: clamped",
         [](const assets::BuildingType& b) { return "rand_count=" + std::to_string(b.rand_count); }},
        {"  Wegspeed: 100000000, 100, 100, 100", "range of the field: clamped",
         [](const assets::BuildingType& b) { return "walk_speed[0]=" + std::to_string(b.walk_speed[0]); }},
        {"  Randwachs: 20000000", "range of the field: clamped",
         [](const assets::BuildingType& b) { return "edge_growth=" + std::to_string(b.edge_growth); }},
        {"  Maxenergy: 999999999999.0", "range of the field: clamped (saturating_cast)",
         [](const assets::BuildingType& b) { return "max_health=" + std::to_string(b.max_health); }},
        {"  Rotate: 4000000000", "number beyond int32: clamped",
         [](const assets::BuildingType& b) { return "rotate=" + std::to_string(b.rotate); }},
        {"  AnimAnz: 2147483647\n  AnimAdd: 2147483647", "(not documented)",
         [](const assets::BuildingType& b) {
             return "anim_count=" + std::to_string(b.anim_count) + " anim_add=" + std::to_string(b.anim_add);
         }},
        {"  Objekt: HAUS_BAUKOST\n    Money: -1000\n  EndObj", "(not documented)",
         [](const assets::BuildingType& b) { return "build_cost.money=" + std::to_string(b.build_cost.money); }},
        {"  Objekt: HAUS_PRODTYP\n    Kosten: -5, -7\n  EndObj", "(not documented)",
         [](const assets::BuildingType& b) {
             return "running_cost=" + std::to_string(b.running_cost) + " idle=" + std::to_string(b.idle_cost);
         }},
    };
    for (const Field& f : fields) {
        const Outcome o = load_buildings(haus(f.line));
        std::string value(f.line);
        for (char& c : value) {
            if (c == '\n') {
                c = ' ';
            }
        }
        row("haeuser.cod", value.c_str(), f.documented, o.first ? f.show(*o.first) : verdict(o));
    }
    // ObjFill
    {
        const auto doc = assets::cod::parse(haus("  ObjFill: 0, 20000"), kHaus);
        const auto* records = doc.root.records("HAUS");
        row("haeuser.cod", "ObjFill 0, 20000", "10000 copies: clamped, with a warning",
            std::to_string(records != nullptr ? records->size() : 0) + " records, " +
                std::to_string(doc.diagnostics.size()) + " diagnostics");
    }
    // A decimal beyond int32
    {
        const auto doc = assets::cod::parse("Wert = 99999999999999999999.5\nNeg = -99999999999999999999.5\n");
        std::string obs;
        for (const char* name : {"Wert", "Neg"}) {
            const auto it = doc.symbols.all().find(name);
            obs +=
                std::string(name) + "=" +
                (it != doc.symbols.all().end() && it->second.count >= 1 ? std::to_string(it->second.values[0].integer)
                                                                        : std::string("?")) +
                " ";
        }
        row("both", "+-99999999999999999999.5", "range of int32: clamped", obs);
    }
}

void figures_cases() {
    for (const char* n : {"0", "1023", "1024", "-1"}) {
        const std::string text = std::string("Objekt: FIGUR\n  Nummer: ") + n + "\n  Speed: 100\nEndObj\n";
        auto t = assets::FigureTable::from_cod(assets::cod::parse(text, kFigur));
        row("figuren.cod", (std::string("Nummer ") + n).c_str(), "0..1023, else refused",
            t.ok() ? "loaded" : "refused (" + t.error().message + ")");
    }
    struct Field {
        const char* line;
        const char* documented;
    };
    for (const Field& f : {Field{"  Maxtrag: 200000000", "range of the field: clamped"},
                           Field{"  Maxware: 1000", "range of the field: clamped"},
                           Field{"  Worktime: 999999999999.0", "range of the field: clamped"},
                           Field{"  Shotradius: 999999999999.0", "range of the field: clamped"},
                           Field{"  Speed: -10", "(not documented)"}, Field{"  Rotate: 0", "(not documented)"}}) {
        const std::string text = std::string("Objekt: FIGUR\n  Nummer: 5\n") + f.line + "\nEndObj\n";
        auto t = assets::FigureTable::from_cod(assets::cod::parse(text, kFigur));
        std::string obs = t.ok() ? "loaded" : "refused";
        if (t.ok()) {
            const auto* fig = t->get(5);
            if (fig != nullptr) {
                obs += ": max_carry=" + std::to_string(fig->max_carry) +
                       " cargo_slots=" + std::to_string(fig->ship.cargo_slots) +
                       " work_time_ms=" + std::to_string(fig->work_time_ms) +
                       " range8=" + std::to_string(fig->combat.range8) + " speed=" + std::to_string(fig->speed) +
                       " rotate=" + std::to_string(fig->rotate);
            }
        }
        row("figuren.cod", f.line + 2, f.documented, obs);
    }
}

std::string gad_line(const assets::GadgetLayout& l) {
    return std::to_string(l.gadgets.size()) + " gadgets, " + std::to_string(l.warnings.size()) + " warnings";
}

void gad_cases(bool bomb) {
    // Include depth: each level includes the next.
    {
        const auto l = assets::parse_gad(
            "Include: \"A\"\nObjekt: GADGET\n  @Nummer: 0\n  Id: 1\nEndObj\n",
            [](std::string_view) -> std::optional<std::string> { return std::string("Include: \"A\"\nX = 1\n"); });
        row(".gad", "Include 5+ levels", "4 levels, rest left out with a warning", gad_line(l));
    }
    {
        std::string big(300'000, ';');
        big += "\n";
        std::string text;
        for (int i = 0; i < 8; ++i) {
            text += "Include: \"BIG\"\n";
        }
        const auto l = assets::parse_gad(text, [&](std::string_view) -> std::optional<std::string> { return big; });
        row(".gad", "Include 8 x 300 KB", "1 MiB in all, rest left out with a warning", gad_line(l));
    }
    for (const char* n : {"299", "300", "5000"}) {
        const auto l = assets::parse_gad(std::string("Objekt: GADGET\n  @Nummer: ") + n + "\n  Id: 1\nEndObj\n");
        row(".gad", (std::string("Nummer ") + n).c_str(), "(not documented; original MAXGADGET 300)", gad_line(l));
    }
    {
        const auto l = assets::parse_gad(
            "Objekt: GADGET\n  @Nummer: 0\n  Id: 1\n  Pos: 2147483647, -2147483648\n  Size: -5, 99999999\n"
            "  Posoffs: 100000, -100000\n  Slidhorflg: 1\nEndObj\n");
        const auto& g = l.gadgets.front();
        row(".gad", "Pos/Size/Posoffs extreme", "(not documented)",
            "pos=" + std::to_string(g.pos[0]) + "," + std::to_string(g.pos[1]) + " size=" + std::to_string(g.size[0]) +
                "," + std::to_string(g.size[1]) + " offset=" + std::to_string(g.offset[0]) + "," +
                std::to_string(g.offset[1]) + " slider_size=" + std::to_string(g.slider_size[0]));
    }
    if (bomb) {
        std::fprintf(stderr, "bomb: GAD Nummer 200000000 ...\n");
        const auto l = assets::parse_gad("Objekt: GADGET\n  @Nummer: 200000000\n  Id: 1\nEndObj\n");
        row(".gad", "Nummer 200000000", "(not documented)", gad_line(l));
    }
}

void objfill_bomb() {
    // ObjFill copies a whole record with its children: copies of copies multiply past the clamp.
    const std::string text =
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n"
        "  Objekt: HAUS_PRODTYP\n    @Nummer: 0\n    Kind: 1\n    ObjFill: 0, 10000\n  EndObj\n"
        "  @Nummer: 1\n  ObjFill: 0, 10000\n"
        "EndObj\n";
    std::fprintf(stderr, "bomb: nested ObjFill (%zu bytes of text) ...\n", text.size());
    const auto doc = assets::cod::parse(text, kHaus);
    std::size_t children = 0;
    for (const auto& [n, r] : *doc.root.records("HAUS")) {
        if (const auto* p = r.records("HAUS_PRODTYP")) {
            children += p->size();
        }
    }
    row("haeuser.cod", "nested ObjFill 10000 x 10000", "ObjFill: 10000 copies", std::to_string(children) + " records");
}

/// A table with buildings at the limits, used in a running game: built by orders, saved, loaded.
void game_with_extremes() {
    const std::string text =
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "  @Nummer: 1\n  Id: 20401\n  Kind: 1\n  Gfx: 1\n  Size: 1, 1\n  Wegspeed: 1, 1, 1, 1\n"
        "  @Nummer: 2\n  Id: 23500\n  Kind: 14\n  Gfx: 10\n  Size: 255, 255\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 3\n    Ware: 255\n    Rohstoff: 254\n    Maxlager: 134217727\n"
        "    Interval: 1\n    Prodmenge: 1000000\n    Arbeiter: 65535\n    Figuranz: 255\n    Figurnr: 65535\n"
        "  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 0\n  EndObj\n"
        "  @Nummer: 65535\n  Id: 20000\n  Kind: 14\n  Gfx: 65000\n  Size: 2, 2\n  RandAnz: 65535\n  RandAdd: 65535\n"
        "EndObj\n";
    auto table = assets::BuildingTable::from_cod(assets::cod::parse(text, kHaus));
    if (!table) {
        row("game", "extreme table", "loads", "refused: " + table.error().message);
        return;
    }
    // Island 0: the 255 x 255 producer covers it whole; island 1: the building of Nummer 65535.
    sim::World world(700, 400);
    std::string obs;
    for (std::uint16_t i = 0; i < 2; ++i) {
        sim::IslandMap map(255, 255);
        for (std::int32_t y = 0; y < 255; ++y) {
            for (std::int32_t x = 0; x < 255; ++x) {
                (void)map.place(*table->by_id(20101), x, y, 0, 0);
            }
        }
        map.capture_ground();
        const auto status = i == 0 ? map.build(*table->by_id(23500), 0, 0, 0, 0, 0, 0)
                                   : map.build(*table->by_id(20000), 10, 10, 0, 0, 0, 0);
        obs += std::string(i == 0 ? "255x255" : "Nummer 65535") + (status ? " placed, " : " NOT placed, ");
        assets::IslandHeader header;
        header.number = static_cast<std::uint8_t>(i);
        header.width = 255;
        header.height = 255;
        if (auto added = world.add_island(sim::WorldIsland{header, std::move(map), 20U + 300U * i, 20}); !added) {
            obs += "island refused: " + added.error().message + ", ";
        }
    }
    std::vector<sim::Player> players(2);
    players[0].role = sim::PlayerRole::Human;
    players[0].money = 1'000'000;
    players[1].role = sim::PlayerRole::Computer;
    world.set_players(players);
    sim::City city;
    city.island = 0;
    city.index = 0;
    city.owner = 0;
    city.name = "Mod";
    world.add_city(city);
    const assets::FigureTable figures;
    sim::Simulation simulation(world, *table, figures);
    for (int t = 0; t < 200; ++t) {
        simulation.tick();
    }
    const auto valid = sim::validate(world);
    obs += valid ? ", valid after 200 ticks" : ", INVALID: " + valid.error().message;
    auto saved = sim::write_game(world, *table, &simulation, sim::SaveOptions{false});
    if (!saved) {
        obs += ", save failed: " + saved.error().message;
    } else {
        auto loaded = sim::read_game(*saved, *table, nullptr, &figures);
        obs += loaded ? ", saves and loads (" + std::to_string(saved->size()) + " bytes)"
                      : ", does not load again: " + loaded.error().message;
    }
    row("game", "Id 23500 Size 255x255, Id 20000 Gfx 65000", "a valid running game", obs);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "objfill") == 0) {
        objfill_bomb();  // alone, so that its memory can be measured
        return 0;
    }
    const bool bomb = argc > 1 && std::strcmp(argv[1], "bomb") == 0;
    std::setvbuf(stdout, nullptr, _IOLBF, 0);  // every row is out before a sanitizer stops the program
    std::printf("| File | Value | data-limits.md | Observed |\n|---|---|---|---|\n");
    buildings_cases();
    figures_cases();
    gad_cases(bomb);
    game_with_extremes();
    if (bomb) {
        objfill_bomb();
    }
    return 0;
}
