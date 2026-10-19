// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ship's window (docs/re/ui.md#auswahlfenster): the ship, transfer with the own Kontor, trade
// with a foreign one.
#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

#include "panel_common.hpp"

#include "o1602/sim/diplomacy.hpp"
#include "o1602/sim/ships.hpp"
#include "o1602/ui/text_format.hpp"

namespace o1602::app {

namespace {

using namespace panel_detail;

// ---- Ships ---------------------------------------------------------------------------------------

constexpr std::uint8_t kCannonWare = 13;  // KANONEN
constexpr std::uint8_t kFirstSoldierWare = 25;
constexpr std::uint8_t kLastSoldierWare = 44;
constexpr std::array<std::uint32_t, 4> kSteps{32, 5 * 32, 10 * 32, 50 * 32};  // 1, 5, 10, 50 t

/// SHIP (the ship), TRANSFER (with the own Kontor), TRADE (with a foreign one), ROUTE (the trade
/// route, with LAGER and HANDEL as ware pickers) and PIRAT/PIRATORD (dealing with the pirates)
/// (ShipPanel_Refresh 0x00436170, TransferPanel_Refresh 0x004372C0, TradePanel_Refresh 0x004377F0,
/// RoutePanel_Refresh 0x00436E60, PiratePanel_Refresh 0x00436C10).
class ShipPanel final : public SelectionPanel {
public:
    explicit ShipPanel(ShipLayouts l)
        : m_ship(std::move(l.ship), 1.0),
          m_transfer(std::move(l.transfer), 1.0),
          m_trade(std::move(l.trade), 1.0),
          m_route(std::move(l.route), 1.0),
          m_pirat(std::move(l.pirat), 1.0),
          m_piratord(std::move(l.piratord), 1.0),
          m_stock(std::move(l.lager)),
          m_lists(std::move(l.handel)) {
        const assets::GadgetLayout& sl = m_ship.layout();
        m_gfx_ware = sl.constant("GFXWARE").value_or(192);
        m_gfx_empty = sl.constant("GFXWARELEER").value_or(188) + kEmptyHold;
        m_gfx_ship = sl.constant("GFXSHIP").value_or(363);
        // Where the buttons of the left column stand in SHIP.GAD, and the bottom of the name.
        for (std::size_t i = 0; i < m_column.size(); ++i) {
            m_column[i] = m_ship.position(kColumn[i]).value_or(std::array<std::int32_t, 2>{});
        }
        if (const auto name = sl.index_of(kNameFrame)) {
            m_name_bottom = sl.gadgets[*name].pos[1] + sl.gadgets[*name].size[1];
        }
        m_ship.set_visible(kDeadSlider, false);
        m_transfer.set_selected(45003, true);
        m_transfer.set_selected(45141, true);
        m_trade.set_selected(33003, true);
        if (const assets::Gadget* g = m_route.layout().find(kStopSymbol)) {
            m_gfx_stop = g->gfx;
        }
        if (const assets::Gadget* g = m_route.layout().find(kStopArms)) {
            m_gfx_arms = g->gfx;
        }
        if (const assets::Gadget* g = m_route.layout().find(kEntryArrow[0])) {
            m_gfx_arrow = g->gfx;
        }
        if (const assets::Gadget* g = m_piratord.layout().find(kPirateArms)) {
            m_gfx_pirate_arms = g->gfx;
        }
    }
    void set_scale(double scale) override {
        for (ui::Panel* p : {&m_ship, &m_transfer, &m_trade, &m_route, &m_pirat, &m_piratord}) {
            p->set_scale(scale);
        }
        m_stock.panel().set_scale(scale);
        m_lists.panel().set_scale(scale);
    }
    ui::Panel& panel() override {
        switch (m_page) {
            case Page::Transfer:
                return m_transfer;
            case Page::Trade:
                return m_trade;
            case Page::Route:
                return m_route;
            case Page::PickOwn:
                return m_stock.panel();
            case Page::PickForeign:
                return m_lists.panel();
            case Page::Pirates:
                return m_pirat;
            case Page::PirateOrder:
                return m_piratord;
            default:
                return m_ship;
        }
    }
    [[nodiscard]] bool picking() const override { return m_pick_stop.has_value(); }
    void picked(std::uint32_t x, std::uint32_t y, const Selection& s, const PanelContext& c,
                std::vector<sim::Command>& out) override {
        const std::optional<std::uint8_t> stop = std::exchange(m_pick_stop, std::nullopt);
        const sim::Ship* ship = c.world().ship(s.ship);
        if (!stop || ship == nullptr || ship->owner != c.local) {
            return;
        }
        // Route_PickStopAt 0x00440B30: the city of the tile, any owner; no city ends the pick.
        const sim::World& w = c.world();
        if (x >= w.width() || y >= w.height()) {
            return;
        }
        const sim::World::Cell& cell = w.cell(x, y);
        if (cell.island == sim::World::kSea) {
            return;
        }
        const sim::MapCell& top = w.islands()[cell.island].map.top(cell.local_x, cell.local_y);
        const sim::City* city = w.city(cell.island, top.city);
        if (city != nullptr && city->warehouse_x) {
            out.emplace_back(route_stop(*ship, c, *stop, sim::Target::city_of(city->island, city->index)));
        }
    }

    void refresh(const Selection& s, const PanelContext& c) override {
        if (s.ship != m_last_ship) {
            m_last_ship = s.ship;
            m_page = Page::Root;
            m_hold = 0;
            m_offer = 0;
        }
        const sim::Ship* ship = c.world().ship(s.ship);
        if (ship == nullptr) {
            return;
        }
        const Context k = context(*ship, c);
        // Transfer, trade and the pirates end when the ship sails off (0x0043AA00); the route
        // editor stays.
        const bool at_kontor = m_page == Page::Transfer                                 ? k.own_kontor
                               : m_page == Page::Trade                                  ? k.foreign_kontor
                               : m_page == Page::Pirates || m_page == Page::PirateOrder ? k.pirates
                                                                                        : true;
        if (ship->owner != c.local || !at_kontor) {
            m_page = Page::Root;
            m_pick_stop.reset();
        }
        switch (m_page) {
            case Page::Root:
                refresh_root(*ship, k, c);
                break;
            case Page::Transfer:
                refresh_transfer(*ship, k, c);
                break;
            case Page::Trade:
                refresh_trade(*ship, k, c);
                break;
            case Page::Route:
                refresh_route(*ship, c);
                break;
            case Page::PickOwn:
            case Page::PickForeign:
                refresh_picker(*ship, c);
                break;
            case Page::Pirates:
            case Page::PirateOrder:
                refresh_pirates(c);
                break;
        }
    }

    void clicked(const ui::PanelEvent& e, const Selection& s, const PanelContext& c,
                 std::vector<sim::Command>& out) override {
        const sim::Ship* ship = c.world().ship(s.ship);
        if (ship == nullptr || ship->owner != c.local) {
            // Somebody else's ship: only buying it, if it is offered.
            if (ship != nullptr && e.kind == ui::PanelEvent::Kind::Clicked && e.id == kSale && ship->for_sale) {
                out.emplace_back(ship_command(*ship, c, sim::ShipCommand::Op::Buy));
            }
            return;
        }
        switch (m_page) {
            case Page::Transfer:
                clicked_transfer(e, *ship, c, out);
                break;
            case Page::Trade:
                clicked_trade(e, *ship, c, out);
                break;
            case Page::Route:
                clicked_route(e, *ship, c, out);
                break;
            case Page::PickOwn:
            case Page::PickForeign:
                clicked_picker(e, *ship, c, out);
                break;
            case Page::Pirates:
            case Page::PirateOrder:
                clicked_pirates(e, *ship, c, out);
                break;
            default:
                clicked_root(e, *ship, c, out);
                break;
        }
    }

