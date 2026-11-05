// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/players.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include "o1602/editor/islands.hpp"
#include "o1602/sim/new_game.hpp"

namespace o1602::editor {

using core::ErrorCode;
using core::make_error;

namespace {

bool playing(const sim::Player& p) {
    return p.role == sim::PlayerRole::Human || p.role == sim::PlayerRole::Computer;
}

bool owns_anything(const sim::World& world, sim::PlayerId player) {
    return std::any_of(world.cities().begin(), world.cities().end(),
                       [&](const sim::City& c) { return c.owner == player; }) ||
           std::any_of(world.ships().begin(), world.ships().end(),
                       [&](const sim::Ship& s) { return s.owner == player; }) ||
           std::any_of(world.soldiers().begin(), world.soldiers().end(),
                       [&](const sim::Soldier& s) { return s.owner == player; });
}

/// `after` with `players`, the relations among the players `before` had kept (a new number of
/// players starts the diplomacy over, sim::World::set_players).
void keep_relations(const sim::World& before, sim::World& after, std::vector<sim::Player> players) {
    after.set_players(std::move(players));
    const std::size_t n = std::min(before.players().size(), after.players().size());
    for (std::size_t a = 0; a < n; ++a) {
        for (std::size_t b = 0; b < n; ++b) {
            after.diplomacy().relation(static_cast<sim::PlayerId>(a), static_cast<sim::PlayerId>(b)) =
                before.diplomacy().relation(static_cast<sim::PlayerId>(a), static_cast<sim::PlayerId>(b));
        }
    }
}

sim::FactionBehaviour behaviour_of(Neutral party) {
    switch (party) {
        case Neutral::Natives:
            return sim::FactionBehaviour::Natives;
        case Neutral::Pirates:
            return sim::FactionBehaviour::Pirates;
        case Neutral::Trader:
            break;
    }
    return sim::FactionBehaviour::Trader;
}

/// The slots of the party's behaviour (a package's faction may act as one too).
std::vector<sim::PlayerId> party_slots(const sim::World& world, Neutral party) {
    std::vector<sim::PlayerId> slots;
    for (std::size_t p = 0; p < world.players().size(); ++p) {
        const sim::Player& player = world.players()[p];
        if (player.role == sim::PlayerRole::Neutral && world.behaviour(player) == behaviour_of(party)) {
            slots.push_back(static_cast<sim::PlayerId>(p));
        }
    }
    return slots;
}

bool native_village(const sim::World& world, const sim::City& city) {
    const sim::Player* owner = world.player(city.owner);
    return owner != nullptr && owner->role == sim::PlayerRole::Neutral &&
           world.behaviour(*owner) == sim::FactionBehaviour::Natives;
}

}  // namespace

std::vector<sim::PlayerId> player_slots(const sim::World& world) {
    std::vector<sim::PlayerId> slots;
    for (std::size_t n = 0; n < sim::kMaxPlayers; ++n) {
        const sim::PlayerId slot = sim::player_slot(n);
        if (slot < world.players().size() && playing(world.players()[slot])) {
            slots.push_back(slot);
        }
    }
    return slots;
}

std::vector<sim::BannerId> player_banners() {
    std::vector<sim::BannerId> banners;
    for (std::size_t n = 0; n < sim::kMaxPlayers; ++n) {
        banners.push_back(static_cast<sim::BannerId>(
            n < sim::kOriginalPlayers ? n : sim::kFirstExtraBanner + (n - sim::kOriginalPlayers)));
    }
    return banners;
}

std::vector<sim::FactionId> slot_factions(const sim::World& world) {
    std::vector<sim::FactionId> factions;
    for (std::size_t f = 0; f < world.factions().size() && f < sim::kMaxFactions; ++f) {
        if (world.factions()[f].behaviour == sim::FactionBehaviour::Settlers) {
            factions.push_back(static_cast<sim::FactionId>(f));
        }
    }
    return factions;
}

PlayerValues player_values(const sim::Player& player) {
    PlayerValues v;
    v.role = player.role;
    v.banner = player.banner;
    v.money = player.money;
    v.name = player.name;
    v.monuments_allowed = player.monuments_allowed;
    v.arches_allowed = player.arches_allowed;
    v.faction = player.faction;
    return v;
}

core::Result<std::unique_ptr<Operation>> set_player(const sim::World& world, sim::PlayerId player,
                                                    const PlayerValues& values) {
    const sim::Player* p = world.player(player);
    if (p == nullptr || !playing(*p)) {
        return make_error(ErrorCode::NotFound, "no human or computer in slot " + std::to_string(player));
    }
    if (values.role != sim::PlayerRole::Human && values.role != sim::PlayerRole::Computer) {
        return make_error(ErrorCode::InvalidFormat, "a slot of the editor is a human's or a computer's");
    }
    if (!values.name.empty() && !sim::valid_name(values.name, kMaxPlayerName)) {
        return make_error(ErrorCode::InvalidFormat, "a player cannot be named so");
    }
    const auto banners = player_banners();
    if (std::find(banners.begin(), banners.end(), values.banner) == banners.end()) {
        return make_error(ErrorCode::InvalidFormat, "no player's banner " + std::to_string(values.banner));
    }
    if (values.money < kMinStartMoney || values.money > kMaxStartMoney || values.monuments_allowed > kMaxAllowed ||
        values.arches_allowed > kMaxAllowed) {
        return make_error(ErrorCode::InvalidFormat, "a value out of range");
    }
    if (const auto factions = slot_factions(world);
        values.faction != p->faction && std::find(factions.begin(), factions.end(), values.faction) == factions.end()) {
        return make_error(ErrorCode::InvalidFormat,
                          "no faction of the settlers' behaviour " + std::to_string(values.faction));
    }
    sim::World after = world;
    (void)sim::take_banner(after, player, values.banner);
    sim::Player* changed = after.player(player);
    if (changed == nullptr) {
        return make_error(ErrorCode::NotFound, "no player in slot " + std::to_string(player));
    }
    sim::Player& q = *changed;
    q.role = values.role;
    q.money = values.money;
    q.name = values.name;
    q.monuments_allowed = values.monuments_allowed;
    q.arches_allowed = values.arches_allowed;
    q.faction = values.faction;
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "player_change"));
}

