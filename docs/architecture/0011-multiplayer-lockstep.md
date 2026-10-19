# 0011 – Multiplayer: lockstep through the host

Status: accepted · 2026-10-18

Builds on [ADR 0004](0004-deterministic-simulation.md): the simulation is deterministic and driven
by commands, so only the commands have to travel over the network. Implemented in `engine/net`; the
menu in `engine/app` (`multiplayer_menu.hpp`, `multiplayer.cpp`).

## Decision

1. **Star, not peer-to-peer.** Every computer is connected to the host only. Only the host needs a
   reachable port (UDP 16020); the others connect outwards, which works through home routers without
   any port forwarding (M-001). A host without a player of its own (a dedicated server) is possible
   the same way.
2. **Frames.** A frame is one call of `Simulation::tick()`, counted from the start of the session. The
   host collects the commands of all players, puts each into the next open frame and sends the frames
   **complete** to everyone ("everything up to frame N is here"). A frame without commands costs
   nothing but that bound. A computer runs complete frames only, so all of them run the same commands
   in the same frame. While the game is paused the frames go on and the game time stands still, so
   commands given during a pause (building) apply the same on every computer. The session drives the
   simulation in place of `Simulation::advance()`; the game speed sets how many frames run per second.
3. **Lead.** The host closes frames a few frames ahead of its own game: as many as a message to the
   slowest computer takes (from the measured round trip and the game speed, 2 to 50 frames). The lead
   grows at once and shrinks slowly. A computer with more frames in hand than the lead catches up.
4. **Slow computers.** The host runs at most one second of game time (plus the lead) ahead of the
   slowest computer; then it waits, and the game gets slower for everyone instead of unfair. If a
   computer stands completely still for 10 s (a debugger, a window being dragged), the game goes on
   without it; it catches up, or gets the game anew once it is more than 30 s behind.
5. **Fingerprints.** Every 100 frames (5 s at normal speed) each computer sends the host its
   `Simulation::state_hash()`. The host compares it with its own; not every tick, because each
   fingerprint costs a savegame in memory.
6. **One way for the start, joining, reconnecting and desyncs: the savegame.** The host sends a
   computer its game as an `.o1602` savegame ([ADR 0009](0009-own-save-format.md)) together
   with the speed, the pause and the frames already closed after it. That is how a computer comes into
   the game at the start, into a running game, back after a lost connection, and back into step after
   a desync (M-003). The host is always right. A computer that diverges again right after loading
   (another version) is sent away after two tries. An epoch number per game sent keeps old reports of
   a computer from touching the new game.
7. **Senders.** Every command names its player; the host takes from a computer only commands for its
   own slot (review point 6), none from spectators, and at most 256 per frame and player. Only the
   host sets the game speed and hands slots to the computer (`PlayerControl`); every player may pause,
   go on and rename himself, at most four times a second. A message that makes no sense, a message to
   the host larger than 1 MiB and a connection without `Hello` after 10 s disconnect the computer.
8. **Slots.** A computer gets a free human slot of the world (or the one it asks for) and a token with
   which it gets the same slot back after a lost connection. Tokens are random and belong to whoever
   holds the slot: a slot that changes hands (in the lobby, after a kick, to a newcomer) gets a new
   one, and the host's own slot takes none. In a game hosted while it runs (`--host`), the computer
   players' slots are open as well: the computer plays such a slot until someone takes it and again
   after he leaves. Without a free slot it watches. A slot
   that was left stays in the game and gives no commands until someone comes back. The player's name
   is set as a `PlayerRename` of that player.
9. **Same data.** When a computer joins, the host checks the protocol version and a fingerprint of the
   game data the simulation depends on (`haeuser.cod`, `figuren.cod`), and the game's password if it
   has one. The names for new cities and ships (`Text.cod` of the installation that began the game)
   are part of the simulation's saved state, so they reach every computer with the game and
   installations in different languages can play together.
10. **Transport.** Messages travel reliably and in order over ENet 1.3.18 (`third_party/enet`, MIT,
    shipped unchanged so that the build needs no network). The sessions only know the interface
    `net::Transport`; the tests have an in-memory network with latency, jitter and cuts, and a relay
    or Steam can be plugged in later.
11. **Lobby.** A host may start without a game: it holds a `Lobby` with the title, a description of
    what is played and the slots of the game to come (open, a human, a computer player, closed).
    Computers come in, take open slots or watch, say they are ready and chat (at most 5 lines a
    second each). The host sets the kinds of the slots, removes players and starts when it wants;
    the game is made from the lobby and every computer gets it as it would get a running one
    (point 6). The lobby travels as a whole whenever it changes. A slot's index is the player of the
    world, and with it the banner colour; the slots of the free trader, the pirates and the natives
    are reserved and not shown. Every game has all its players: a slot nobody takes is played by
    the computer, and the host hands it to whoever comes into the running game (the command
    `PlayerControl`) and back to the computer when that player leaves.
14. **Players.** A new endless game has four to twelve players. Four play on the original's maps;
    more get a world of their own (ADR 0008), as much sea and land per player as the original's
    500 × 350 for four. Players five on sit behind the original's seven slots (slot 7 on) with
    banners of their own colours (`BannerTable::add_more_players`), named after them.
12. **Finding games.** A host answers searches on UDP port 16021 with what the menu lists (title, host,
    game, players and slots, started, password, joining, spectators, protocol version). A computer
    looking for games asks every network of the computer by broadcast, the computer itself and the
    hosts it joined before directly,
    every 3 s, and forgets a game that has not answered for 6 s. Nothing of this is needed to join:
    an address alone does it too, so games on the internet need no server in between.
13. **The menu is Open1602's own.** The original's network screens (DirectPlay sessions, its lobby)
    are not used. Open1602's menu has three screens: finding a game (the list fills by itself, an
    address can be typed, hosts joined before are remembered), creating one (a new endless game with
    its level and world number, a scenario or a savegame; title, password, shown on the local
    network, joining the running game, spectators, host without a player, port) and the lobby
    (slots, ready, chat, start). Its texts follow the language of the installation.

## Rationale

- A star with a host keeps the order of the commands in one place: no agreement among all computers,
  no dependency on the order of the network, one place to check the senders. The delay is one round
  trip to the host, which does not matter for a building game.
- Taking the savegame for everything that means "a computer needs the game" saves three mechanisms
  of their own and is covered by the test "continuous against saving and loading".
- The network load does not depend on the game speed: one message per frame drawn and computer.
- A lobby before the game, rather than slots taken in a running one, lets the players choose their
  slots and the host set the computer players before anything happens; the same savegame path then
  starts the game for everyone.
- A search of its own on a separate port keeps the game port free of anything but sessions, and a
  host behind a router stays reachable by address.

## Consequences

- Videos, menus and windows do not pause the game in multiplayer (M-018); only pause commands do, for
  everyone. The automatic return to speed 1 at a fire or an attack works only on the host.
- Loading savegames and starting new games is blocked during a session.
- The fingerprint costs an unpacked savegame on every computer every 100 frames.
- A host behind a router needs UDP port 16020 (or the one it chose) forwarded for players on the
  internet; the menu says so.

## Open

- Choosing banners in the lobby, the endless game's own world settings over the network.
- The computer taking over slots of players who began as humans, waiting for a disconnected player.
- Changing the host when it leaves (today the game ends for everyone), a relay for hosts behind strict
  NAT, IPv6 (ENet 1.3 knows IPv4 only), searches beyond the local network.
- Replays: the savegame plus the session's frames are already all one needs.