    [[nodiscard]] std::optional<std::string> help(std::int32_t id, const Selection& s,
                                                  const PanelContext& c) const override {
        const sim::Ship* ship = c.world().ship(s.ship);
        if (ship == nullptr) {
            return std::nullopt;
        }
        const auto line = [&](std::size_t n) { return std::optional<std::string>(help_line(c, n)); };
        // Holds: their goods, the soldiers' kind, or empty.
        const auto hold_text = [&](std::size_t h) -> std::optional<std::string> {
            if (h >= ship->cargo.size()) {
                return std::nullopt;
            }
            const sim::CargoSlot& slot = ship->cargo[h];
            if (slot.ware == 0 || slot.amount == 0) {
                return c.text("WARE", 0);
            }
            if (slot.ware >= kFirstSoldierWare && slot.ware <= kLastSoldierWare) {
                return c.text("WARE", slot.ware);
            }
            return ui::format_text(c.text("GAME", 50),
                                   {std::int64_t{slot.amount >> 5U}, std::string_view(c.text("WARE", slot.ware))});
        };
        if (m_page == Page::Root) {
            switch (id) {
                case kName:
                    return line(10);
                case kRepair:
                    return line(103);
                case kSale:
                    return line(ship->owner != c.local ? 107 : ship->for_sale ? 106 : 105);
                case kTrade:
                    return line(108);
                case kCannonsDown:
                    return line(119);
                case kCannons:
                    return ship->cannons > 0 ? ui::format_text(c.text("GAME", 23), {std::int64_t{ship->cannons}})
                                             : c.text("GAME", 24);
                case kHealthHover:
                    return line(121);
                case kPriceSlider:
                    return line(118);
                case kPriceDown:
                    return line(116);
                case kPriceUp:
                    return line(117);
                case kFound:
                    return line(122);
                case kTransfer:
                    return line(123);
                case kRoute:
                    return line(ship->route.active ? 110 : 111);
                case kStop:
                    return line(127);
                case kExplore:
                    return line(125);
                case kScuttle:
                    return line(238);
                case kPirates:
                    return line(104);
                case kRouteEditor:
                    return line(109);
                default:
                    break;
            }
            if (id >= kHoldButton && id < kHoldButton + 8) {
                return hold_text(static_cast<std::size_t>(id - kHoldButton));
            }
            return std::nullopt;
        }
        if (m_page == Page::Route) {
            return route_help(id, *ship, c);
        }
        if (m_page == Page::PickOwn || m_page == Page::PickForeign) {
            return picker_help(id, *ship, c);
        }
        if (m_page == Page::Pirates || m_page == Page::PirateOrder) {
            return std::nullopt;  // the original has none
        }
        const sim::City* city = m_city ? c.world().city(m_city->first, m_city->second) : nullptr;
        if (m_page == Page::Transfer) {
            switch (id) {
                case 45001:
                    return line(158);
                case 45002:
                    return line(160);
                case 45007:
                case 45008:
                    return line(165);
                case 45122:
                    return line(10);
                case 45124:
                    return line(11);
                default:
                    break;
            }
            if (id >= 45003 && id <= 45006) {
                return line(static_cast<std::size_t>(49 + id - 45003));
            }
            if (id >= 45141 && id <= 45144) {
                return line(static_cast<std::size_t>(161 + id - 45141));
            }
            if (id >= 45071 && id <= 45078) {
                return hold_text(static_cast<std::size_t>(id - 45071));
            }
            if (id >= 45031 && id <= 45034 && city != nullptr) {
                const std::uint8_t w = kontor_ware(static_cast<std::int32_t>(id - 45031));
                if (w == 0) {
                    return std::nullopt;
                }
                const std::uint32_t t = city->stock(w) >> 5U;
                return t >= 1
                           ? ui::format_text(c.text("GAME", 50), {std::int64_t{t}, std::string_view(c.text("WARE", w))})
                           : c.text("WARE", w);
            }
            return std::nullopt;
        }
        // Trade.
        const bool natives = city != nullptr && is_natives(*city, c);
        switch (id) {
            case 33001:
                return line(natives ? 47 : 45);
            case 33002:
                return line(natives ? 48 : 46);
            case 33122:
                return line(10);
            case 33124:
                return line(11);
            default:
                break;
        }
        if (id >= 33003 && id <= 33006) {
            return line(static_cast<std::size_t>(49 + id - 33003));
        }
        if (id >= 33031 && id <= 33038 && city != nullptr) {
            const auto k = static_cast<std::size_t>(id - 33031);
            if (k >= m_offers.size()) {
                return std::nullopt;
            }
            const std::uint8_t w = m_offers[k];
            return ui::format_text(c.text("GAME", 50),
                                   {std::int64_t{sellable(*city, w) >> 5U}, std::string_view(c.text("WARE", w))});
        }
        if (id >= 33071 && id <= 33078 && city != nullptr) {
            const auto h = static_cast<std::size_t>(id - 33071);
            if (h >= ship->cargo.size()) {
                return std::nullopt;
            }
            const sim::CargoSlot& slot = ship->cargo[h];
            if (slot.ware == 0 || slot.amount == 0 ||
                (slot.ware >= kFirstSoldierWare && slot.ware <= kLastSoldierWare)) {
                return hold_text(h);
            }
            const std::uint32_t wanted = buyable(*city, slot.ware);
            const sim::City::Trade* t = city->trade_of(slot.ware);
            if (wanted == 0) {
                return ui::format_text(c.text("GAME", t != nullptr && t->buy ? 67 : 68),
                                       {std::int64_t{slot.amount >> 5U}, std::string_view(c.text("WARE", slot.ware))});
            }
            return ui::format_text(c.text("GAME", natives ? 69 : 70),
                                   {std::int64_t{slot.amount >> 5U}, std::string_view(c.text("WARE", slot.ware)),
                                    std::int64_t{wanted >> 5U}, std::int64_t{t != nullptr ? t->buy_price : 0}});
        }
        return std::nullopt;
    }

