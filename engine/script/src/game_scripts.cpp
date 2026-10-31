// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/script/game_scripts.hpp"

#include <algorithm>
#include <cstdio>
#include <numeric>
#include <optional>
#include <span>

#include "o1602/core/file.hpp"
#include "o1602/core/hash.hpp"
#include "o1602/core/text.hpp"
#include "o1602/sim/simulation.hpp"

namespace o1602::script {

struct GameScripts::Package {
    std::string id;    ///< the package's
    std::string name;  ///< what its state is saved under and its notes name (PackageScript::name)
    Sandbox sandbox;
    sim::Random random;
    bool started = false;   ///< on_start ran, or the savegame kept a state
    bool stopped = false;   ///< failed at its top level or too often
    bool campaign = false;  ///< the script of a campaign's part: keeps the table `campaign`
    std::size_t errors = 0;

    Package(std::string package, std::string script_name, Limits limits)
        : id(std::move(package)), name(std::move(script_name)), sandbox(limits) {}
};

namespace {

std::span<const std::uint8_t> bytes_of(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

/// The generator of a package's math.random in a new game: from the world's generators, which are
/// the same on every computer, and the package's id, without drawing from them.
sim::Random new_random(const sim::World& world, std::string_view package) {
    const std::uint64_t id = core::xxh64(bytes_of(package));
    return sim::Random(world.random(sim::RandomStream::Events).state() ^ id, id);
}

/// An integer argument; nothing for another value.
std::optional<std::int64_t> integer(std::span<const Value> args, std::size_t i) {
    if (i < args.size()) {
        if (const auto* value = std::get_if<std::int64_t>(&args[i])) {
            return *value;
        }
    }
    return std::nullopt;
}

/// The ware number of a trade good: a number, "anno1602:<name or number>", "<package>:<name>", or a
/// name of the package's own goods.toml.
std::optional<std::uint8_t> ware_argument(const sim::World& world, std::string_view package,
                                          std::span<const Value> args, std::size_t i) {
    std::optional<std::uint8_t> ware;
    if (const auto number = integer(args, i)) {
        if (*number >= 0 && *number <= 255) {
            ware = static_cast<std::uint8_t>(*number);
        }
    } else if (i < args.size()) {
        if (const auto* id = std::get_if<std::string>(&args[i])) {
            ware = assets::ware_of(world.goods(),
                                   id->find(':') == std::string::npos ? std::string(package) + ":" + *id : *id);
        }
    }
    return ware && world.goods().is_trade(*ware) ? ware : std::nullopt;
}

/// The city `index` of island `island`; nullptr for none.
const sim::City* find_city(const sim::World& world, std::int64_t island, std::int64_t index) {
    if (island < 0 || island > 0xFFFF || index < 0 || index > 0xFF) {
        return nullptr;
    }
    return world.city(static_cast<std::uint16_t>(island), static_cast<std::uint8_t>(index));
}

/// The whole tons in `amount` (1/32 t), rounded down.
std::int64_t whole_tons(std::uint32_t amount) {
    return std::int64_t{amount / 32};
}

std::string text_of(const Value& value) {
    if (const auto* text = std::get_if<std::string>(&value)) {
        return *text;
    }
    if (const auto* number = std::get_if<std::int64_t>(&value)) {
        return std::to_string(*number);
    }
    if (const auto* number = std::get_if<double>(&value)) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.14g", *number);  // the C locale: a dot, as in Lua
        return buffer;
    }
    if (const auto* flag = std::get_if<bool>(&value)) {
        return *flag ? "true" : "false";
    }
    return {};
}

std::uint64_t inhabitants_of(const sim::World& world, sim::PlayerId player) {
    std::uint64_t sum = 0;
    for (const sim::City& city : world.cities()) {
        if (city.owner == player) {
            sum = std::accumulate(city.inhabitants.begin(), city.inhabitants.end(), sum);
        }
    }
    return sum;
}

Value player_table(const sim::World& world, sim::PlayerId id) {
    const sim::Player* player = world.player(id);
    if (player == nullptr) {
        return {};
    }
    const char* role = player->role == sim::PlayerRole::Human      ? "human"
                       : player->role == sim::PlayerRole::Computer ? "computer"
                                                                   : "neutral";
    return make_table({
        {"id", std::int64_t{id}},
        {"name", core::cp1252_to_utf8(player->name)},
        {"role", std::string(role)},
        {"money", std::int64_t{player->money}},
        {"inhabitants", static_cast<std::int64_t>(inhabitants_of(world, id))},
        {"playing", player->active()},
        {"won", player->won},
    });
}

Value city_table(const sim::City& city) {
    return make_table({
        {"island", std::int64_t{city.island}},
        {"city", std::int64_t{city.index}},
        {"name", core::cp1252_to_utf8(city.name)},
        {"player", city.owner != sim::kNoPlayer ? Value{std::int64_t{city.owner}} : Value{}},
    });
}

core::Error script_error(std::string message) {
    return core::make_error(core::ErrorCode::ScriptError, std::move(message));
}

}  // namespace

std::vector<PackageScript> read_package_scripts(std::span<const assets::ModPackage> packages,
                                                std::vector<std::string>& problems) {
    std::vector<PackageScript> scripts;
    for (const assets::ModPackage& p : packages) {
        const std::filesystem::path path = p.folder / std::filesystem::path(kScriptFile);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) {
            continue;
        }
        auto bytes = core::read_file(path);
        if (!bytes) {
            problems.push_back(p.id + "/" + std::string(kScriptFile) + ": " + bytes.error().message);
            continue;
        }
        const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        scripts.push_back(PackageScript{p.id, std::string(core::skip_utf8_bom(text)), {}});
    }
    return scripts;
}

