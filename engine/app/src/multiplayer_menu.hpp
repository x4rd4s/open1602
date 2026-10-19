// SPDX-License-Identifier: GPL-3.0-or-later
//
// The multiplayer menu (ADR 0011): Open1602's own, the original's network screens are not used.
// Three screens over the whole window: finding a game (the local network searched by itself,
// remembered hosts, an address typed in), creating one (an endless game, a scenario or a
// savegame, and the session's settings) and the lobby (slots, ready, chat, start). The menu shows
// what the viewer puts into the MultiplayerModel and answers with MultiplayerEvents; the session
// and the games are the viewer's.
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app_settings.hpp"
#include "main_menu.hpp"
#include "multiplayer_texts.hpp"

#include "o1602/net/lan.hpp"
#include "o1602/net/session.hpp"
#include "o1602/sim/new_game.hpp"
#include "o1602/ui/form.hpp"

namespace o1602::app {

/// The game a host sets up.
struct HostSetup {
    enum class Kind : std::uint8_t { Endless, Scenario, Savegame };
    Kind kind = Kind::Endless;
    std::uint8_t level = 1;  ///< endless game: the difficulty, 0..3 ([ENDLESSKIND])
    std::uint32_t seed = 0;  ///< endless game: the world's number
    /// Endless game: players with a start (kMinPlayers..sim::kMaxPlayers); the computer plays the
    /// slots no person takes. Four play on the original's maps; more get a world of their own,
    /// sized for them.
    std::uint8_t players = 4;
    static constexpr std::uint8_t kMinPlayers = 4;
    std::filesystem::path file;  ///< scenario
    std::size_t slot = 0;        ///< savegame: its slot
    std::string file_name;       ///< its name for the lobby (Windows-1252)
    std::string title;           ///< the game's name
    std::string password;
    bool lan = true;  ///< answer searches on the local network
    bool join_running = true;
    bool spectators = true;
    bool dedicated = false;  ///< the host plays no slot
    std::uint16_t port = net::kDefaultPort;
};

/// A line of the chat, or a notice of the session (someone came or went).
struct ChatLine {
    std::string name;  ///< empty for a notice
    std::string text;
    sim::PlayerId seat = sim::kNoPlayer;
    std::uint64_t time_ms = 0;  ///< when it came (real time)
};

/// What the menu shows; the viewer keeps it up to date.
struct MultiplayerModel {
    std::string name;                                            ///< the player's name
    std::vector<net::FoundGame> found;                           ///< games that answered the search
    std::vector<std::string> level_names;                        ///< [ENDLESSKIND]
    std::vector<MenuScenario> scenarios;                         ///< the scenario list of the main menu
    std::vector<std::pair<std::size_t, std::string>> savegames;  ///< slot and name
    const net::Session* session = nullptr;                       ///< the session, once hosting or joining
    bool hosting = false;
    bool everyone_ready = false;             ///< (host) every other player is ready
    std::uint16_t port = net::kDefaultPort;  ///< (host) the game's port
    std::deque<ChatLine> chat;               ///< newest last
};

struct MultiplayerEvent {
    enum class Kind : std::uint8_t {
        Close,       ///< back to the main menu
        Search,      ///< look for games again
        Join,        ///< connect to `address`:`port` with `password`
        Host,        ///< open a lobby for `setup`
        Leave,       ///< out of the session (back to the search)
        Start,       ///< (host) start the game
        TakeSeat,    ///< take slot `seat` (kNoPlayer: watch)
        SetSeat,     ///< (host) slot `seat` becomes `seat_kind`
        Kick,        ///< (host) send the player of `seat` away
        KickMember,  ///< (host) send the computer of roster entry `member` away (a spectator too)
        Ready,       ///< ready (`ready`) or not
        Say,         ///< chat `text`
        Name,        ///< the player's name changed to `text`
    };
    Kind kind = Kind::Close;
    std::string address{};
    std::uint16_t port = net::kDefaultPort;
    std::string password{};
    HostSetup setup{};
    sim::PlayerId seat = sim::kNoPlayer;
    std::size_t member = 0;
    net::LobbySeat::Kind seat_kind = net::LobbySeat::Kind::Open;
    bool ready = false;
    std::string text{};
};

/// "name", "name:port", "1.2.3.4:port": the host and its port (kDefaultPort if none is given);
/// nothing for an empty or malformed address.
[[nodiscard]] std::optional<std::pair<std::string, std::uint16_t>> parse_address(std::string_view text);

class MultiplayerMenu {
public:
    enum class Screen : std::uint8_t { Browse, Create, Lobby, Connecting };