    bool back() override {
        if (m_pick_stop) {
            m_pick_stop.reset();
            return true;
        }
        switch (m_page) {
            case Page::Root:
                return false;
            case Page::PickOwn:
            case Page::PickForeign:
                m_page = Page::Route;
                return true;
            case Page::PirateOrder:
                m_page = Page::Pirates;
                return true;
            default:
                m_page = Page::Root;
                return true;
        }
    }
    std::optional<Request> take_request() override { return std::exchange(m_request, std::nullopt); }

private:
    enum class Page : std::uint8_t { Root, Transfer, Trade, Route, PickOwn, PickForeign, Pirates, PirateOrder };
    static constexpr std::int32_t kName = 38002;
    static constexpr std::int32_t kRepair = 38003;
    static constexpr std::int32_t kSale = 38004;
    static constexpr std::int32_t kTrade = 38005;
    static constexpr std::int32_t kCannonsDown = 38006;
    static constexpr std::int32_t kCannons = 38007;
    static constexpr std::int32_t kHealthFrame = 38008;
    static constexpr std::int32_t kHealth = 38009;
    static constexpr std::int32_t kHealthHover = 38010;
    static constexpr std::int32_t kPriceTrack = 38011;
    static constexpr std::int32_t kPriceSlider = 38012;
    static constexpr std::int32_t kPriceDown = 38013;
    static constexpr std::int32_t kPriceUp = 38014;
    static constexpr std::int32_t kPriceLabel = 38015;
    static constexpr std::int32_t kPrice = 38016;
    static constexpr std::int32_t kFound = 38017;
    static constexpr std::int32_t kTransfer = 38018;
    static constexpr std::int32_t kPirates = 38019;
    static constexpr std::int32_t kRoute = 38020;
    static constexpr std::int32_t kRouteEditor = 38021;
    static constexpr std::int32_t kStop = 38022;
    static constexpr std::int32_t kExplore = 38023;
    static constexpr std::int32_t kExploreRing = 38024;
    static constexpr std::int32_t kScuttle = 38025;
    static constexpr std::int32_t kHoldIcon = 38051;
    static constexpr std::int32_t kHoldBar = 38071;
    static constexpr std::int32_t kHoldButton = 38091;
    static constexpr std::int32_t kDeadSlider = 38111;
    // ROUTE.GAD: + stop 0..3; entries 0 and 1 of a stop.
    static constexpr std::int32_t kStopName = 58011;
    static constexpr std::int32_t kStopArmsShadow = 58021;
    static constexpr std::int32_t kStopArms = 58031;
    static constexpr std::int32_t kStopSymbol = 58041;
    static constexpr std::array<std::int32_t, 2> kEntryIcon{58101, 58201};
    [[maybe_unused]] static constexpr std::array<std::int32_t, 2> kEntryBack{58111, 58211};
    static constexpr std::array<std::int32_t, 2> kEntryBar{58121, 58221};
    static constexpr std::array<std::int32_t, 2> kEntryFrame{58131, 58231};
    static constexpr std::array<std::int32_t, 2> kEntrySlider{58141, 58241};
    static constexpr std::array<std::int32_t, 2> kEntryArrow{58151, 58251};
    // PIRAT.GAD, PIRATORD.GAD.
    static constexpr std::int32_t kBuyPeace = 54004;
    static constexpr std::int32_t kHire = 54005;
    static constexpr std::int32_t kPirateText = 55011;    // + line 0..2
    static constexpr std::int32_t kPirateArms = 55021;    // + player 0..2
    static constexpr std::int32_t kPirateChoice = 55031;  // + player 0..2
    static constexpr std::int32_t kPay = 55004;
    static constexpr std::int32_t kCostLabel = 55005;
    static constexpr std::int32_t kCost = 55006;

    /// What the ship can do where it lies (Ship_ContextActions 0x0044E630).
    struct Context {
        bool foreign = true;  ///< not the local player's ship
        bool moving = false;
        bool own_kontor = false;                ///< at an own Kontor: transfer
        bool foreign_kontor = false;            ///< at another's Kontor with a trade treaty: trade
        bool found = false;                     ///< an island within 4 tiles without an own city
        std::optional<sim::CostBuilding> yard;  ///< an own shipyard in reach
        sim::ExploreState explore = sim::ExploreState::NotAllowed;
        bool pirates = false;                                        ///< at the pirates' Kontor
        std::optional<std::pair<std::uint16_t, std::uint8_t>> city;  ///< the Kontor's city
    };

