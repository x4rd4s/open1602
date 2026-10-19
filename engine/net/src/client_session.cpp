// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <utility>

#include "o1602/net/session.hpp"

namespace o1602::net {

ClientSession::ClientSession(std::unique_ptr<Transport> transport, Hello hello, Loader load, SessionConfig config)
    : Session(std::move(transport), config), m_hello(std::move(hello)), m_load(std::move(load)) {
    m_hello.protocol = kProtocolVersion;
    m_hello.name.resize(std::min(m_hello.name.size(), kMaxNameLength));
    m_hello.password.resize(std::min(m_hello.password.size(), kMaxPasswordLength));
}

void ClientSession::say(std::string text) {
    text.resize(std::min(text.size(), kMaxChatLength));
    if (m_welcomed && !m_ended && !text.empty()) {
        send(kHost, Chat{m_seat, m_hello.name, std::move(text)});
    }
}

void ClientSession::take_seat(Seat seat) {
    if (m_welcomed && !m_ended && !m_lobby.started) {
        send(kHost, TakeSeat{seat});
    }
}

void ClientSession::set_ready(bool ready) {
    if (m_welcomed && !m_ended && !m_lobby.started) {
        send(kHost, SetReady{ready});
    }
}

void ClientSession::submit(sim::Command command) {
    // Only the host decides when a command is carried out, also those of this computer's player.
    if (m_ended || m_simulation == nullptr || m_seat == sim::kNoPlayer || command_player(command) != m_seat) {
        return;
    }
    m_outgoing.push_back(std::move(command));
}

void ClientSession::update(std::uint32_t real_ms, const sim::Simulation::BeforeTick& before_tick) {
    if (m_ended) {
        return;
    }
    m_incoming.clear();
    m_transport->poll(m_incoming);
    for (TransportEvent& e : m_incoming) {
        if (m_ended) {
            break;
        }
        switch (e.kind) {
            case TransportEvent::Kind::Connected:
                m_connected = true;
                send(kHost, m_hello);
                break;
            case TransportEvent::Kind::Disconnected:
                end(m_welcomed ? EndReason::HostLeft : EndReason::ConnectFailed);
                break;
            case TransportEvent::Kind::Received:
                receive(e.data);
                break;
        }
    }
    if (m_ended) {
        m_transport->disconnect(kHost);
        m_transport->flush();
        return;
    }
    if (m_simulation != nullptr) {
        run(real_ms, before_tick);
    }
    while (!m_outgoing.empty()) {
        // As many as fit into one message the host takes (large build orders take room).
        std::size_t n = std::min<std::size_t>(m_outgoing.size(), kMaxCommandsPerMessage);
        std::vector<std::uint8_t> bytes;
        for (;;) {
            CommandBatch batch;
            batch.commands.assign(m_outgoing.begin(), m_outgoing.begin() + static_cast<std::ptrdiff_t>(n));
            bytes = encode(batch);
            if (bytes.size() <= kMaxMessageToHost || n == 1) {
                break;
            }
            n /= 2;
        }
        m_outgoing.erase(m_outgoing.begin(), m_outgoing.begin() + static_cast<std::ptrdiff_t>(n));
        if (bytes.size() <= kMaxMessageToHost) {
            m_transport->send(kHost, bytes);
        }
    }
    report(real_ms);
    m_transport->flush();
}

void ClientSession::receive(std::span<const std::uint8_t> bytes) {
    auto message = decode(bytes);
    if (!message) {
        end(EndReason::ProtocolError);
        return;
    }
    if (const auto* welcome = std::get_if<Welcome>(&*message)) {
        // Again in the lobby when this computer moves to another slot.
        if ((m_welcomed && m_lobby.started) || welcome->protocol != kProtocolVersion) {
            end(EndReason::ProtocolError);
            return;
        }
        m_welcomed = true;
        m_seat = welcome->seat;
        m_token = welcome->token;
        SessionEvent e;
        e.kind = SessionEvent::Kind::Welcomed;
        e.seat = m_seat;
        event(std::move(e));
    } else if (const auto* refused = std::get_if<Refused>(&*message)) {
        SessionEvent e;
        e.kind = SessionEvent::Kind::Ended;
        e.reason = EndReason::Refused;
        e.refused = refused->reason;
        m_ended = true;
        m_end_reason = refused->reason == RefuseReason::Desync ? EndReason::Desync : EndReason::Refused;
        e.reason = m_end_reason;
        event(std::move(e));
    } else if (!m_welcomed) {
        end(EndReason::ProtocolError);
    } else if (auto* snapshot = std::get_if<Snapshot>(&*message)) {
        load(*snapshot);
    } else if (auto* frames = std::get_if<Turn>(&*message)) {
        turn(*frames);
    } else if (auto* roster = std::get_if<Roster>(&*message)) {
        m_roster = std::move(roster->members);
    } else if (auto* lobby = std::get_if<Lobby>(&*message)) {
        m_lobby = std::move(*lobby);
        lobby_event();
    } else if (const auto* line = std::get_if<Chat>(&*message)) {
        chat_event(*line);
    } else if (const auto* desync = std::get_if<Desync>(&*message)) {
        for (const Seat s : desync->seats) {
            SessionEvent e;
            e.kind = SessionEvent::Kind::Desync;
            e.seat = s;
            e.frame = desync->frame;
            event(std::move(e));
        }
    } else {
        end(EndReason::ProtocolError);  // a message only computers send
    }
}

void ClientSession::load(Snapshot& m) {
    sim::Simulation* simulation = m_load ? m_load(m.save) : nullptr;
    if (simulation == nullptr) {
        m_simulation = nullptr;
        end(EndReason::LoadFailed);
        return;
    }
    m_simulation = simulation;
    // Speed and pause are no part of the savegame.
    m_simulation->set_speed(std::max<std::uint32_t>(m.speed, 1));
    if (m.paused != m_simulation->paused()) {
        m_simulation->execute(sim::GameSpeedOrder{
            sim::kNoPlayer, m.paused ? sim::GameSpeedOrder::Op::Pause : sim::GameSpeedOrder::Op::Resume, m.speed});
    }
    m_epoch = m.epoch;
    m_frames = m.frame;
    m_closed = m.frame;
    m_frames_ahead.clear();
    m_pending_ms = 0;
    m_outgoing.clear();  // given in the old game: they would act on the new one
    SessionEvent e;
    e.kind = SessionEvent::Kind::Loaded;
    e.seat = m_seat;
    e.frame = m_frames;
    event(std::move(e));
    // At once: the host takes this computer as playing from now on.
    send(kHost, Progress{m_epoch, m_frames, std::nullopt});
    m_reported = m_frames;
    m_since_report = 0;
}

void ClientSession::turn(Turn& m) {
    if (m_simulation == nullptr) {
        return;  // frames of a game this computer does not have (yet)
    }
    if (m.first > m_closed || m.closed - m_frames > (std::uint64_t{1} << 20)) {
        end(EndReason::ProtocolError);  // a gap, or more frames than any game runs ahead
        return;
    }
    m_lead = m.lead;
    auto listed = m.frames.begin();
    for (std::uint64_t f = m.first; f < m.closed; ++f) {
        const bool has = listed != m.frames.end() && listed->frame == f;
        if (f >= m_closed) {
            m_frames_ahead.emplace_back();
            if (has) {
                m_frames_ahead.back() = std::move(listed->commands);
            }
        }
        if (has) {
            ++listed;
        }
    }
    m_closed = std::max(m_closed, m.closed);
}

void ClientSession::run(std::uint32_t real_ms, const sim::Simulation::BeforeTick& before_tick) {
    std::uint32_t due = frames_due(real_ms, *m_simulation);
    // More in hand than the host keeps ahead: this computer is behind and catches up.
    const std::uint64_t in_hand = m_closed - m_frames;
    if (in_hand > std::uint64_t{m_lead} + 2) {
        due = std::max<std::uint32_t>(due, static_cast<std::uint32_t>(std::min<std::uint64_t>(
                                               in_hand - m_lead, sim::Simulation::kMaxTicksPerAdvance)));
    }
    std::uint32_t done = 0;
    while (done < due && m_frames < m_closed) {
        if (before_tick && !before_tick()) {
            break;
        }
        const std::vector<sim::Command> commands = std::move(m_frames_ahead.front());
        m_frames_ahead.pop_front();
        if (const auto hash = run_frame(*m_simulation, commands)) {
            send(kHost, Progress{m_epoch, m_frames, hash});
            m_reported = m_frames;
            m_since_report = 0;
        }
        ++done;
    }
    m_waiting = done < due && m_frames == m_closed;
    ran(done);
}

void ClientSession::report(std::uint32_t real_ms) {
    if (m_simulation == nullptr) {
        return;
    }
    m_since_report += real_ms;
    if (m_frames != m_reported && m_since_report >= m_config.progress_interval_ms) {
        send(kHost, Progress{m_epoch, m_frames, std::nullopt});
        m_reported = m_frames;
        m_since_report = 0;
    }
}

}  // namespace o1602::net
