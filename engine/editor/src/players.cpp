// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/players.hpp"

#include <algorithm>
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

PlayerValues player_values(const sim::Player& player) {
    PlayerValues v;
    v.role = player.role;
    v.banner = player.banner;
    v.money = player.money;
    v.name = player.name;
    v.monuments_allowed = player.monuments_allowed;
    v.arches_allowed = player.arches_allowed;
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
    sim::World after = world;
    (void)sim::take_banner(after, player, values.banner);
    sim::Player& q = *after.player(player);
    q.role = values.role;
    q.money = values.money;
    q.name = values.name;
    q.monuments_allowed = values.monuments_allowed;
    q.arches_allowed = values.arches_allowed;
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
    after.set_players(std::move(players));
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
    after.set_players(std::move(players));
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "player_remove"));
}

}  // namespace o1602::editor