    Context context(const sim::Ship& ship, const PanelContext& c) {
        Context k;
        const sim::World& w = c.world();
        k.foreign = ship.owner != c.local;
        const sim::ShipMotion& m = ship.motion;
        k.moving = m.phase == sim::ShipMotion::Phase::Run || m.phase == sim::ShipMotion::Phase::Turn ||
                   (m.path_index < m.path.size());
        k.explore = c.simulation.fleet().explore_state(ship, c.local);
        if (k.foreign || k.moving) {
            return k;
        }
        if (const auto index = sim::kontor_in_reach(w, c.buildings, ship.x, ship.y)) {
            const sim::City& city = w.cities()[*index];
            k.city = std::make_pair(city.island, city.index);
            const sim::Player* owner = w.player(city.owner);
            if (city.owner == ship.owner) {
                k.own_kontor = true;
            } else if (owner != nullptr && owner->role == sim::PlayerRole::Pirates) {
                k.pirates = true;
            } else if (w.diplomacy().may_trade(ship.owner, city.owner)) {  // natives too: by treaty
                k.foreign_kontor = true;
            }
        } else {
            // An island within 4 tiles where the player has no city yet.
            for (int dy = -4; dy <= 4 && !k.found; ++dy) {
                for (int dx = -4; dx <= 4 && !k.found; ++dx) {
                    const std::int32_t x = ship.x + dx;
                    const std::int32_t y = ship.y + dy;
                    if (x < 0 || y < 0 || x >= static_cast<std::int32_t>(w.width()) ||
                        y >= static_cast<std::int32_t>(w.height())) {
                        continue;
                    }
                    const sim::World::Cell& cell = w.cell(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
                    if (cell.island == sim::World::kSea) {
                        continue;
                    }
                    const bool own_city = std::any_of(w.cities().begin(), w.cities().end(), [&](const sim::City& ct) {
                        return ct.island == cell.island && ct.owner == ship.owner;
                    });
                    k.found = !own_city;
                }
            }
        }
        for (const sim::CostBuilding& b : w.cost_buildings()) {
            if (b.kind != sim::CostBuilding::Kind::Shipyard || b.island >= w.islands().size()) {
                continue;
            }
            const sim::MapCell& top = w.islands()[b.island].map.top(b.x, b.y);
            if (top.owner == ship.owner && sim::building_in_reach(w, c.buildings, b.island, b.x, b.y, ship.x, ship.y)) {
                k.yard = b;
                break;
            }
        }
        return k;
    }

    [[nodiscard]] static bool is_natives(const sim::City& city, const PanelContext& c) {
        const sim::Player* p = c.world().player(city.owner);
        return p != nullptr && p->role == sim::PlayerRole::Natives;
    }
    [[nodiscard]] static sim::ShipCommand ship_command(const sim::Ship& ship, const PanelContext& c,
                                                       sim::ShipCommand::Op op) {
        sim::ShipCommand o;
        o.player = c.local;
        o.ship = ship.id;
        o.op = op;
        return o;
    }
    [[nodiscard]] std::uint32_t step(const ui::Panel& p, std::int32_t first) const {
        for (std::size_t k = 0; k < kSteps.size(); ++k) {
            if (p.selected(first + static_cast<std::int32_t>(k))) {
                return kSteps[k];
            }
        }
        return kSteps[0];
    }

    void holds(ui::Panel& p, const sim::Ship& ship, const PanelContext& c, std::int32_t icon, std::int32_t bar,
               std::int32_t button, bool shown) {
        const std::size_t n = sim::usable_holds(ship, c.figures);
        for (std::size_t h = 0; h < 8; ++h) {
            const auto k = static_cast<std::int32_t>(h);
            const bool hold = shown && h < n;
            p.set_visible(icon + k, hold);
            p.set_visible(button + k, hold);
            const bool filled = hold && ship.cargo[h].ware != 0 && ship.cargo[h].amount != 0;
            p.set_visible(bar + k, filled);
            if (!hold) {
                continue;
            }
            const sim::CargoSlot& slot = ship.cargo[h];
            p.set_graphic(icon + k, filled ? m_gfx_ware + slot.ware : m_gfx_empty, 0);
            if (filled) {
                p.set_slider(bar + k, 1, static_cast<std::int32_t>(std::min(slot.amount, sim::kHoldCapacity)),
                             static_cast<std::int32_t>(sim::kHoldCapacity));
            }
        }
    }

    void refresh_root(const sim::Ship& ship, const Context& k, const PanelContext& c) {
        m_ship.set_text(kName, ship.name);
        const bool own = !k.foreign;
        const bool idle = own && !k.moving;
        m_ship.set_visible(kTrade, idle && k.foreign_kontor);
        m_ship.set_visible(kTransfer, idle && k.own_kontor);
        const assets::FigureType* f = c.figures.get(ship.figure);
        const std::uint32_t max_health = f != nullptr ? std::max<std::uint32_t>(f->ship.max_health, 1) : 1;
        const std::uint32_t health = ship.health == 0 ? max_health : ship.health;
        m_ship.set_visible(kRepair, idle && k.yard.has_value() && health < max_health);
        m_ship.set_selected(kRepair, ship.repairing);
        m_ship.set_visible(kFound, idle && k.found);
        m_ship.set_visible(kStop, own && k.moving);
        const bool running = k.explore == sim::ExploreState::Running;
        m_ship.set_visible(kExplore, own && (k.explore == sim::ExploreState::Possible || running));
        m_ship.set_visible(kExploreRing, own && running);
        if (running) {
            m_ship.set_graphic(kExploreRing, m_gfx_ship + 28 + (11 - std::min<std::int32_t>(ship.explore_steps, 11)),
                               0);
        }
        // Sale: an own ship can be offered; another's only shows its price, and can be bought.
        m_ship.set_visible(kSale, own || ship.for_sale);
        m_ship.set_selected(kSale, ship.for_sale && own);
        for (const std::int32_t id : {kPriceTrack, kPriceSlider, kPriceDown, kPriceUp}) {
            m_ship.set_visible(id, ship.for_sale && own);
        }
        m_ship.set_visible(kPriceLabel, ship.for_sale);
        m_ship.set_visible(kPrice, ship.for_sale);
        if (ship.for_sale) {
            m_ship.set_text(kPriceLabel, c.text("GAME", 10));
            m_ship.set_text(kPrice, "$ " + std::to_string(c.simulation.fleet().sale_price(ship)));
            if (m_ship.held() != kPriceSlider) {
                m_ship.set_slider(kPriceSlider, 0, ship.sale_factor, sim::ShipCommand::kMaxSaleFactor);
            }
        }
        for (const std::int32_t id : {kHealthFrame, kHealth, kHealthHover, kCannonsDown, kCannons}) {
            m_ship.set_visible(id, own);
        }
        m_ship.set_slider(kHealth, 0, static_cast<std::int32_t>(std::uint64_t{health} * 1000 / max_health), 1000);
        m_ship.set_text(kCannons, std::to_string(ship.cannons));
        m_ship.set_disabled(kCannonsDown, ship.cannons == 0);
        m_ship.set_visible(kRoute, own && !ship.for_sale && ship.route.has_stops());
        m_ship.set_visible(kRouteEditor, own && !ship.for_sale);
        m_ship.set_visible(kScuttle, own && !ship.for_sale);
        m_ship.set_visible(kPirates, idle && k.pirates);
        arrange_column(own && (k.explore == sim::ExploreState::Possible || running),
                       idle && k.yard.has_value() && health < max_health, idle && k.pirates);
        m_ship.set_selected(kRoute, !ship.route.active);
        holds(m_ship, ship, c, kHoldIcon, kHoldBar, kHoldButton, own || ship.for_sale);
    }

    /// U-013: in the original, "repair" and "pirates" take the place of "explore" (one button
    /// over the other). When both apply, Open1602 puts the other button right under the name and
    /// "explore" below it, so every action can be clicked.
    void arrange_column(bool explore, bool repair, bool pirates) {
        const bool apart = explore && (repair || pirates);
        std::int32_t explore_top = m_column[2][1];
        for (std::size_t i = 0; i < 2; ++i) {
            const std::int32_t y = apart ? m_name_bottom + 2 : m_column[i][1];
            m_ship.set_position(kColumn[i], m_column[i][0], y);
            const bool shown = i == 0 ? repair : pirates;
            if (apart && shown) {
                if (const auto g = m_ship.layout().index_of(kColumn[i])) {
                    explore_top = std::max(explore_top, y + m_ship.layout().gadgets[*g].size[1]);
                }
            }
        }
        for (std::size_t i = 2; i < m_column.size(); ++i) {
            m_ship.set_position(kColumn[i], m_column[i][0], m_column[i][1] + (explore_top - m_column[2][1]));
        }
    }

    void clicked_root(const ui::PanelEvent& e, const sim::Ship& ship, const PanelContext& c,
                      std::vector<sim::Command>& out) {
        if (e.kind == ui::PanelEvent::Kind::Slid && e.id == kPriceSlider) {
            const auto factor =
                static_cast<std::uint16_t>(m_ship.slider(kPriceSlider, 0, sim::ShipCommand::kMaxSaleFactor));
            if (factor != ship.sale_factor) {
                sim::ShipCommand o = ship_command(ship, c, sim::ShipCommand::Op::OfferForSale);
                o.factor = factor;
                out.emplace_back(o);
            }
            return;
        }
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            return;
        }
        const Context k = context(ship, c);
        switch (e.id) {
            case kStop: {
                sim::ShipOrder o;
                o.player = c.local;
                o.ship = ship.id;
                out.emplace_back(o);
                break;
            }
            case kExplore: {
                sim::ExploreOrder o;
                o.player = c.local;
                o.ship = ship.id;
                out.emplace_back(o);
                break;
            }
            case kRoute: {
                sim::RouteEdit o;
                o.player = c.local;
                o.ship = ship.id;
                o.op = ship.route.active ? sim::RouteEdit::Op::Stop : sim::RouteEdit::Op::Start;
                out.emplace_back(o);
                break;
            }
            case kRepair:
                if (k.yard) {
                    sim::ShipyardOrder o;
                    o.player = c.local;
                    o.island = k.yard->island;
                    o.x = k.yard->x;
                    o.y = k.yard->y;
                    o.op = sim::ShipyardOrder::Op::Repair;
                    o.ship = ship.id;
                    o.on = !ship.repairing;
                    out.emplace_back(o);
                }
                break;
            case kSale:
                out.emplace_back(ship_command(
                    ship, c, ship.for_sale ? sim::ShipCommand::Op::WithdrawSale : sim::ShipCommand::Op::OfferForSale));
                break;
            case kPriceDown:
            case kPriceUp: {
                sim::ShipCommand o = ship_command(ship, c, sim::ShipCommand::Op::OfferForSale);
                o.factor = static_cast<std::uint16_t>(std::clamp<int>(ship.sale_factor + (e.id == kPriceUp ? 1 : -1), 0,
                                                                      sim::ShipCommand::kMaxSaleFactor));
                out.emplace_back(o);
                break;
            }
            case kCannonsDown:
                if (ship.cannons > 0) {
                    out.emplace_back(ship_command(ship, c, sim::ShipCommand::Op::UnmountCannon));
                }
                break;
            case kTrade:
            case kTransfer:
                if (k.city) {
                    m_city = k.city;
                    m_page = e.id == kTrade ? Page::Trade : Page::Transfer;
                    // The fullest hold is chosen first (0x0044E390).
                    m_hold = 0;
                    for (std::size_t h = 0; h < ship.cargo.size(); ++h) {
                        if (ship.cargo[h].amount >= ship.cargo[static_cast<std::size_t>(m_hold)].amount) {
                            m_hold = static_cast<std::int32_t>(h);
                        }
                    }
                    m_offer = 0;
                }
                break;
            case kFound:
                m_request = Request{Request::Kind::FoundKontor, ship.id};
                break;
            case kScuttle:
                out.emplace_back(ship_command(ship, c, sim::ShipCommand::Op::Scuttle));
                break;
            case kPirates:
                if (k.city) {
                    m_city = k.city;
                    m_page = Page::Pirates;
                }
                break;
            case kRouteEditor:
                // 0x0044EEC0: opening the editor drops the stops that are no city (patrol points).
                for (std::uint8_t stop = 0; stop < sim::TradeRoute::kStops; ++stop) {
                    const sim::Target& t = ship.route.stops[stop].target;
                    if (!t.empty() && t.kind != sim::Target::Kind::City) {
                        sim::RouteEdit o;
                        o.player = c.local;
                        o.ship = ship.id;
                        o.op = sim::RouteEdit::Op::ClearStop;
                        o.stop = stop;
                        out.emplace_back(o);
                    }
                }
                m_page = Page::Route;
                break;
            default:
                if (e.id >= kHoldButton && e.id < kHoldButton + 8) {
                    const auto h = static_cast<std::size_t>(e.id - kHoldButton);
                    if (h >= ship.cargo.size()) {
                        break;
                    }
                    const sim::CargoSlot& slot = ship.cargo[h];
                    if (slot.ware >= kFirstSoldierWare && slot.ware <= kLastSoldierWare && slot.amount != 0) {
                        // Soldiers in the hold go ashore (0x00448F90).
                        sim::UnloadOrder o;
                        o.player = c.local;
                        o.ship = ship.id;
                        o.hold = static_cast<std::uint8_t>(h);
                        out.emplace_back(o);
                    } else if (slot.ware == kCannonWare && slot.amount >= 32) {
                        sim::ShipCommand o = ship_command(ship, c, sim::ShipCommand::Op::MountCannon);
                        o.hold = static_cast<std::uint8_t>(h);
                        out.emplace_back(o);
                    }
                }
                break;
        }
    }

