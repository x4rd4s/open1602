// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the local player is told of the game: the simulation's events become the original's
// messages (MELDUNG) with their voices and videos, lines in the log, entries in the high score
// lists and, once a game is decided, the end screen. Also the short notes of the status line and
// the lines of the multiplayer chat. Needs no platform, so that the tests have it: the game plays
// and shows the notices it returns (MapViewer::show_notices).
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "multiplayer_menu.hpp"

#include "o1602/sim/score.hpp"
#include "o1602/sim/simulation.hpp"

namespace o1602::app {

/// A message for the local player (GameSound::message): its voice, its icon in the status bar and
/// its video.
struct Announcement {
    std::uint16_t message = 0;
    std::optional<std::pair<double, double>> tile;  ///< where it happened, in world tiles
    std::optional<sim::PlayerId> about;             ///< the player it is about, if not the local one
    std::uint8_t tracks = 0;                        ///< the tracks of its video (bit i = track i, 0 = all)
};

/// A video by itself, whatever the video switches of the messages say (the defeats).
struct DirectVideo {
    std::uint16_t number = 0;
};

/// The high score list takes every human still playing and `defeated`, who is out at this moment
/// (HighScore_UpdateAllHumans 0x00475990); `progress`: a won single player game opens the next
/// mission.
struct ScoreUpdate {
    sim::PlayerId defeated = sim::kNoPlayer;
    bool progress = false;
};

using Notice = std::variant<Announcement, DirectVideo, ScoreUpdate>;

/// The help bar of the original over grass and trees of an explored island (0x0040A3D0): the
/// three crops of the climate (north sugar cane, grapes, tobacco; south spices, cotton, cocoa)
/// with their fertility in percent.
[[nodiscard]] std::array<std::pair<std::uint8_t, std::uint32_t>, 3> island_crops(const sim::WorldIsland& island);
/// The same for the log and the window title.
[[nodiscard]] std::string crops_text(const sim::WorldIsland& island);

/// A line in the multiplayer chat (Windows-1252, as drawn), also in the log unless `echo` is off;
/// the chat keeps the last kChatKept lines. `chat` may be null (no panel): then only the log.
void add_chat_line(std::deque<ChatLine>* chat, ChatLine line, bool echo = true);
/// Lines the chat keeps.
inline constexpr std::size_t kChatKept = 100;

class EventPresenter {
public:
    /// How the log names a player ("player 1 red (human)").
    using PlayerLabel = std::function<std::string(const sim::Player& player, sim::PlayerId id)>;
    explicit EventPresenter(PlayerLabel label);

    /// The end screen comes this long after the message of a decided game when it has no video,
    /// after an own defeat without one the shorter time (docs/re/missions.md "Sieg"; with a video
    /// it comes when the video ends).
    static constexpr std::uint64_t kEndScreenDelayMs = 5000;
    static constexpr std::uint64_t kDefeatDelayMs = 500;
    /// How long a note stays in the status line.
    static constexpr std::uint64_t kNoticeMs = 4000;

    // What the simulation reported, for the local player `local`, in the order to play it.

    /// Hunger and monuments (MELDUNG 0x33, 0x31); shortages and new build levels only in the log.
    [[nodiscard]] std::vector<Notice> city_events(const sim::World& world, sim::PlayerId local,
                                                  const std::vector<sim::CityEvent>& events) const;
    /// Discoveries and exploring of the local player's ships, in the log (their voices come from
    /// GameSound::ships).
    void ship_events(const sim::World& world, sim::PlayerId local, const std::vector<sim::ShipEvent>& events) const;
    /// Fires and plagues in the player's cities (MELDUNG 2, 1).
    [[nodiscard]] std::vector<Notice> disaster_events(const sim::World& world, sim::PlayerId local,
                                                      const std::vector<sim::DisasterEvent>& events) const;
    /// Droughts on islands where the player has a city (MELDUNG 3).
    [[nodiscard]] std::vector<Notice> island_events(const sim::World& world, sim::PlayerId local,
                                                    const std::vector<sim::IslandEvent>& events) const;
    /// Volcanoes (MELDUNG 0x5E), for players who have discovered the island.
    [[nodiscard]] std::vector<Notice> volcano_events(const sim::World& world, sim::PlayerId local,
                                                     const std::vector<sim::VolcanoEvent>& events) const;
    /// What building and demolishing did, in the log.
    void command_results(const std::vector<sim::CommandResult>& results) const;
    /// A rival founded a city (MELDUNG 0x0E, its icon in the rival's colour).
    [[nodiscard]] std::vector<Notice> construction_events(const sim::World& world, sim::PlayerId local,
                                                          const std::vector<sim::ConstructionEvent>& events) const;
    /// Offers, treaties and broken peace (MELDUNG 0x4D..0x52).
    [[nodiscard]] std::vector<Notice> diplomacy_notices(sim::PlayerId local,
                                                        const std::vector<sim::DiplomacyNotice>& notices) const;
    /// Victory, defeat and the end of the game: messages, videos, high scores, and the end screen
    /// that follows (`now_ms`: real time).
    [[nodiscard]] std::vector<Notice> game_events(const sim::World& world, sim::PlayerId local,
                                                  const std::vector<sim::GameEvent>& events, std::uint64_t now_ms);

    // The end screen of a decided game.

    /// The end screen follows a decided game when the videos of its messages are over (`videos`:
    /// one plays or waits) and the delay ran out while the game was not paused (docs/re/missions.md
    /// "Sieg"). Called every frame the game runs; returns the outcome once it is due.
    [[nodiscard]] std::optional<sim::GameOutcome> end_screen_due(std::uint64_t now_ms, bool videos, bool game_paused);
    /// The final end screen was shown: once per game (a won game may go on).
    void end_screen_shown() noexcept { m_end_shown = true; }
    /// Back to the menu: no end screen follows any more.
    void drop_end_screen() noexcept { m_end_pending.reset(); }
    /// Another game runs: its end screen is still to come.
    void reset_game() noexcept {
        m_end_pending.reset();
        m_end_shown = false;
    }

    // The status line.

    /// A short note in the status line for a few seconds and in the log (Windows-1252, from ui::tr).
    void notify(std::string text, std::uint64_t now_ms);
    /// The note to show at `now_ms`, if one is still there.
    [[nodiscard]] const std::string* notice(std::uint64_t now_ms) const noexcept {
        return now_ms < m_notice_until_ms ? &m_notice : nullptr;
    }

private:
    struct EndPending {
        sim::GameOutcome outcome = sim::GameOutcome::Lost;
        /// Real time left while the game runs: the original waits game speed * 5 s of game time.
        std::uint64_t left_ms = 0;
        std::uint64_t last_ms = 0;  ///< real time of the last check
        bool after_video = false;   ///< a video played since: the screen follows when it ends
    };
    /// The end screen is to follow, unless one is coming or was shown already.
    void expect_end_screen(sim::GameOutcome outcome, std::uint64_t delay_ms, std::uint64_t now_ms);

    PlayerLabel m_label;
    std::optional<EndPending> m_end_pending;
    bool m_end_shown = false;            ///< once per game (a won game may go on)
    std::uint32_t m_defeat_variant = 0;  ///< own counter for 1010/2020 (no game random numbers)
    std::string m_notice;
    std::uint64_t m_notice_until_ms = 0;
};

}  // namespace o1602::app