core::Result<std::unique_ptr<Operation>> add_player(const sim::World& world) {
    const std::vector<sim::PlayerId> slots = player_slots(world);
    if (slots.size() >= sim::kMaxPlayers) {
        return make_error(ErrorCode::LimitExceeded,
                          "a map holds at most " + std::to_string(sim::kMaxPlayers) + " players");
    }
    // The first slot no human or computer has: 0 to 3, then 7 on, behind the trader, the pirates
    // and the natives.
    std::size_t n = 0;
    while (n < sim::kMaxPlayers && sim::player_slot(n) < world.players().size() &&
           playing(world.players()[sim::player_slot(n)])) {
        ++n;
    }
    const sim::PlayerId slot = sim::player_slot(n);
    std::vector<sim::Player> players = world.players();
    while (players.size() <= slot) {
        sim::Player absent;  // holds the place of the trader, the pirates or the natives
        absent.status = sim::PlayerStatus::Absent;
        players.push_back(std::move(absent));
    }
    sim::Player fresh;
    fresh.role = sim::PlayerRole::Computer;
    for (const sim::PlayerId s : slots) {
        if (world.players()[s].role == sim::PlayerRole::Computer) {
            fresh = world.players()[s];  // its money and unlocked levels, as sim::add_players
            break;
        }
    }
    fresh.role = sim::PlayerRole::Computer;
    fresh.status = sim::PlayerStatus::Playing;
    fresh.name.clear();
    fresh.won = false;
    fresh.mission = sim::kNoMission;
    fresh.faction = sim::faction::kSettlers;
    for (const sim::BannerId b : player_banners()) {
        const bool taken = std::any_of(slots.begin(), slots.end(),
                                       [&](sim::PlayerId s) { return s != slot && players[s].banner == b; });
        if (!taken) {
            fresh.banner = b;
            break;
        }
    }
    players[slot] = std::move(fresh);
    sim::World after = world;
    keep_relations(world, after, std::move(players));
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "player_add"));
}