    // Transfer: the wares of the Kontor in four groups, four at a time.
    struct WareRange {
        std::uint8_t first;
        std::uint8_t last;
    };
    static WareRange category_range(std::int32_t cat) {
        switch (cat) {
            case 0:
                return {22, 24};  // Baumaterial
            case 1:
                return {11, 13};  // Waffen
            case 2:
                return {14, 21};  // Bedarfsgüter
            default:
                return {2, 10};  // Rohstoffe
        }
    }
    static std::int32_t category_of(std::uint8_t w) {
        for (std::int32_t c = 0; c < 4; ++c) {
            const auto [first, last] = category_range(c);
            if (w >= first && w <= last) {
                return c;
            }
        }
        return 0;
    }
    [[nodiscard]] std::int32_t category() const {
        for (std::int32_t k = 0; k < 4; ++k) {
            if (m_transfer.selected(45141 + k)) {
                return k;
            }
        }
        return 0;
    }
    /// The ware on Kontor button k (0..3), 0 if none.
    [[nodiscard]] std::uint8_t kontor_ware(std::int32_t k) const {
        const std::int32_t cat = category();
        const auto [first, last] = category_range(cat);
        const std::int32_t w = first + m_scroll[static_cast<std::size_t>(cat)] + k;
        return w <= last ? static_cast<std::uint8_t>(w) : 0;
    }
    void show_ware(std::uint8_t w) {
        const std::int32_t cat = category_of(w);
        for (std::int32_t k = 0; k < 4; ++k) {
            m_transfer.set_selected(45141 + k, k == cat);
        }
        const auto [first, last] = category_range(cat);
        std::int32_t& scroll = m_scroll[static_cast<std::size_t>(cat)];
        if (last - first + 1 > 4) {
            scroll = std::clamp<std::int32_t>(w - first - 1, 0, last - first - 3);
        }
        for (std::int32_t k = 0; k < 4; ++k) {
            m_transfer.set_selected(45031 + k, kontor_ware(k) == w);
        }
    }

    void refresh_transfer(const sim::Ship& ship, const Context&, const PanelContext& c) {
        const sim::City* city = m_city ? c.world().city(m_city->first, m_city->second) : nullptr;
        if (city == nullptr) {
            m_page = Page::Root;
            return;
        }
        m_transfer.set_text(45122, ship.name);
        m_transfer.set_text(45124, city->name);
        const std::int32_t cat = category();
        const auto [first, last] = category_range(cat);
        const bool scrolls = last - first + 1 > 4;
        const std::int32_t scroll = m_scroll[static_cast<std::size_t>(cat)];
        m_transfer.set_visible(45007, scrolls && scroll > 0);
        m_transfer.set_visible(45008, scrolls && scroll < last - first - 3);
        const auto cap = static_cast<std::int32_t>(std::max<std::uint32_t>(city->capacity, 1));
        for (std::int32_t k = 0; k < 4; ++k) {
            const std::uint8_t w = kontor_ware(k);
            m_transfer.set_visible(45011 + k, w != 0);
            m_transfer.set_visible(45031 + k, w != 0);
            m_transfer.set_visible(45131 + k, w != 0);
            if (w == 0) {
                continue;
            }
            m_transfer.set_graphic(45011 + k, m_gfx_ware + w, 0);
            m_transfer.set_disabled(45011 + k, city->stock(w) == 0);
            m_transfer.set_slider(
                45131 + k, 1, static_cast<std::int32_t>(std::min<std::uint32_t>(city->stock(w), city->capacity)), cap);
        }
        holds(m_transfer, ship, c, 45051, 45091, 45071, true);
        for (std::int32_t h = 0; h < 8; ++h) {
            m_transfer.set_selected(45071 + h, h == m_hold);
        }
    }

    void clicked_transfer(const ui::PanelEvent& e, const sim::Ship& ship, const PanelContext& c,
                          std::vector<sim::Command>& out) {
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            return;
        }
        const std::uint32_t amount = step(m_transfer, 45003);
        const std::int32_t cat = category();
        const auto [first, last] = category_range(cat);
        std::int32_t& scroll = m_scroll[static_cast<std::size_t>(cat)];
        if (e.id == 45007) {
            scroll = std::max(0, scroll - 1);
        } else if (e.id == 45008) {
            scroll = std::min(scroll + 1, std::max(0, last - first - 3));
        } else if (e.id >= 45071 && e.id <= 45078) {
            m_hold = e.id - 45071;
            const auto h = static_cast<std::size_t>(m_hold);
            if (h < ship.cargo.size() && ship.cargo[h].ware >= 2 && ship.cargo[h].ware <= 24 &&
                ship.cargo[h].amount != 0) {
                show_ware(ship.cargo[h].ware);  // the Kontor shows the same ware
            }
        } else if (e.id == 45001) {
            std::uint8_t w = 0;
            for (std::int32_t k = 0; k < 4; ++k) {
                if (m_transfer.selected(45031 + k)) {
                    w = kontor_ware(k);
                }
            }
            if (w >= 2 && w <= 24) {
                sim::CargoTransfer o;
                o.player = c.local;
                o.ship = ship.id;
                o.ware = w;
                o.to_ship = true;
                o.amount = amount;
                out.emplace_back(o);
            }
        } else if (e.id == 45002) {
            const auto h = static_cast<std::size_t>(m_hold);
            if (h < ship.cargo.size() && ship.cargo[h].ware >= 2 && ship.cargo[h].ware <= 24 &&
                ship.cargo[h].amount != 0) {
                show_ware(ship.cargo[h].ware);
                sim::CargoTransfer o;
                o.player = c.local;
                o.ship = ship.id;
                o.ware = ship.cargo[h].ware;
                o.to_ship = false;
                o.amount = std::min(amount, ship.cargo[h].amount);
                o.hold = static_cast<std::uint8_t>(h);
                out.emplace_back(o);
            }
        }
    }