std::optional<PackageScript> read_scenario_script(const assets::PackageScenario& scenario,
                                                  std::vector<std::string>& problems) {
    if (scenario.script.empty()) {
        return std::nullopt;
    }
    auto bytes = core::read_file(scenario.script);
    if (!bytes) {
        problems.push_back(scenario.id + ": its script: " + bytes.error().message);
        return std::nullopt;
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    return PackageScript{scenario.package, std::string(core::skip_utf8_bom(text)), scenario.id};
}

GameScripts::GameScripts(std::vector<PackageScript> scripts, const sim::World& world, Limits limits) : m_world(&world) {
    for (PackageScript& script : scripts) {
        std::string name = script.name.empty() ? script.package : std::move(script.name);
        auto package = std::make_unique<Package>(std::move(script.package), std::move(name), limits);
        Package& p = *package;
        m_packages.push_back(std::move(package));
        const auto& part = world.campaign_part();
        p.campaign = part && p.name == world.package_scenario();
        add_api(p);
        if (p.campaign) {
            (void)p.sandbox.add_value("game", "campaign", Value{part->campaign});
            (void)p.sandbox.add_value("game", "part", Value{static_cast<std::int64_t>(part->part) + 1});
        }

        const auto saved = std::find_if(world.script_states().begin(), world.script_states().end(),
                                        [&](const sim::ScriptState& s) { return s.package == p.name; });
        p.random = saved != world.script_states().end() ? saved->random : new_random(world, p.name);
        p.started = saved != world.script_states().end();

        if (const core::Status ran = p.sandbox.run(p.name, script.source); !ran) {
            fail(p, ran.error().message);
            p.stopped = true;
            continue;
        }
        if (saved != world.script_states().end()) {
            if (const core::Status loaded = p.sandbox.load_table(kStateTable, saved->data); !loaded) {
                fail(p, loaded.error().message);
            }
        }
        if (p.campaign) {
            // What the part before left, or what this part kept when it was saved.
            const auto& carried = saved != world.script_states().end() ? saved->campaign : part->carried;
            if (const core::Status loaded = p.sandbox.load_table(kCampaignTable, carried); !loaded) {
                fail(p, loaded.error().message);
            }
        }
    }
}

GameScripts::~GameScripts() = default;

void GameScripts::add_api(Package& package) {
    Sandbox& sandbox = package.sandbox;
    sandbox.set_random([&package] { return package.random.next(); });
    sandbox.set_print([this, &package](std::string_view line) {
        if (m_notes.size() < kMaxPendingMessages) {
            m_notes.push_back(ScriptNote{package.name, std::string(line), false});
        }
    });
    const auto add = [&sandbox](std::string_view name, Sandbox::HostFunction function) {
        (void)sandbox.add_function("game", name, std::move(function));
    };
    (void)sandbox.add_value("game", "package", Value{package.id});

    add("time", [this](std::span<const Value>) -> core::Result<Value> {
        return Value{static_cast<std::int64_t>(m_world->time_ms() / 1000)};
    });
    add("players", [this](std::span<const Value>) -> core::Result<Value> {
        std::vector<Value> ids;
        for (std::size_t p = 0; p < m_world->players().size(); ++p) {
            ids.emplace_back(static_cast<std::int64_t>(p));
        }
        return make_table({}, std::move(ids));
    });
    add("player", [this](std::span<const Value> args) -> core::Result<Value> {
        const auto id = integer(args, 0);
        if (!id) {
            return core::make_error(core::ErrorCode::ScriptError, "game.player: the player's number is missing");
        }
        if (*id < 0 || *id >= static_cast<std::int64_t>(m_world->players().size())) {
            return Value{};
        }
        return player_table(*m_world, static_cast<sim::PlayerId>(*id));
    });
    add("cities", [this](std::span<const Value> args) -> core::Result<Value> {
        const auto player = integer(args, 0);
        if (!args.empty() && !std::holds_alternative<std::monostate>(args[0]) && !player) {
            return script_error("game.cities: the player must be a number");
        }
        std::vector<Value> cities;
        for (const sim::City& city : m_world->cities()) {
            if (!player || city.owner == *player) {
                cities.push_back(city_table(city));
            }
        }
        return make_table({}, std::move(cities));
    });
    add("goods", [this, &package](std::span<const Value> args) -> core::Result<Value> {
        const auto island = integer(args, 0);
        const auto index = integer(args, 1);
        const auto ware = ware_argument(*m_world, package.id, args, 2);
        if (!island || !index) {
            return script_error("game.goods: the island and the city must be numbers");
        }
        if (!ware) {
            return script_error("game.goods: no such good");
        }
        const sim::City* city = find_city(*m_world, *island, *index);
        return city != nullptr ? Value{whole_tons(city->stock(*ware))} : Value{};
    });
    add("add_money", [this](std::span<const Value> args) -> core::Result<Value> {
        const auto player = integer(args, 0);
        const auto amount = integer(args, 1);
        if (m_simulation == nullptr) {
            return script_error("game.add_money: only while the game runs, not at the top level");
        }
        if (!player || !amount) {
            return script_error("game.add_money: the player and the amount must be integers");
        }
        if (*player < 0 || *player >= static_cast<std::int64_t>(m_world->players().size()) ||
            !m_simulation->script_money(static_cast<sim::PlayerId>(*player), *amount)) {
            return script_error("game.add_money: no such player");
        }
        return Value{std::int64_t{m_world->players()[static_cast<std::size_t>(*player)].money}};
    });
    add("add_goods", [this, &package](std::span<const Value> args) -> core::Result<Value> {
        const auto island = integer(args, 0);
        const auto index = integer(args, 1);
        const auto ware = ware_argument(*m_world, package.id, args, 2);
        const auto tons = integer(args, 3);
        if (m_simulation == nullptr) {
            return script_error("game.add_goods: only while the game runs, not at the top level");
        }
        if (!island || !index || !tons) {
            return script_error("game.add_goods: the island, the city and the tons must be integers");
        }
        if (!ware) {
            return script_error("game.add_goods: no such good");
        }
        const sim::City* city = find_city(*m_world, *island, *index);
        if (city == nullptr) {
            return script_error("game.add_goods: no such city");
        }
        // Whole tons only: as much as fits, or as much as nobody has reserved.
        constexpr std::int64_t kMaxTons = std::int64_t{1} << 40;
        const std::int64_t wanted = std::clamp(*tons, -kMaxTons, kMaxTons);
        const std::uint32_t room = city->capacity > city->stock(*ware) ? city->capacity - city->stock(*ware) : 0;
        const std::int64_t whole =
            wanted >= 0 ? std::min(wanted, whole_tons(room)) : -std::min(-wanted, whole_tons(city->available(*ware)));
        const auto changed = m_simulation->script_goods(city->island, city->index, *ware, whole * 32);
        return Value{changed ? *changed / 32 : std::int64_t{0}};
    });
    add("win", [this](std::span<const Value> args) -> core::Result<Value> {
        const auto player = integer(args, 0);
        if (m_simulation == nullptr) {
            return script_error("game.win: only while the game runs, not at the top level");
        }
        if (!player) {
            return script_error("game.win: the player's number is missing");
        }
        return Value{*player >= 0 && *player < static_cast<std::int64_t>(m_world->players().size()) &&
                     m_simulation->script_win(static_cast<sim::PlayerId>(*player))};
    });
    add("defeat", [this](std::span<const Value> args) -> core::Result<Value> {
        const auto player = integer(args, 0);
        if (m_simulation == nullptr) {
            return script_error("game.defeat: only while the game runs, not at the top level");
        }
        if (!player) {
            return script_error("game.defeat: the player's number is missing");
        }
        return Value{*player >= 0 && *player < static_cast<std::int64_t>(m_world->players().size()) &&
                     m_simulation->script_defeat(static_cast<sim::PlayerId>(*player))};
    });
    add("message", [this, &package](std::span<const Value> args) -> core::Result<Value> {
        const auto* id = args.empty() ? nullptr : std::get_if<std::string>(&args[0]);
        if (id == nullptr || id->empty()) {
            return core::make_error(core::ErrorCode::ScriptError, "game.message: the message's id is missing");
        }
        ScriptMessage message;
        // A name of the package's own messages.toml, or "<package>:<name>" of another package.
        message.id = id->find(':') == std::string::npos ? package.id + ":" + *id : *id;
        if (args.size() > 1) {
            if (const auto* table = std::get_if<std::shared_ptr<const Table>>(&args[1]); table != nullptr && *table) {
                for (const auto& [name, value] : (*table)->fields) {
                    message.args.emplace_back(name, text_of(value));
                }
            } else if (!std::holds_alternative<std::monostate>(args[1])) {
                return core::make_error(core::ErrorCode::ScriptError,
                                        "game.message: the placeholders must be a table of names");
            }
        }
        if (args.size() > 2 && !std::holds_alternative<std::monostate>(args[2])) {
            const auto player = integer(args, 2);
            if (!player || *player < 0 || *player >= static_cast<std::int64_t>(m_world->players().size())) {
                return core::make_error(core::ErrorCode::ScriptError, "game.message: no such player");
            }
            message.player = static_cast<sim::PlayerId>(*player);
        }
        if (m_messages.size() >= kMaxPendingMessages) {
            m_messages.erase(m_messages.begin());
        }
        m_messages.push_back(std::move(message));
        return Value{};
    });
}

void GameScripts::after_tick(sim::Simulation& simulation, const sim::TickEvents& events) {
    m_simulation = &simulation;
    for (const auto& package : m_packages) {
        dispatch(*package, events);
    }
    m_simulation = nullptr;
}

void GameScripts::dispatch(Package& package, const sim::TickEvents& events) {
    if (!package.started) {
        package.started = true;
        call(package, "on_start");
    }
    for (const sim::ConstructionEvent& e : events.construction) {
        if (e.kind != sim::ConstructionEvent::Kind::Placed || e.type == nullptr) {
            continue;
        }
        const sim::City* city = m_world->city(e.island, e.city);
        const Value args[] = {make_table({
            {"building", std::int64_t{e.type->id}},
            {"island", std::int64_t{e.island}},
            {"x", std::int64_t{e.x}},
            {"y", std::int64_t{e.y}},
            {"player", city != nullptr && city->owner != sim::kNoPlayer ? Value{std::int64_t{city->owner}} : Value{}},
        })};
        call(package, "on_building_placed", args);
    }
    for (const sim::HouseEvent& e : events.houses) {
        const char* kind = e.kind == sim::HouseEvent::Kind::Upgraded     ? "upgraded"
                           : e.kind == sim::HouseEvent::Kind::Downgraded ? "downgraded"
                                                                         : "abandoned";
        const Value args[] = {make_table({
            {"kind", std::string(kind)},
            {"class", std::int64_t{e.resident_class}},
            {"island", std::int64_t{e.island}},
            {"x", std::int64_t{e.x}},
            {"y", std::int64_t{e.y}},
        })};
        call(package, "on_house_changed", args);
    }
    for (const sim::GameEvent& e : events.game) {
        const auto player_or_nil = [](sim::PlayerId p) {
            return p == sim::kNoPlayer ? Value{} : Value{std::int64_t{p}};
        };
        if (e.kind == sim::GameEvent::Kind::MissionAccomplished) {
            const Value args[] = {player_or_nil(e.player)};
            call(package, "on_player_won", args);
        } else if (e.kind == sim::GameEvent::Kind::PlayerDefeated) {
            const Value args[] = {player_or_nil(e.player), player_or_nil(e.by)};
            call(package, "on_player_defeated", args);
        } else if (e.kind == sim::GameEvent::Kind::GameOver) {
            const Value args[] = {player_or_nil(e.player)};
            call(package, "on_game_over", args);
        }
    }
    if (m_world->time_ms() % 1000 == 0) {
        const Value args[] = {Value{static_cast<std::int64_t>(m_world->time_ms() / 1000)}};
        call(package, "on_second", args);
    }
}

void GameScripts::call(Package& package, std::string_view function, std::span<const Value> args) {
    if (package.stopped || !package.sandbox.has_function(function)) {
        return;
    }
    if (auto result = package.sandbox.call(function, args); !result) {
        fail(package, result.error().message);
    }
}

void GameScripts::fail(Package& package, const std::string& message) {
    ++package.errors;
    if (m_notes.size() < kMaxPendingMessages) {
        m_notes.push_back(ScriptNote{package.name, message, true});
    }
    if (package.errors >= kMaxScriptErrors && !package.stopped) {
        package.stopped = true;
        if (m_notes.size() < kMaxPendingMessages) {
            m_notes.push_back(ScriptNote{
                package.name, "stopped for the rest of the game after " + std::to_string(kMaxScriptErrors) + " errors",
                true});
        }
    }
}

std::vector<sim::ScriptState> GameScripts::save_states() {
    std::vector<sim::ScriptState> states;
    for (const auto& package : m_packages) {
        sim::ScriptState state;
        state.package = package->name;
        state.random = package->random;
        if (auto data = package->sandbox.save_table(kStateTable)) {
            state.data = std::move(data).value();
        } else if (m_notes.size() < kMaxPendingMessages) {
            // Not counted as a failure: saving also happens on one computer alone (a player saves),
            // and the scripts must go on the same everywhere.
            m_notes.push_back(ScriptNote{package->name, data.error().message, true});
        }
        if (package->campaign) {
            if (auto data = package->sandbox.save_table(kCampaignTable)) {
                state.campaign = std::move(data).value();
            } else if (m_notes.size() < kMaxPendingMessages) {
                m_notes.push_back(ScriptNote{package->name, data.error().message, true});
            }
        }
        states.push_back(std::move(state));
    }
    return states;
}

std::vector<ScriptMessage> GameScripts::take_messages() {
    return std::exchange(m_messages, {});
}

std::vector<ScriptNote> GameScripts::take_notes() {
    return std::exchange(m_notes, {});
}

std::vector<std::string> GameScripts::packages() const {
    std::vector<std::string> ids;
    for (const auto& package : m_packages) {
        ids.push_back(package->name);
    }
    return ids;
}

}  // namespace o1602::script