    explicit MultiplayerMenu(ui::FormStyle style);

    [[nodiscard]] Screen screen() const noexcept { return m_screen; }
    /// The search screen; `notice` in red below the list (why a session ended).
    void open(std::string notice = {});
    void show_connecting(std::string address);
    void show_lobby();
    void set_scale(double scale) { m_form.set_scale(scale); }
    /// Colours (palette) of the banners, by banner id: a player's slot shows the banner of the
    /// same number (sim::kFirstExtraBanner); `other` for slots beyond them.
    void set_banner_colours(std::vector<std::uint8_t> colours, std::uint8_t other) {
        m_banners = std::move(colours);
        m_other_banner = other;
    }

    /// Everything to draw for the model, relative to the 1024 x 768 design's top left.
    [[nodiscard]] std::vector<ui::DrawItem> draw(const MultiplayerModel& model,
                                                 const std::array<const ui::Font*, 3>& fonts);
    std::vector<MultiplayerEvent> mouse(const MultiplayerModel& model, std::int32_t x, std::int32_t y, bool down);
    /// Escape: one screen back. Close on the search screen, Leave in a session; nothing when the
    /// menu went back by itself (from creating a game to the search).
    std::optional<MultiplayerEvent> back();
    /// The name in the name field (typed, or the model's).
    [[nodiscard]] const std::string& name() const noexcept { return m_name; }

    [[nodiscard]] bool typing() const noexcept { return m_form.focus().has_value(); }
    void type(std::string_view text);
    /// Backspace 8, Tab 9 (next field), Enter 13 (sends the chat, connects), Escape 27.
    std::vector<MultiplayerEvent> key(const MultiplayerModel& model, int key);

private:
    [[nodiscard]] static std::string tr(MpText id) { return mp_text(id); }
    [[nodiscard]] static std::string tr(MpText id, std::string_view value) { return mp_text(id, value); }
    void layout(const MultiplayerModel& model);
    void layout_browse(const MultiplayerModel& model);
    void layout_create(const MultiplayerModel& model);
    void layout_lobby(const MultiplayerModel& model);
    void layout_connecting();
    void click(const MultiplayerModel& model, ui::Form::Id id, std::vector<MultiplayerEvent>& out);
    /// The text of a field by its Id, nullptr if the Id is no field.
    [[nodiscard]] std::string* field_text(ui::Form::Id id);
    [[nodiscard]] std::size_t field_limit(ui::Form::Id id) const;
    /// The rows of the search list: the games found.
    struct BrowseRow {
        std::string address;  ///< "host:port" to join
        const net::FoundGame* game = nullptr;
    };
    [[nodiscard]] std::vector<BrowseRow> browse_rows(const MultiplayerModel& model) const;
    std::optional<MultiplayerEvent> join_event(const std::string& address) const;
    [[nodiscard]] std::string state_text(const net::GameInfo& info) const;

    ui::Form m_form;
    Screen m_screen = Screen::Browse;
    [[nodiscard]] std::uint8_t banner_colour(std::size_t seat) const noexcept {
        return seat < m_banners.size() ? m_banners[seat] : m_other_banner;
    }

    std::vector<std::uint8_t> m_banners;
    std::uint8_t m_other_banner = 0;
    std::string m_notice;      ///< on the search screen, why the last session ended
    std::string m_connecting;  ///< the address being connected to
    // Search
    std::string m_name;
    bool m_name_known = false;  ///< m_name was taken from the model
    std::string m_address;
    std::string m_join_password;
    std::optional<std::string> m_selected;  ///< address of the chosen row
    std::size_t m_browse_first = 0;
    // Create
    HostSetup m_setup;
    std::string m_seed_text;
    std::string m_port_text;
    std::size_t m_list_first = 0;
    std::optional<std::size_t> m_chosen;  ///< scenario index or savegame slot
    // Lobby
    std::string m_chat;
    bool m_ready = false;
    std::vector<std::size_t> m_watchers;  ///< (host) roster entries of the spectators shown with a kick button
};

}  // namespace o1602::app