    // Trade.
    [[nodiscard]] static std::uint32_t sellable(const sim::City& city, std::uint8_t w) {
        const sim::City::Trade* t = city.trade_of(w);
        if (t == nullptr || !t->sell) {
            return 0;
        }
        const std::int64_t n = std::int64_t{city.stock(w)} - t->keep - city.reserved_of(w);
        return n < 32 ? 0 : static_cast<std::uint32_t>(n);
    }
    [[nodiscard]] static std::uint32_t buyable(const sim::City& city, std::uint8_t w) {
        const sim::City::Trade* t = city.trade_of(w);
        if (t == nullptr || !t->buy) {
            return 0;
        }
        const std::int64_t n = std::int64_t{t->limit} - city.stock(w) + city.reserved_of(w);
        return n < 32 ? 0 : static_cast<std::uint32_t>(n);
    }

    void refresh_trade(const sim::Ship& ship, const Context&, const PanelContext& c) {
        const sim::City* city = m_city ? c.world().city(m_city->first, m_city->second) : nullptr;
        if (city == nullptr) {
            m_page = Page::Root;
            return;
        }
        const bool natives = is_natives(*city, c);
        m_trade.set_text(33122, ship.name);
        m_trade.set_text(33124, city->name);
        const assets::BuildingType* kontor = sim::kontor_of(c.world(), c.buildings, *city);
        const std::int32_t places = std::min<std::int32_t>(kontor != nullptr ? kontor->max_wares : 8, 8);
        m_offers.clear();
        for (std::uint8_t w = 2; w <= 24; ++w) {
            if (sellable(*city, w) >= 32) {
                m_offers.push_back(w);
            }
        }
        const auto cap = static_cast<std::int32_t>(std::max<std::uint32_t>(city->capacity, 1));
        for (std::int32_t k = 0; k < 8; ++k) {
            const bool place = k < places;
            const bool ware = place && k < static_cast<std::int32_t>(m_offers.size());
            m_trade.set_visible(33011 + k, place);
            m_trade.set_visible(33031 + k, place);
            m_trade.set_visible(33131 + k, ware);
            m_trade.set_selected(33031 + k, k == m_offer);
            if (ware) {
                const std::uint8_t w = m_offers[static_cast<std::size_t>(k)];
                m_trade.set_graphic(33011 + k, m_gfx_ware + w, 0);
                m_trade.set_slider(
                    33131 + k, 1,
                    static_cast<std::int32_t>(std::min<std::uint32_t>(sellable(*city, w), city->capacity)), cap);
            } else if (place) {
                m_trade.set_graphic(33011 + k, m_gfx_empty, 0);
            }
        }
        const bool offer = m_offer < static_cast<std::int32_t>(m_offers.size());
        m_trade.set_visible(33111, offer);
        m_trade.set_visible(33112, offer && !natives);
        m_trade.set_visible(33113, offer && !natives);
        m_trade.set_disabled(33001, !offer);
        if (offer) {
            const std::uint8_t w = m_offers[static_cast<std::size_t>(m_offer)];
            m_trade.set_text(33111, c.text("WARE", w));
            const sim::City::Trade* t = city->trade_of(w);
            m_trade.set_text(33112, std::to_string(t != nullptr ? t->sell_price : 0));
        }
        holds(m_trade, ship, c, 33051, 33091, 33071, true);
        if (m_hold >= static_cast<std::int32_t>(ship.cargo.size())) {
            m_hold = 0;
        }
        for (std::int32_t h = 0; h < 8; ++h) {
            m_trade.set_selected(33071 + h, h == m_hold);
        }
        const auto h = static_cast<std::size_t>(m_hold);
        const std::uint8_t hw = h < ship.cargo.size() && ship.cargo[h].amount != 0 ? ship.cargo[h].ware : 0;
        const bool wanted = hw >= 2 && hw <= 24 && buyable(*city, hw) >= 32;
        m_trade.set_visible(33114, wanted);
        m_trade.set_visible(33115, wanted && !natives);
        m_trade.set_visible(33116, wanted && !natives);
        m_trade.set_disabled(33002, !wanted);
        if (wanted) {
            const sim::City::Trade* t = city->trade_of(hw);
            m_trade.set_text(33114, c.text("WARE", hw));
            m_trade.set_text(33115, std::to_string(t != nullptr ? t->buy_price : 0));
        }
    }

    void clicked_trade(const ui::PanelEvent& e, const sim::Ship& ship, const PanelContext& c,
                       std::vector<sim::Command>& out) {
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            return;
        }
        const std::uint32_t amount = step(m_trade, 33003);
        if (e.id >= 33031 && e.id <= 33038) {
            m_offer = e.id - 33031;
        } else if (e.id >= 33071 && e.id <= 33078) {
            m_hold = e.id - 33071;
        } else if (e.id == 33001 && m_offer < static_cast<std::int32_t>(m_offers.size())) {
            sim::CargoTransfer o;
            o.player = c.local;
            o.ship = ship.id;
            o.ware = m_offers[static_cast<std::size_t>(m_offer)];
            o.to_ship = true;
            o.amount = amount;  // the simulation takes what fits, what is offered and what is paid for
            out.emplace_back(o);
        } else if (e.id == 33002) {
            const auto h = static_cast<std::size_t>(m_hold);
            if (h < ship.cargo.size() && ship.cargo[h].ware >= 2 && ship.cargo[h].ware <= 24 &&
                ship.cargo[h].amount != 0) {
                sim::CargoTransfer o;
                o.player = c.local;
                o.ship = ship.id;
                o.ware = ship.cargo[h].ware;
                o.to_ship = false;
                o.amount = std::min(amount, ship.cargo[h].amount);
                o.hold = static_cast<std::uint8_t>(h);
                out.emplace_back(o);
            }
        }
    }

    // ---- The trade route (ROUTE, pickers LAGER and HANDEL) ----

    [[nodiscard]] static sim::RouteEdit route_stop(const sim::Ship& ship, const PanelContext& c, std::uint8_t stop,
                                                   const sim::Target& target) {
        sim::RouteEdit o;
        o.player = c.local;
        o.ship = ship.id;
        o.op = target.empty() ? sim::RouteEdit::Op::ClearStop : sim::RouteEdit::Op::SetStop;
        o.stop = stop;
        o.target = target;
        return o;
    }
    [[nodiscard]] static sim::RouteEdit route_entry(const sim::Ship& ship, const PanelContext& c, std::uint8_t stop,
                                                    std::uint8_t entry, const sim::RouteEntry& value) {
        sim::RouteEdit o;
        o.player = c.local;
        o.ship = ship.id;
        o.op = sim::RouteEdit::Op::SetEntry;
        o.stop = stop;
        o.entry = entry;
        o.value = value;
        return o;
    }
    [[nodiscard]] static const sim::City* stop_city(const sim::Ship& ship, const PanelContext& c, std::size_t stop) {
        const sim::Target& t = ship.route.stops[stop].target;
        return t.kind == sim::Target::Kind::City ? c.world().city(t.island, t.city) : nullptr;
    }
    /// Another's city without a trade treaty: its goods can not be chosen.
    [[nodiscard]] static bool locked(const sim::City& city, const PanelContext& c) {
        return city.owner != c.local && !c.world().diplomacy().may_trade(c.local, city.owner);
    }
    [[nodiscard]] static std::int32_t arms_of(std::int32_t base, const PanelContext& c, sim::PlayerId p) {
        const sim::Player* player = c.world().player(p);
        const std::int32_t banner = player != nullptr ? player->banner : 0;
        return base + (banner > 3 ? 0 : banner);
    }

