# engine/net

Multiplayer in lockstep through the host ([ADR 0011](../../docs/architecture/0011-multiplayer-lockstep.md)):
only commands travel, every computer computes the whole game. Depends on `sim` only (and ENet), so it
also runs without a window, as a dedicated server or in tests.

| File | Contents |
|---|---|
| `protocol.hpp` | Messages as bytes and back (little endian, fixed widths, bounds checked) |
| `transport.hpp` | Interface: reliable, ordered messages per connection |
| `enet_transport.hpp` | Transport over UDP with ENet (`third_party/enet`), port 16020 |
| `loopback.hpp` | In-memory network with its own clock, latency, jitter and cuts (tests) |
| `session.hpp` | `HostSession` (lobby, collects commands, closes frames, compares fingerprints, sends savegames) and `ClientSession` (takes a slot, runs complete frames); when the host leaves, a `HostSession` made from the successor's `ClientSession` hosts on and the others follow it (host migration) |
| `lan.hpp` | Finding games: `LanBeacon` answers searches for a host, `LanBrowser` asks the local network and remembered hosts (UDP port 16021) |

In the game: the main menu's "Mehrspieler" ("Multiplayer") opens the multiplayer menu. From the
command line, `open1602 --host --scenario "Szenes/Am Ende allein.szm" --name Merlin` hosts a running
game at once, and `open1602 --join ADDRESS --name Anna [--password TEXT]` joins one.
