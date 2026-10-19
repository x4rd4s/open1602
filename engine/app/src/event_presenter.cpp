// SPDX-License-Identifier: GPL-3.0-or-later
#include "event_presenter.hpp"

#include <algorithm>
#include <cstdio>

#include "o1602/audio/game_audio.hpp"
#include "o1602/core/text.hpp"

namespace o1602::app {

namespace {

/// A message, with where it happened and the player it is about.
Announcement announce(std::uint16_t message, std::optional<std::pair<double, double>> tile = std::nullopt,
                      std::optional<sim::PlayerId> about = std::nullopt) {
    Announcement a;
    a.message = message;
    a.tile = tile;
    a.about = about;
    return a;
}

/// A message whose video plays only some of its tracks: the messages 0x6B..0x6E speak to the
/// winner on track 1 and to the others on track 2.
Announcement announce_with_tracks(std::uint16_t message, std::uint8_t tracks) {
    Announcement a;
    a.message = message;
    a.tracks = tracks;
    return a;
}

}  // namespace

std::array<std::pair<std::uint8_t, std::uint32_t>, 3> island_crops(const sim::WorldIsland& island) {
    static constexpr std::array<std::uint8_t, 3> kNorth = {48, 50, 46};
    static constexpr std::array<std::uint8_t, 3> kSouth = {47, 49, 51};
    std::array<std::pair<std::uint8_t, std::uint32_t>, 3> crops{};
    const std::array<std::uint8_t, 3>& wares = island.header.climate == 1 ? kSouth : kNorth;
    for (std::size_t i = 0; i < crops.size(); ++i) {
        crops[i] = {wares[i], sim::island_fertility(island, wares[i]) * 100 / 128};
    }
    return crops;
}

std::string crops_text(const sim::WorldIsland& island) {
    std::string text;
    for (const auto& [ware, percent] : island_crops(island)) {
        if (!text.empty()) {
            text += ", ";
        }
        text += "#" + std::to_string(ware) + " " + std::to_string(percent) + " %";
    }
    return text;
}

void add_chat_line(std::deque<ChatLine>* chat, ChatLine line, bool echo) {
    if (echo) {
        std::fprintf(stderr, "multiplayer: %s%s%s\n", core::cp1252_to_utf8(line.name).c_str(),
                     line.name.empty() ? "" : ": ", core::cp1252_to_utf8(line.text).c_str());
    }
    if (chat == nullptr) {
        return;
    }
    chat->push_back(std::move(line));
    while (chat->size() > kChatKept) {
        chat->pop_front();
    }
}

EventPresenter::EventPresenter(PlayerLabel label) : m_label(std::move(label)) {}

std::vector<Notice> EventPresenter::city_events(const sim::World& world, sim::PlayerId local,
                                                const std::vector<sim::CityEvent>& events) const {
    std::vector<Notice> out;
    for (const sim::CityEvent& e : events) {
        if (e.owner != local) {
            continue;
        }
        const sim::City* city = world.city(e.island, e.city);
        const std::string name = city != nullptr ? city->name : std::string("?");
        if (e.kind == sim::CityEvent::Kind::Hunger) {
            std::fprintf(stderr, "%s: the people are starving\n", name.c_str());
            out.emplace_back(announce(0x33));
        } else if (e.kind == sim::CityEvent::Kind::Monument) {
            out.emplace_back(announce(0x31));
        } else if (e.kind == sim::CityEvent::Kind::Unlocked) {
            std::fprintf(stderr, "%s: build level %u reached\n", name.c_str(), e.infra);
        } else {
            std::fprintf(stderr, "%s: short of ware %u\n", name.c_str(), e.ware);
        }
    }
    return out;
}

void EventPresenter::ship_events(const sim::World& world, sim::PlayerId local,
                                 const std::vector<sim::ShipEvent>& events) const {
    // MELDUNG 0x2A-0x30.
    for (const sim::ShipEvent& e : events) {
        if (e.player != local || e.island >= world.islands().size()) {
            continue;
        }
        const unsigned number = world.islands()[e.island].header.number;
        switch (e.kind) {
            case sim::ShipEvent::Kind::NativesFound:
                std::fprintf(stderr, "you discovered a native village! (island %u)\n", number);
                break;
            case sim::ShipEvent::Kind::PiratesFound:
                std::fprintf(stderr, "the pirates' hideout has been found! (island %u)\n", number);
                break;
            case sim::ShipEvent::Kind::CropsExplored:
                std::fprintf(stderr, "the crops of island %u are explored: %s\n", number,
                             crops_text(world.islands()[e.island]).c_str());
                break;
            case sim::ShipEvent::Kind::DepositFound:
                std::fprintf(stderr, "%s on island %u (%u,%u)!\n",
                             e.ware == sim::ware::kGold ? "you discovered a gold vein" : "an ore deposit lies", number,
                             e.x, e.y);
                break;
            case sim::ShipEvent::Kind::ExploreFinished:
                std::fprintf(stderr, "island %u explored\n", number);
                break;
            default:
                break;
        }
    }
}

std::vector<Notice> EventPresenter::disaster_events(const sim::World& world, sim::PlayerId local,
                                                    const std::vector<sim::DisasterEvent>& events) const {
    std::vector<Notice> out;
    for (const sim::DisasterEvent& e : events) {
        if (e.owner != local) {
            continue;
        }
        const sim::City* city = world.city(e.island, e.city);
        const char* name = city != nullptr ? city->name.c_str() : "?";
        const bool fire = e.disaster == sim::Disaster::Kind::Fire;
        if (e.kind == sim::DisasterEvent::Kind::Started && !e.spread) {
            std::fprintf(stderr, "%s: %s (%u,%u)\n", name,
                         fire ? "a fire broke out!" : "the plague haunts your settlement!", e.x, e.y);
            const sim::WorldIsland& isl = world.islands()[e.island];
            out.emplace_back(
                announce(fire ? std::uint16_t{0x02} : std::uint16_t{0x01},
                         std::make_pair(static_cast<double>(isl.x + e.x), static_cast<double>(isl.y + e.y))));
        } else if (e.kind == sim::DisasterEvent::Kind::BurnedDown) {
            std::fprintf(stderr, "%s: a building burned down (%u,%u)\n", name, e.x, e.y);
        }
    }
    return out;
}

std::vector<Notice> EventPresenter::island_events(const sim::World& world, sim::PlayerId local,
                                                  const std::vector<sim::IslandEvent>& events) const {
    std::vector<Notice> out;
    for (const sim::IslandEvent& e : events) {
        const sim::City* own = nullptr;
        for (const sim::City& c : world.cities()) {
            if (c.island == e.island && c.owner == local) {
                own = &c;
            }
        }
        if (own == nullptr) {
            continue;
        }
        switch (e.kind) {
            case sim::IslandEvent::Kind::DroughtStarted:
                std::fprintf(stderr, "%s: the drought destroys your harvest!\n", own->name.c_str());
                out.emplace_back(announce(0x03));
                break;
            case sim::IslandEvent::Kind::Withered:
                std::fprintf(stderr, "%s: %u fields withered\n", own->name.c_str(), e.count);
                break;
            case sim::IslandEvent::Kind::Recovered:
                std::fprintf(stderr, "%s: %u fields grow again\n", own->name.c_str(), e.count);
                break;
            case sim::IslandEvent::Kind::DroughtOver:
                std::fprintf(stderr, "%s: the drought is over\n", own->name.c_str());
                break;
        }
    }
    return out;
}

std::vector<Notice> EventPresenter::volcano_events(const sim::World& world, sim::PlayerId local,
                                                   const std::vector<sim::VolcanoEvent>& events) const {
    std::vector<Notice> out;
    for (const sim::VolcanoEvent& e : events) {
        if (e.kind == sim::VolcanoEvent::Kind::Erupted && e.message && world.islands()[e.island].discovered_by(local)) {
            std::fprintf(stderr, "a volcano has erupted! (island %u)\n",
                         static_cast<unsigned>(world.islands()[e.island].header.number));
            const sim::WorldIsland& isl = world.islands()[e.island];
            out.emplace_back(
                announce(0x5E, std::make_pair(isl.x + isl.map.width() / 2.0, isl.y + isl.map.height() / 2.0)));
        }
    }
    return out;
}

void EventPresenter::command_results(const std::vector<sim::CommandResult>& results) const {
    for (const sim::CommandResult& r : results) {
        if (!r.build) {
            if (r.demolished != 0) {
                std::fprintf(stderr, "demolished %zu, back: %lld gold, %u t tools, %u t wood, %u t bricks\n",
                             r.demolished, static_cast<long long>(r.refund.money), r.refund.tools / 32,
                             r.refund.wood / 32, r.refund.bricks / 32);
            }
            continue;
        }
        std::fprintf(stderr, "built %zu of %zu", r.build->placed, r.build->items.size());
        for (const sim::PlannedBuild& item : r.build->items) {
            if (!item.ok()) {
                std::fprintf(stderr, " - %s", sim::refusal_name(item.refusal));
                break;
            }
        }
        std::fprintf(stderr, "\n");
    }
}

std::vector<Notice> EventPresenter::construction_events(const sim::World& world, sim::PlayerId local,
                                                        const std::vector<sim::ConstructionEvent>& events) const {
    std::vector<Notice> out;
    for (const sim::ConstructionEvent& e : events) {
        if (e.kind != sim::ConstructionEvent::Kind::CityFounded) {
            continue;
        }
        if (const sim::City* c = world.city(e.island, e.city)) {
            std::fprintf(stderr, "a new city was founded: %s\n", c->name.c_str());
            if (c->owner != local) {
                const sim::WorldIsland& i = world.islands()[e.island];
                out.emplace_back(
                    announce(0x0E, std::make_pair(i.x + i.map.width() / 2.0, i.y + i.map.height() / 2.0), c->owner));
            }
        }
    }
    return out;
}

std::vector<Notice> EventPresenter::diplomacy_notices(sim::PlayerId local,
                                                      const std::vector<sim::DiplomacyNotice>& notices) const {
    static constexpr std::array<std::uint16_t, 6> kMessages = {0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52};
    std::vector<Notice> out;
    for (const sim::DiplomacyNotice& n : notices) {
        if (n.to == local) {
            out.emplace_back(announce(kMessages[static_cast<std::size_t>(n.kind)]));
        }
    }
    return out;
}

void EventPresenter::expect_end_screen(sim::GameOutcome outcome, std::uint64_t delay_ms, std::uint64_t now_ms) {
    if (!m_end_pending && !m_end_shown) {
        m_end_pending = EndPending{outcome, delay_ms, now_ms, false};
    }
}

std::vector<Notice> EventPresenter::game_events(const sim::World& world, sim::PlayerId local,
                                                const std::vector<sim::GameEvent>& events, std::uint64_t now_ms) {
    std::vector<Notice> out;
    for (const sim::GameEvent& e : events) {
        const sim::Player* p = world.player(e.player);
        const std::string who =
            p != nullptr ? m_label(*p, e.player) : std::string("player ") + std::to_string(e.player);
        switch (e.kind) {
            case sim::GameEvent::Kind::MissionAccomplished:
                std::fprintf(stderr, "%s accomplished the mission!\n", who.c_str());
                if (e.player == local) {
                    expect_end_screen(sim::GameOutcome::Won, kEndScreenDelayMs, now_ms);
                    // 0x6B is the multiplayer message with the winner's colour (0x6B..0x6E); its
                    // video speaks to the winner on track 1.
                    if (e.message == audio::kMessageMultiplayerWin && p != nullptr) {
                        out.emplace_back(announce_with_tracks(
                            static_cast<std::uint16_t>(e.message + sim::four_colour_index(p->banner)), 0x03));
                    } else {
                        out.emplace_back(announce(e.message));
                    }
                }
                break;
            case sim::GameEvent::Kind::GameOver:
                std::fprintf(stderr, "the game is over\n");
                out.emplace_back(ScoreUpdate{sim::kNoPlayer, true});  // op 0x3E, after the winners are known
                if (e.player != local && world.player(local) != nullptr && !world.player(local)->won) {
                    expect_end_screen(sim::GameOutcome::Lost, kEndScreenDelayMs, now_ms);
                    const sim::Mission* m = p != nullptr ? world.mission_of(*p) : nullptr;
                    if (m != nullptr && m->success_message == audio::kMessageMultiplayerWin) {
                        // A race: "the red player fulfilled his task", the others' voice (track 2).
                        out.emplace_back(
                            announce_with_tracks(static_cast<std::uint16_t>(audio::kMessageMultiplayerWin +
                                                                            sim::four_colour_index(p->banner)),
                                                 0x05));
                    } else {
                        out.emplace_back(announce(0x53));  // "Ihr wurdet besiegt!"
                    }
                }
                break;
            case sim::GameEvent::Kind::PlayerDefeated:
                std::fprintf(stderr, "%s is defeated\n", who.c_str());
                out.emplace_back(ScoreUpdate{e.player, false});  // op 3 sub 9, the defeated one still counted
                if (e.player == local) {
                    expect_end_screen(sim::GameOutcome::Lost, kDefeatDelayMs, now_ms);
                    out.emplace_back(announce(0x53));
                    // The defeat videos come directly, whatever the video switches say (0x004766A0):
                    // bankruptcy 998, otherwise 1010 or 2020.
                    out.emplace_back(DirectVideo{e.reason == sim::GameEvent::Reason::Bankrupt ? std::uint16_t{998}
                                                 : (m_defeat_variant++ & 1U) == 0             ? std::uint16_t{1010}
                                                                                              : std::uint16_t{2020}});
                } else {
                    out.emplace_back(announce(e.reason == sim::GameEvent::Reason::Bankrupt ? std::uint16_t{0x59}
                                                                                           : std::uint16_t{0x5A}));
                }
                break;
            case sim::GameEvent::Kind::ArchAllowed:
                if (e.player == local) {
                    std::fprintf(stderr, "you may build a triumphal arch\n");
                    out.emplace_back(announce(0x32));
                }
                break;
        }
    }
    return out;
}

std::optional<sim::GameOutcome> EventPresenter::end_screen_due(std::uint64_t now_ms, bool videos, bool game_paused) {
    if (!m_end_pending) {
        return std::nullopt;
    }
    EndPending& e = *m_end_pending;
    const std::uint64_t step = now_ms >= e.last_ms ? now_ms - e.last_ms : 0;
    e.last_ms = now_ms;
    // WinMain runs no game frame while a video plays, the next video of a chain included.
    if (videos) {
        e.after_video = true;
        return std::nullopt;
    }
    if (!game_paused) {
        e.left_ms -= std::min(e.left_ms, step);
    }
    if (!e.after_video && e.left_ms > 0) {
        return std::nullopt;
    }
    const sim::GameOutcome outcome = e.outcome;
    m_end_pending.reset();
    return outcome;
}

void EventPresenter::notify(std::string text, std::uint64_t now_ms) {
    std::fprintf(stderr, "%s\n", core::cp1252_to_utf8(text).c_str());
    m_notice = std::move(text);
    m_notice_until_ms = now_ms + kNoticeMs;
}

}  // namespace o1602::app