    void refresh_route(const sim::Ship& ship, const PanelContext& c) {
        const auto cap = static_cast<std::int32_t>(sim::kHoldCapacity);
        for (std::uint8_t stop = 0; stop < sim::TradeRoute::kStops; ++stop) {
            const sim::City* city = stop_city(ship, c, stop);
            const auto s = static_cast<std::int32_t>(stop);
            m_route.set_text(kStopName + s, city != nullptr ? city->name : c.text("GAME", 46));
            m_route.set_visible(kStopArms + s, city != nullptr);
            m_route.set_visible(kStopArmsShadow + s, city != nullptr);
            m_route.set_selected(kStopName + s, m_pick_stop == stop);
            m_route.set_graphic(kStopSymbol + s, m_gfx_stop + (city != nullptr ? 0 : 1), 0);
            if (city != nullptr) {
                m_route.set_graphic(kStopArms + s, arms_of(m_gfx_arms, c, city->owner), 0);
                m_route.set_graphic(kStopArmsShadow + s, arms_of(m_gfx_arms, c, city->owner), 0);
            }
            const bool lock = city == nullptr || locked(*city, c);
            for (std::size_t e = 0; e < 2; ++e) {
                const sim::RouteEntry& entry = ship.route.stops[stop].entries[e];
                const bool off = city == nullptr || entry.ware == 0 || lock;
                m_route.set_graphic(kEntryIcon[e] + s,
                                    city != nullptr && entry.ware != 0 ? m_gfx_ware + entry.ware : m_gfx_empty, 0);
                m_route.set_visible(kEntryBar[e] + s, !off);
                m_route.set_disabled(kEntrySlider[e] + s, off);
                m_route.set_disabled(kEntryFrame[e] + s, lock);
                // The direction is fixed at another's city by what it buys and sells.
                m_route.set_disabled(kEntryArrow[e] + s, off || city->owner != c.local);
                m_route.set_graphic(kEntryArrow[e] + s, m_gfx_arrow + (entry.unload ? 2 : 0), 0);
                const auto amount = static_cast<std::int32_t>(std::min(entry.amount, sim::kHoldCapacity));
                m_route.set_slider(kEntryBar[e] + s, 1, off ? 0 : amount, cap);
                if (m_route.held() != kEntrySlider[e] + s) {
                    m_route.set_slider(kEntrySlider[e] + s, 1, off ? 0 : amount, cap);
                }
            }
        }
    }

    void clicked_route(const ui::PanelEvent& e, const sim::Ship& ship, const PanelContext& c,
                       std::vector<sim::Command>& out) {
        for (std::uint8_t stop = 0; stop < sim::TradeRoute::kStops; ++stop) {
            const auto s = static_cast<std::int32_t>(stop);
            const sim::City* city = stop_city(ship, c, stop);
            for (std::uint8_t k = 0; k < 2; ++k) {
                const sim::RouteEntry& entry = ship.route.stops[stop].entries[k];
                if (e.kind == ui::PanelEvent::Kind::Slid && e.id == kEntrySlider[k] + s && city != nullptr &&
                    entry.ware != 0 && !locked(*city, c)) {
                    // Whole 1/32 t like the original (no rounding to tons).
                    sim::RouteEntry v = entry;
                    v.amount = static_cast<std::uint32_t>(
                        m_route.slider(e.id, 1, static_cast<std::int32_t>(sim::kHoldCapacity)));
                    if (v.amount != entry.amount) {
                        out.emplace_back(route_entry(ship, c, stop, k, v));
                    }
                    return;
                }
            }
            if (e.kind != ui::PanelEvent::Kind::Clicked) {
                continue;
            }
            if (e.id == kStopName + s) {
                if (m_pick_stop && *m_pick_stop != stop && city != nullptr) {
                    // While choosing: another stop's name copies its city.
                    out.emplace_back(route_stop(ship, c, *m_pick_stop, ship.route.stops[stop].target));
                    m_pick_stop.reset();
                } else {
                    m_pick_stop = stop;
                }
                return;
            }
            if (e.id == kStopSymbol + s) {
                if (city == nullptr) {
                    m_pick_stop = stop;
                } else {
                    out.emplace_back(route_stop(ship, c, stop, sim::Target{}));
                }
                return;
            }
            for (std::uint8_t k = 0; k < 2; ++k) {
                const sim::RouteEntry& entry = ship.route.stops[stop].entries[k];
                if (e.id == kEntryFrame[k] + s && city != nullptr && !locked(*city, c)) {
                    m_pick_entry_stop = stop;
                    m_pick_entry = k;
                    m_pick_stop.reset();
                    m_page = city->owner == c.local ? Page::PickOwn : Page::PickForeign;
                    return;
                }
                if (e.id == kEntryArrow[k] + s && city != nullptr && city->owner == c.local && entry.ware != 0) {
                    sim::RouteEntry v = entry;
                    v.unload = !v.unload;
                    out.emplace_back(route_entry(ship, c, stop, k, v));
                    return;
                }
            }
        }
    }

    [[nodiscard]] std::optional<std::string> route_help(std::int32_t id, const sim::Ship& ship,
                                                        const PanelContext& c) const {
        for (std::uint8_t stop = 0; stop < sim::TradeRoute::kStops; ++stop) {
            const auto s = static_cast<std::int32_t>(stop);
            const sim::City* city = stop_city(ship, c, stop);
            if (id == kStopName + s) {
                return help_line(c, 170);
            }
            if (id == kStopSymbol + s) {
                return help_line(c, city != nullptr ? 171 : 170);
            }
            for (std::size_t k = 0; k < 2; ++k) {
                if (id != kEntryFrame[k] + s && id != kEntryArrow[k] + s && id != kEntrySlider[k] + s) {
                    continue;
                }
                const sim::RouteEntry& entry = ship.route.stops[stop].entries[k];
                if (city == nullptr || entry.ware == 0) {
                    return std::nullopt;
                }
                // RoutePanel_EntryText 0x00430B80: own city load/unload, another's buy/sell.
                const bool own = city->owner == c.local;
                const std::size_t line = own ? (entry.unload ? 53 : 51) : (entry.unload ? 55 : 54);
                return ui::format_text(c.text("GAME", line), {std::int64_t{entry.amount >> 5U},
                                                              std::string_view(c.text("WARE", entry.ware))});
            }
        }
        return std::nullopt;
    }

    void refresh_picker(const sim::Ship& ship, const PanelContext& c) {
        const sim::City* city = stop_city(ship, c, m_pick_entry_stop);
        if (city == nullptr || locked(*city, c)) {
            m_page = Page::Route;
            return;
        }
        if (m_page == Page::PickOwn) {
            m_stock.refresh(*city, true);
        } else {
            m_lists.refresh(*city, c);
        }
    }

    void clicked_picker(const ui::PanelEvent& e, const sim::Ship& ship, const PanelContext& c,
                        std::vector<sim::Command>& out) {
        const sim::City* city = stop_city(ship, c, m_pick_entry_stop);
        if (e.kind != ui::PanelEvent::Kind::Clicked || city == nullptr) {
            return;
        }
        sim::RouteEntry v = ship.route.stops[m_pick_entry_stop].entries[m_pick_entry];
        if (m_page == Page::PickOwn) {
            const std::optional<std::uint8_t> w = m_stock.ware_of(e.id);
            if (!w) {
                return;  // the original ignores clicks beside the list
            }
            // 50 t; delivered if the own Kontor buys the ware, taken if it sells it (0x0043C600).
            v.ware = *w;
            v.amount = *w != 0 ? sim::kHoldCapacity : 0;
            if (const sim::City::Trade* t = city->trade_of(*w); t != nullptr && *w != 0) {
                v.unload = t->buy ? true : t->sell ? false : v.unload;
            }
        } else {
            const auto w = m_lists.ware_of(e.id);
            if (!w) {
                return;
            }
            // What the city buys is delivered (50 t), what it sells is taken (25 t).
            v.ware = w->first;
            v.unload = w->second;
            v.amount = w->second ? sim::kHoldCapacity : sim::kHoldCapacity / 2;
        }
        out.emplace_back(route_entry(ship, c, m_pick_entry_stop, m_pick_entry, v));
        m_page = Page::Route;
    }

