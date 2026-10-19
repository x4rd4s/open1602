// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/building_table.hpp"

#include <algorithm>
#include <array>

#include "o1602/assets/cod.hpp"
#include "o1602/assets/cod_predefined.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/savegame.hpp"
#include "o1602/core/file.hpp"

namespace o1602::assets {

using core::ErrorCode;
using core::make_error;

namespace {

constexpr std::uint32_t kMaxId = 23500;  // Haus_GetById (0x004630F0) accepts 20000..23500
// Sprites of the island map are 16 bit, 0xFFFF marks an empty tile (sim::MapCell). The original's
// grid word has only 13 bits, but savegames store buildings, not sprites, so mods may go beyond.
constexpr std::uint32_t kMaxSprite = 0xFFFF;
constexpr std::int32_t kProdKindGrowing = 10;  // HAUS_PRODTYP Kind ROHSTWACHS

std::uint32_t non_negative(std::int32_t v) {
    return v < 0 ? 0U : static_cast<std::uint32_t>(v);
}

}  // namespace

core::Result<BuildingTable> BuildingTable::from_cod(const cod::Document& haeuser) {
    const auto* records = haeuser.root.records("HAUS");
    if (records == nullptr) {
        return make_error(ErrorCode::InvalidFormat, "haeuser.cod contains no HAUS records");
    }

    BuildingTable table;
    table.m_by_id.assign(kMaxId - kIdOffset + 1, -1);

    for (const auto& [nummer, r] : *records) {
        const std::int32_t id = r.integer("Id");
        if (id == 0) {
            continue;  // unused slot (filled with defaults by ObjFill)
        }
        if (id < static_cast<std::int32_t>(kIdOffset) || id > static_cast<std::int32_t>(kMaxId)) {
            return make_error(ErrorCode::InvalidFormat,
                              "HAUS #" + std::to_string(nummer) + " has invalid Id " + std::to_string(id));
        }
        BuildingType b;
        b.id = static_cast<std::uint32_t>(id);
        b.record = static_cast<std::uint32_t>(std::max(0, nummer));
        b.kind = r.integer("Kind");
        b.gfx = non_negative(r.integer("Gfx"));
        b.size_x = std::max(1U, non_negative(r.integer("Size", 0, 1)));
        b.size_y = std::max(1U, non_negative(r.integer("Size", 1, 1)));
        // Islands are at most 255 tiles wide; record numbers index a table.
        if (b.size_x > 255 || b.size_y > 255 || nummer > 0xFFFF) {
            return make_error(ErrorCode::InvalidFormat,
                              "HAUS #" + std::to_string(nummer) + " has an invalid Size or Nummer");
        }
        b.rotate = non_negative(r.integer("Rotate"));
        b.anim_count = non_negative(r.integer("AnimAnz"));
        b.anim_add = non_negative(r.integer("AnimAdd"));
        b.anim_time = r.integer("AnimTime");
        b.anim_frame = non_negative(r.integer("AnimFrame"));
        b.posoffs = r.integer("Posoffs");
        if (const cod::Field* einhoffs = r.field("Einhoffs"); einhoffs != nullptr && !einhoffs->values.empty()) {
            b.icon_height = einhoffs->values[0].real;
        }
        b.high = (r.integer("Highflg") & 1) != 0;
        b.draw_ground = (r.integer("Grundflg") & 1) != 0;
        b.door_on_edge = (r.integer("Tuerflg") & 1) != 0;
        b.blocks_shots = (r.integer("NoShotFlg") & 1) != 0;
        if (const cod::Field* energy = r.field("Maxenergy"); energy != nullptr && !energy->values.empty()) {
            b.max_health = static_cast<std::uint32_t>(std::max(0.0, energy->values[0].real * kStockUnitsPerTon));
        }
        if (const cod::Field* ruin = r.field("Ruinenr"); ruin != nullptr && !ruin->values.empty()) {
            b.ruin = static_cast<std::uint8_t>(ruin->values[0].integer);
        }
        for (std::size_t i = 0; i < b.walk_speed.size(); ++i) {
            // 0x00462834: trunc(value * 32 / 100), at most 126. Zero would mean infinite speed
            // (a division by zero in the original); Open1602 keeps at least 1.
            const std::int32_t percent = r.integer("Wegspeed", i, 100);
            b.walk_speed[i] = static_cast<std::uint8_t>(std::clamp(percent * 32 / 100, 1, 126));
        }
        b.place_outside = (r.integer("PlaceFlg") & 1) != 0;
        b.coastal = (r.integer("Strandflg") & 1) != 0;
        b.upgrade = (r.integer("Ausbauflg") & 1) != 0;
        b.destroyed = (r.integer("Destroyflg") & 1) != 0;
        b.build_sound = static_cast<std::uint16_t>(std::clamp(r.integer("Bausample"), 0, 0xFFFF));
        b.beach_offset = static_cast<std::uint8_t>(std::clamp(r.integer("Strandoff"), 0, 255));
        b.mouth_offset = static_cast<std::uint8_t>(std::clamp(r.integer("Mundoff"), 0, 255));
        // Stored as value * 128 / 100 (record +0x40).
        b.edge_growth = static_cast<std::uint8_t>(std::clamp(r.integer("Randwachs", 0, 100) * 128 / 100, 0, 255));
        b.build_gfx = non_negative(r.integer("Baugfx"));
        b.kreuz_base = r.integer("KreuzBase");
        b.rand_count = static_cast<std::uint16_t>(r.integer("RandAnz"));
        b.rand_add = static_cast<std::uint16_t>(r.integer("RandAdd"));
        if (const auto* prod = r.records("HAUS_PRODTYP"); prod != nullptr && !prod->empty()) {
            const auto& prodtyp = prod->begin()->second;
            b.prod_kind = prodtyp.integer("Kind");
            for (std::size_t i = 0; i < b.attached_figures.size(); ++i) {
                b.attached_figures[i] = static_cast<std::uint16_t>(std::max(0, prodtyp.integer("Rauchfignr", i)));
            }
            const auto tons = [&](const char* name) {
                const cod::Field* f = prodtyp.field(name);
                const double v = f != nullptr && !f->values.empty() ? f->values[0].real : 0.0;
                // The original truncates (_ftol); the values in haeuser.cod are exact multiples of 1/32.
                return v > 0.0 ? static_cast<std::uint32_t>(v * kStockUnitsPerTon) : 0U;
            };
            b.ware = static_cast<std::uint8_t>(non_negative(prodtyp.integer("Ware")));
            b.raw_ware = static_cast<std::uint8_t>(non_negative(prodtyp.integer("Rohstoff")));
            b.raw2_ware = static_cast<std::uint8_t>(non_negative(prodtyp.integer("Workstoff")));
            b.produce_amount = tons("Prodmenge");
            b.raw_amount = tons("Rohmenge");
            b.raw2_amount = tons("Workmenge");
            b.interval = non_negative(prodtyp.integer("Interval"));
            b.worker_figure = static_cast<std::uint16_t>(non_negative(prodtyp.integer("Figurnr")));
            b.worker_count = static_cast<std::uint8_t>(non_negative(prodtyp.integer("Figuranz")));
            b.radius = static_cast<std::uint8_t>(std::min(255U, non_negative(prodtyp.integer("Radius"))));
            b.max_stock = non_negative(prodtyp.integer("Maxlager")) * kStockUnitsPerTon;
            b.max_idle_cycles = non_negative(prodtyp.integer("Maxnorohst"));
            b.stock_animation = (prodtyp.integer("LagAniFlg") & 1) != 0;
            b.continuous_animation = (prodtyp.integer("Anicontflg") & 1) != 0;
            b.withered = (prodtyp.integer("Doerrflg") & 1) != 0;
            b.native = (prodtyp.integer("Nativflg") & 1) != 0;
            b.resident_class = static_cast<std::uint8_t>(non_negative(prodtyp.integer("BGruppe")));
            b.infra = static_cast<std::uint8_t>(std::min(255U, non_negative(prodtyp.integer("Bauinfra"))));
            b.workers = static_cast<std::uint16_t>(std::min(65535U, non_negative(prodtyp.integer("Arbeiter"))));
            if (const cod::Field* kosten = prodtyp.field("Kosten"); kosten != nullptr) {
                b.running_cost = non_negative(kosten->integer(0));
                b.idle_cost = non_negative(kosten->integer(1));
            }
            b.ore_mountain = static_cast<std::uint8_t>(non_negative(prodtyp.integer("Erzbergnr")));
            b.max_wares = static_cast<std::uint8_t>(std::min(255U, non_negative(prodtyp.integer("Maxware"))));
            b.max_wares2 = static_cast<std::uint8_t>(std::min(255U, non_negative(prodtyp.integer("Maxware", 1))));
        }

        if (const auto* cost = r.records("HAUS_BAUKOST"); cost != nullptr && !cost->empty()) {
            const auto& c = cost->begin()->second;
            const auto goods = [&](const char* name) {
                const cod::Field* f = c.field(name);
                const double v = f != nullptr && !f->values.empty() ? f->values[0].real : 0.0;
                return v > 0.0 ? static_cast<std::uint32_t>(v * kStockUnitsPerTon) : 0U;
            };
            b.build_cost.tools = goods("Werkzeug");
            b.build_cost.wood = goods("Holz");
            b.build_cost.bricks = goods("Ziegel");
            b.build_cost.cannons = goods("Kanon");
            b.build_cost.money = c.integer("Money");
        }
        if (b.prod_kind == kProdKindGrowing && b.anim_count > 0) {
            // 0x00462F1D: the number of k in 1..32 with 40 s + 7 s * k <= Interval / AnimAnz,
            // clamped to 31 when an entry is added (0x0047C760).
            const std::uint64_t stage_ms = std::uint64_t{b.interval} * 1000U / b.anim_count;
            std::uint32_t cls = 0;
            for (std::uint32_t k = 1; k <= 32; ++k) {
                cls += 40000U + 7000U * k <= stage_ms ? 1U : 0U;
            }
            b.growth_class = static_cast<std::uint8_t>(std::min(cls, 31U));
        }

        // Duplicate ids exist in the original (the wooden walls at a river mouth repeat those of
        // the wooden beach walls, D-007); the first record wins the id, as in the id -> record
        // table the original builds while loading, but every record keeps its number and sprites.
        const std::size_t slot = b.id - kIdOffset;
        const auto index = static_cast<std::int32_t>(table.m_types.size());
        if (table.m_by_id[slot] < 0) {
            table.m_by_id[slot] = index;
        }
        if (std::uint64_t{b.gfx} + b.tile_count() > kMaxSprite) {
            return make_error(ErrorCode::InvalidFormat,
                              "building " + std::to_string(b.id) + ": sprites " + std::to_string(b.gfx) + " + " +
                                  std::to_string(b.tile_count()) + " reach beyond sprite 65534");
        }
        if (b.gfx + b.tile_count() > table.m_by_sprite.size()) {
            table.m_by_sprite.resize(b.gfx + b.tile_count(), -1);
        }
        for (std::uint32_t k = 0; k < b.tile_count(); ++k) {
            if (table.m_by_sprite[b.gfx + k] < 0) {
                table.m_by_sprite[b.gfx + k] = index;
            }
        }
        if (b.record >= table.m_by_record.size()) {
            table.m_by_record.resize(std::size_t{b.record} + 1, -1);
        }
        table.m_by_record[b.record] = index;
        table.m_types.push_back(b);
    }

    // Beach and river mouth variants know the land building they stand for.
    for (const BuildingType& land : table.m_types) {
        if (land.beach_offset == 0) {
            continue;
        }
        const std::uint32_t beach = land.record + land.beach_offset;
        if (beach >= table.m_by_record.size() || table.m_by_record[beach] < 0) {
            continue;
        }
        BuildingType& b = table.m_types[static_cast<std::size_t>(table.m_by_record[beach])];
        b.land_record = land.record;
        const std::uint32_t mouth = beach + b.mouth_offset;
        if (b.mouth_offset != 0 && mouth < table.m_by_record.size() && table.m_by_record[mouth] >= 0) {
            table.m_types[static_cast<std::size_t>(table.m_by_record[mouth])].land_record = land.record;
        }
    }

    // Classes of inhabitants and their needs (0x00460750: k = trunc(v * 8192) / 600).
    const auto per_tick = [](double v) -> std::uint32_t {
        return v > 0.0 ? static_cast<std::uint32_t>(static_cast<std::int64_t>(v * 8192.0) / 600) : 0U;
    };
    if (const cod::Field* food = haeuser.root.field("Nahrung"); food != nullptr && !food->values.empty()) {
        table.m_food_need = per_tick(food->values[0].real);
    }
    const auto ware_symbol = [&](const char* name, std::uint8_t& out) {
        if (const cod::Symbol* sym = haeuser.symbols.find(name); sym != nullptr && sym->defined) {
            out = static_cast<std::uint8_t>(sym->values[0].integer);
        }
    };
    ware_symbol("NAHRUNG", table.m_food_ware);
    ware_symbol("WERKZEUG", table.m_materials.tools);
    ware_symbol("HOLZ", table.m_materials.wood);
    ware_symbol("ZIEGEL", table.m_materials.bricks);
    ware_symbol("KANONEN", table.m_materials.cannons);
    if (const auto* groups = haeuser.root.records("BGRUPPE")) {
        for (const auto& [nummer, g] : *groups) {
            if (nummer < 0 || nummer > 255) {
                continue;
            }
            if (static_cast<std::size_t>(nummer) >= table.m_classes.size()) {
                table.m_classes.resize(static_cast<std::size_t>(nummer) + 1);
            }
            ResidentClass& c = table.m_classes[static_cast<std::size_t>(nummer)];
            c.share = non_negative(g.integer("Prozent")) * 128U / 100U;
            c.max_per_house = non_negative(g.integer("Maxwohn"));
            if (const cod::Field* tax = g.field("Steuer"); tax != nullptr && !tax->values.empty()) {
                c.tax = tax->values[0].real > 0.0 ? static_cast<std::uint32_t>(tax->values[0].real * 16.0 / 3.0) : 0U;
            }
            c.hiscore = non_negative(g.integer("Hiscore"));
            if (const auto* wares = g.records("BGRUPPE_WARE")) {
                for (const auto& [n, list] : *wares) {
                    for (const auto& [name, values] : list.history) {
                        if (name != "Ware" || values.size() < 2) {
                            continue;
                        }
                        const auto ware = static_cast<std::uint8_t>(std::max(0, values[0].integer));
                        const std::uint32_t amount = per_tick(values[1].real);
                        if (ware == 0 || amount == 0) {
                            continue;
                        }
                        auto it = std::find_if(c.needs.begin(), c.needs.end(),
                                               [&](const ResidentClass::Need& need) { return need.ware == ware; });
                        if (it != c.needs.end()) {
                            it->amount = amount;  // a later line replaces an earlier one
                        } else {
                            c.needs.push_back(ResidentClass::Need{ware, amount});
                        }
                    }
                }
            }
        }
    }

    // Unlock levels (BAUINFRA, table 0x0061FBC0: {BGruppe, Minwohn} by INFRA number).
    if (const auto* infra = haeuser.root.records("BAUINFRA")) {
        for (const auto& [nummer, level] : *infra) {
            if (nummer < 0 || nummer > 255) {
                continue;
            }
            if (static_cast<std::size_t>(nummer) >= table.m_infra.size()) {
                table.m_infra.resize(static_cast<std::size_t>(nummer) + 1);
            }
            table.m_infra[static_cast<std::size_t>(nummer)] =
                InfraLevel{static_cast<std::uint8_t>(non_negative(level.integer("BGruppe"))),
                           non_negative(level.integer("Minwohn"))};
        }
    }
    std::size_t max_infra = table.m_infra.size();
    for (const BuildingType& b : table.m_types) {
        max_infra = std::max<std::size_t>(max_infra, std::size_t{b.infra} + 1);
    }
    table.m_infra.resize(max_infra);

    constexpr std::int32_t kKindMeer = 19;
    constexpr std::int32_t kProdKindOpenSea = 9;
    for (std::size_t i = 0; i < table.m_types.size(); ++i) {
        if (table.m_types[i].kind == kKindMeer && table.m_types[i].prod_kind == kProdKindOpenSea) {
            table.m_open_sea = static_cast<std::int32_t>(i);
            break;
        }
    }
    return table;
}

const BuildingType* BuildingTable::by_id(std::uint32_t id) const noexcept {
    if (id < kIdOffset || id > kMaxId) {
        return nullptr;
    }
    const std::int32_t index = m_by_id[id - kIdOffset];
    return index < 0 ? nullptr : &m_types[static_cast<std::size_t>(index)];
}

const BuildingType* BuildingTable::first_house(std::size_t resident_class) const noexcept {
    constexpr std::int32_t kProdKindHouse = 13;  // WOHNUNG
    for (const BuildingType& b : m_types) {
        if (b.prod_kind == kProdKindHouse && b.resident_class == resident_class) {
            return &b;
        }
    }
    return nullptr;
}

const BuildingType* BuildingTable::ruin_record(std::uint8_t ruin, bool beach) const noexcept {
    // 0x00462F6A: land and beach ruins of wooden houses, brick houses, plantations, field paths,
    // roads and mines (IDRUINE = 22601 + offset). The Kontor and market ruins follow (not used yet).
    constexpr std::uint32_t kIdRuine = 22601;
    constexpr std::array<std::uint32_t, 8> kRuins = {0, 9, 10, 19, 20, 30, 35, 40};
    std::size_t slot = ruin;
    if (beach && (slot == 0 || slot == 2)) {
        ++slot;
    }
    return slot < kRuins.size() ? by_id(kIdRuine + kRuins[slot]) : nullptr;
}

const BuildingType* BuildingTable::by_record(std::uint32_t record) const noexcept {
    if (record >= m_by_record.size()) {
        return nullptr;
    }
    const std::int32_t index = m_by_record[record];
    return index < 0 ? nullptr : &m_types[static_cast<std::size_t>(index)];
}

const BuildingType* BuildingTable::by_sprite(std::uint32_t sprite) const noexcept {
    if (sprite >= m_by_sprite.size()) {
        return nullptr;
    }
    const std::int32_t index = m_by_sprite[sprite];
    return index < 0 ? nullptr : &m_types[static_cast<std::size_t>(index)];
}

core::Result<BuildingTable> BuildingTable::load(const GameDirectory& game) {
    const auto path = game.resolve("haeuser.cod");
    if (!path) {
        return make_error(ErrorCode::NotFound, "haeuser.cod not found in the game directory");
    }
    auto bytes = core::read_file(*path);
    if (!bytes) {
        return bytes.error();
    }
    return from_cod(cod::parse(decode_cod(*bytes), cod_predefined_haeuser()));
}

}  // namespace o1602::assets