core::Result<std::unique_ptr<Operation>> remove_last_player(const sim::World& world) {
    const std::vector<sim::PlayerId> slots = player_slots(world);
    if (slots.empty() || slots.back() < sim::kOriginalPlayers) {
        return make_error(ErrorCode::Unsupported, "the first four slots stay");
    }
    const sim::PlayerId last = slots.back();
    if (owns_anything(world, last)) {
        return make_error(ErrorCode::Unsupported, "the player still owns a city, ship or soldier");
    }
    std::vector<sim::Player> players = world.players();
    players.resize(last);
    // The empty slots that only held its place go with it.
    while (players.size() > sim::kOriginalPlayers && players.back().status == sim::PlayerStatus::Absent &&
           players.back().role != sim::PlayerRole::Neutral &&
           !owns_anything(world, static_cast<sim::PlayerId>(players.size() - 1))) {
        players.pop_back();
    }
    sim::World after = world;
    keep_relations(world, after, std::move(players));
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "player_remove"));
}

Relations relations(const sim::World& world, sim::PlayerId a, sim::PlayerId b) {
    const sim::Relation& r = world.diplomacy().relation(a, b);
    return Relations{r.peace == sim::Treaty::Signed, r.trade == sim::Treaty::Signed};
}

core::Result<std::unique_ptr<Operation>> set_relations(const sim::World& world, sim::PlayerId a, sim::PlayerId b,
                                                       Relations r) {
    const sim::Player* pa = world.player(a);
    const sim::Player* pb = world.player(b);
    if (pa == nullptr || pb == nullptr || !playing(*pa) || !playing(*pb)) {
        return make_error(ErrorCode::NotFound, "relations only between humans and computers");
    }
    if (a == b) {
        return make_error(ErrorCode::InvalidFormat, "a player has no relations with himself");
    }
    if (r.trade && !r.peace) {
        return make_error(ErrorCode::InvalidFormat, "a trade treaty needs peace");
    }
    sim::World after = world;
    for (const auto& [x, y] : {std::pair{a, b}, std::pair{b, a}}) {
        sim::Relation& rel = after.diplomacy().relation(x, y);
        rel.peace = r.peace ? sim::Treaty::Signed : sim::Treaty::None;
        rel.trade = r.trade ? sim::Treaty::Signed : sim::Treaty::None;
        rel.tribute = 0;
        rel.trade_mood = 0;
        rel.damage = 0;
    }
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "relations_change"));
}

std::vector<Neutral> neutral_parties(const sim::World& world) {
    std::vector<Neutral> parties;
    for (const Neutral party : {Neutral::Natives, Neutral::Pirates, Neutral::Trader}) {
        if (!party_slots(world, party).empty()) {
            parties.push_back(party);
        }
    }
    return parties;
}

bool has_native_villages(const sim::World& world) {
    return std::any_of(world.cities().begin(), world.cities().end(),
                       [&](const sim::City& c) { return native_village(world, c); });
}