    [[nodiscard]] std::optional<std::string> picker_help(std::int32_t id, const sim::Ship& ship,
                                                         const PanelContext& c) const {
        const sim::City* city = stop_city(ship, c, m_pick_entry_stop);
        if (city == nullptr) {
            return std::nullopt;
        }
        if (m_page == Page::PickForeign) {
            const sim::Player* owner = c.world().player(city->owner);
            if (owner != nullptr && owner->role == sim::PlayerRole::Natives) {
                if (const auto w = m_lists.ware_of(id)) {
                    return ui::format_text(c.text("GAME", 50),
                                           {std::int64_t{TradeLists::amount(*city, w->first, w->second) >> 5U},
                                            std::string_view(c.text("WARE", w->first))});
                }
                return std::nullopt;
            }
            return m_lists.help(id, *city, c);
        }
        const std::optional<std::uint8_t> w = m_stock.ware_of(id);
        if (!w || *w == 0) {
            return std::nullopt;
        }
        const std::uint32_t t = city->stock(*w) >> 5U;
        return t >= 1 ? ui::format_text(c.text("GAME", 50), {std::int64_t{t}, std::string_view(c.text("WARE", *w))})
                      : c.text("WARE", *w);
    }

    // ---- The pirates (PIRAT, PIRATORD) ----

    [[nodiscard]] std::uint32_t pirate_cost(const PanelContext& c) const {
        const sim::City* city = m_city ? c.world().city(m_city->first, m_city->second) : nullptr;
        if (city == nullptr) {
            return 0;
        }
        if (!m_hire) {
            return sim::pirate_peace_price(c.world(), city->owner, c.local);
        }
        const auto k = static_cast<std::size_t>(m_hire_choice);
        return k < m_opponents.size() ? sim::pirate_bribe_price(c.world(), city->owner, c.local, m_opponents[k]) : 0;
    }

    void refresh_pirates(const PanelContext& c) {
        // The other human and computer players still in the game (0x00436C70).
        m_opponents.clear();
        const auto& players = c.world().players();
        for (std::size_t p = 0; p < players.size(); ++p) {
            const sim::Player& pl = players[p];
            if (p != c.local && (pl.role == sim::PlayerRole::Human || pl.role == sim::PlayerRole::Computer) &&
                pl.status == sim::PlayerStatus::Playing) {
                m_opponents.push_back(static_cast<sim::PlayerId>(p));
            }
        }
        if (m_page == Page::Pirates) {
            // Hiring them needs at least two players beside the pirates (0x00474D40).
            m_pirat.set_visible(kHire, m_opponents.size() + 1 >= 2);
            return;
        }
        m_piratord.set_text(kPirateText, c.text("GAME", m_hire ? 39 : 41));
        m_piratord.set_text(kPirateText + 1, c.text("GAME", m_hire ? 40 : 42));
        m_piratord.set_text(kPirateText + 2, c.text("GAME", 43));
        m_piratord.set_visible(kPirateText + 2, !m_hire);
        if (m_hire_choice >= static_cast<std::int32_t>(m_opponents.size())) {
            m_hire_choice = 0;
        }
        for (std::int32_t k = 0; k < 3; ++k) {
            const bool shown = m_hire && k < static_cast<std::int32_t>(m_opponents.size());
            m_piratord.set_visible(kPirateArms + k, shown);
            m_piratord.set_visible(kPirateChoice + k, shown);
            m_piratord.set_selected(kPirateChoice + k, k == m_hire_choice);
            if (shown) {
                m_piratord.set_graphic(kPirateArms + k,
                                       arms_of(m_gfx_pirate_arms, c, m_opponents[static_cast<std::size_t>(k)]), 0);
            }
        }
        m_piratord.set_text(kCostLabel, c.text("GAME", 45));
        m_piratord.set_text(kCost, std::to_string(pirate_cost(c)));
    }

    void clicked_pirates(const ui::PanelEvent& e, const sim::Ship& ship, const PanelContext& c,
                         std::vector<sim::Command>& out) {
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            return;
        }
        if (m_page == Page::Pirates) {
            if (e.id == kBuyPeace || e.id == kHire) {
                m_hire = e.id == kHire;
                m_page = Page::PirateOrder;
            }
            return;
        }
        if (e.id >= kPirateChoice && e.id < kPirateChoice + 3) {
            m_hire_choice = e.id - kPirateChoice;
            return;
        }
        const sim::City* city = m_city ? c.world().city(m_city->first, m_city->second) : nullptr;
        const sim::Player* me = c.world().player(c.local);
        if (e.id != kPay || city == nullptr || me == nullptr) {
            return;
        }
        const std::uint32_t cost = pirate_cost(c);
        if (std::int64_t{cost} > me->money) {
            return;  // the original does nothing without the money
        }
        sim::DiplomacyOrder o;
        o.player = c.local;
        o.partner = city->owner;
        o.ship = ship.id;
        o.kind = m_hire ? sim::DiplomacyOrder::Kind::Bribe : sim::DiplomacyOrder::Kind::BuyPeace;
        if (m_hire) {
            const auto k = static_cast<std::size_t>(m_hire_choice);
            if (k >= m_opponents.size()) {
                return;
            }
            o.target = m_opponents[k];
        }
        out.emplace_back(o);
        m_page = Page::Root;
    }

    ui::Panel m_ship;
    ui::Panel m_transfer;
    ui::Panel m_trade;
    ui::Panel m_route;
    ui::Panel m_pirat;
    ui::Panel m_piratord;
    StockList m_stock;
    TradeLists m_lists;
    std::int32_t m_gfx_stop = 612;
    std::int32_t m_gfx_arms = 548;
    std::int32_t m_gfx_arrow = 605;
    std::int32_t m_gfx_pirate_arms = 548;
    std::optional<std::uint8_t> m_pick_stop;  ///< the stop whose city a click on the map chooses
    std::uint8_t m_pick_entry_stop = 0;       ///< the entry whose ware a picker chooses
    std::uint8_t m_pick_entry = 0;
    bool m_hire = false;             ///< PIRATORD: hire them (or buy peace)
    std::int32_t m_hire_choice = 0;  ///< whom, among the other players
    std::vector<sim::PlayerId> m_opponents;
    std::int32_t m_gfx_ware = 192;
    std::int32_t m_gfx_empty = 192;
    std::int32_t m_gfx_ship = 363;
    static constexpr std::int32_t kNameFrame = 38001;
    /// The left column: repair, pirates, explore and its ring (arrange_column).
    static constexpr std::array<std::int32_t, 4> kColumn = {38003, 38019, 38023, 38024};
    std::array<std::array<std::int32_t, 2>, 4> m_column{};
    std::int32_t m_name_bottom = 384;
    Page m_page = Page::Root;
    std::uint32_t m_last_ship = 0;
    std::int32_t m_hold = 0;
    std::int32_t m_offer = 0;
    std::array<std::int32_t, 4> m_scroll{};
    std::vector<std::uint8_t> m_offers;
    std::optional<std::pair<std::uint16_t, std::uint8_t>> m_city;
    std::optional<Request> m_request;
};

}  // namespace

std::unique_ptr<SelectionPanel> make_ship_panel(ShipLayouts layouts) {
    return std::make_unique<ShipPanel>(std::move(layouts));
}

}  // namespace o1602::app