NeutralRelations neutral_relations(const sim::World& world, sim::PlayerId player, Neutral party) {
    NeutralRelations r;
    const sim::Player* p = world.player(player);
    if (p == nullptr) {
        return r;
    }
    if (party == Neutral::Trader) {
        r.trade = !p->scenario.has(sim::scenario_flag::kNoTrader);
        return r;
    }
    const std::vector<sim::PlayerId> slots = party_slots(world, party);
    if (slots.empty()) {
        return r;
    }
    const sim::Relation& rel = world.diplomacy().relation(player, slots.front());
    r.peace = rel.peace == sim::Treaty::Signed;
    r.trade = rel.trade == sim::Treaty::Signed;
    if (party == Neutral::Natives) {
        for (const sim::City& c : world.cities()) {
            if (native_village(world, c)) {
                // A village without an entry for the player gets the start's when the game begins.
                r.goodwill = player < c.goodwill.size() ? c.goodwill[player] : sim::kNativeGoodwillStart;
                break;
            }
        }
    }
    return r;
}

core::Result<std::unique_ptr<Operation>> set_neutral_relations(const sim::World& world, sim::PlayerId player,
                                                               Neutral party, const NeutralRelations& r) {
    const sim::Player* p = world.player(player);
    const std::vector<sim::PlayerId> slots = party_slots(world, party);
    if (p == nullptr || !playing(*p) || slots.empty()) {
        return make_error(ErrorCode::NotFound, "relations only of a human or computer with a party the world has");
    }
    if (party == Neutral::Natives && r.trade && !r.peace) {
        return make_error(ErrorCode::InvalidFormat, "the natives trade only at peace");
    }
    if (party == Neutral::Trader && !r.peace) {
        return make_error(ErrorCode::InvalidFormat, "the trader is at peace with everyone");
    }
    if (r.goodwill > sim::kMaxNativeGoodwill) {
        return make_error(ErrorCode::InvalidFormat, "goodwill out of range");
    }
    sim::World after = world;
    if (party == Neutral::Trader) {
        sim::Player& changed = *after.player(player);
        changed.scenario.flags = r.trade ? changed.scenario.flags & ~sim::scenario_flag::kNoTrader
                                         : changed.scenario.flags | sim::scenario_flag::kNoTrader;
        return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "relations_change"));
    }
    sim::Diplomacy& d = after.diplomacy();
    const sim::Treaty peace = r.peace ? sim::Treaty::Signed : sim::Treaty::None;
    const sim::Treaty trade = r.trade ? sim::Treaty::Signed : sim::Treaty::None;
    for (const sim::PlayerId slot : slots) {
        for (const auto& [x, y] : {std::pair{player, slot}, std::pair{slot, player}}) {
            sim::Relation& rel = d.relation(x, y);
            rel.peace = peace;
            rel.tribute = 0;
            rel.trade_mood = 0;
            rel.damage = 0;
        }
        d.relation(player, slot).trade = trade;
        if (party == Neutral::Natives) {
            d.relation(slot, player).trade = trade;
            continue;
        }
        // The pirates make war and peace by their mood (sim::pirates_reconsider): a peace lasts as
        // one the player bought, a war keeps nothing that would end it at once.
        std::array<sim::DiplomacyEvent, sim::Diplomacy::kEvents> events = d.events(slot);
        for (sim::DiplomacyEvent& e : events) {
            const bool good = e.type == sim::DiplomacyEventType::PeaceBought ||
                              e.type == sim::DiplomacyEventType::BribedBy ||
                              e.type == sim::DiplomacyEventType::Surrendered;
            if (e.partner == player && good) {
                e = sim::DiplomacyEvent{};
            }
        }
        d.set_events(slot, events);
        if (r.peace) {
            d.add_event(slot, player, sim::DiplomacyEventType::PeaceBought, after.time_ms());
        }
    }
    if (party == Neutral::Natives) {
        sim::fit_goodwill(after);
        for (const sim::City& c : world.cities()) {
            sim::City* village = after.city(c.island, c.index);
            if (village != nullptr && native_village(after, *village) && player < village->goodwill.size()) {
                village->goodwill[player] = r.goodwill;
            }
        }
    }
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "relations_change"));
}

}  // namespace o1602::editor
