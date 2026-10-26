# Messages

The messages of the status bar (the icons at the bottom that speak, show a video and tell what
happened when the mouse is over them) are data: a mod can add messages of its own and change the
original's, with its own text in every language, an icon, a sound and a video
([ADR 0007](../architecture/0007-extensibility-and-modding.md), sections 2 and "Content types").

**Status:** Done are the file `messages.toml`, new messages, changes to the original's messages,
texts per language with placeholders, own sounds (WAV) and the original's videos. The status bar,
the voices and the videos take everything from the message table (`app::MessageTable`). Still
open, all with milestone M8: posting a mod's message from a script (Lua; until then `--message`
shows one at the start), own icons and videos (PNG and a modern video format come with the import
of content), OGG sounds, and a manifest with version, dependencies and load order.

## Where a mod's messages are

```
<user data folder>/mods/<package>/messages.toml
<user data folder>/mods/<package>/sounds/horn.wav
```

The user data folder is `~/.local/share/Open1602` on Linux, `~/Library/Application Support/Open1602`
on macOS and `%APPDATA%\Open1602` on Windows. The folder's name is the package's id: lower-case
letters, digits, `_` and `-` (`lighthouse_mod`). `anno1602` is the original's package and cannot be
used. Packages are read in their [load order](packages.md#load-order); a later one can change what an earlier one
defined. Until content packages have a manifest (M8), the folder's name is all there is.

Problems are written to the log (standard error) with the file and the line, for example
`mods: lighthouse_mod/messages.toml line 7: unknown field 'colour'`. A field with a wrong value is
passed over and the rest of the message is used; a file that is no valid data file (a string not
closed, a key given twice) is passed over as a whole.

## The file

`messages.toml` is [TOML](https://toml.io), of which strings, integers, booleans and tables are
understood (no arrays, floats or multi-line strings). It must be UTF-8. Each table is one message:

```toml
# A message of this package: its id is lighthouse_mod:lit.
[lit]
text = "The lighthouse of {player} shines over {island}."
text.de = "Der Leuchtturm von {player} strahlt über {island}."
icon = "scroll"
kind = "events"
sound = "sounds/horn.wav"
video = 23

# A message that behaves like the original's "gold found" (0x2C), without its video.
[gold_rush]
like = 0x2C
text = "Gold fever breaks out!"
text.de = "Das Goldfieber bricht aus!"
video = false

# A change to the original's plague message (MELDUNG 1): a voice of its own, the rest stays.
["anno1602:1"]
sound = "sounds/plague.wav"
```

A table without a package (`[lit]`) is a new message of this package. A table with one
(`["anno1602:1"]`, `["other_mod:x"]`, the quotes are needed because of the colon) changes the
message of that id: only the fields given change. The original's messages are `anno1602:<number>`
with their number in `Text.cod`'s `[MELDUNG]` section, in decimal or hex (`anno1602:0x2C`).

| Field | Value | Meaning |
|---|---|---|
| `text` | string | The text in the status bar, for every language without its own |
| `text.<code>` | string | The text in one language (`text.de`, `text.en`, ...; the codes of [languages.md](languages.md)) |
| `like` | number or id | Start from this message: its icon, text, voice, video, kind and flags (applied first, wherever it stands) |
| `icon` | `"notice"`, `"scroll"`, `"combat"`, `"player"`, `"plague"`, `"fire"`, `"drought"`, `"volcano"` | The icon in the status bar; the first four in the colour of the player the message is about |
| `kind` | `"general"`, `"events"`, `"island"`, `"players"`, `"trade"`, `"combat"`, `"orders"`, `"diplomacy"`, `"routes"` | Which switch of the voice and video options mutes it (`general`: only the master switch; `routes`: Open1602's switch of „Überprüft Eure Handelsrouten!“) |
| `sound` | file name, number or `false` | A WAV file of the package (relative to its folder, never outside it), a file of the original's `Speech8` (`sound = 530`), or none (`false`: only the short message sound) |
| `video` | number or `false` | A video of the original's `VideoSmk` (`video = 23` plays `VideoSmk/23.smk`), or none |
| `video_always` | `true`/`false` | Play the video also when the place of the message is on screen (else only when it is not) |
| `normal_speed` | `true`/`false` | The game goes back to normal speed (as with fire, plague and attacks) |

A new message without `text` and without `like` has nothing to show; the log says so.

### Text

- The game's fonts know the letters of Windows-1252 (Western European languages); other letters are
  left out.
- The language is the game's ([languages.md](languages.md)). Without a text in it, `text` is
  shown, then `text.en`, and without any the original's text of the message given by `like`.
- `{name}` is a placeholder that the game or a script fills (`{player}`, `{island}`). One without a
  value stays as it is, so a missing value is easy to spot.
- A changed original message without a text of its own keeps the original's text from `Text.cod`,
  in the language of the installation.

### Sound and video

Like the original, a message plays its video when videos of its kind are on and its place is not on
screen (or always, with `video_always`); otherwise it speaks its sound; if it has none, or four
voices are waiting already, the short message sound plays. A sound of the package goes before the
original's voices of a message it borrowed with `like`.

Sounds must be WAV files (PCM, 8 or 16 bits, mono or stereo, any rate); they are read once when the
game starts. OGG comes with the import of content (M8).

## Trying a message

```sh
open1602 --message lighthouse_mod:lit
```

shows the message at the start of the game, as the game will post it: icon, text (placeholders
unfilled), sound and video.

## In the code

`app::MessageTable` (`engine/app/src/message_table.hpp`) holds the messages: the original's first,
at their numbers, then the packages'. At runtime a message is its index (`app::MessageId`); the
string ids are only used when loading and by the scripts that will post messages
(`MapViewer::announce(id, args)`). The status bar (`Hud`) takes icon and text from the table, the
voices and videos (`GameSound`, `audio::Voices`) its `audio::MessagePlayback`. Messages are
presentation only: they never change the game. A game shows the messages of the packages it uses
([packages.md](packages.md#which-packages-a-game-uses)), on every computer of a multiplayer game the
same.

The data file format is read by `core::parse_data_file` (`engine/core/include/o1602/core/data_file.hpp`),
meant for every data file of content packages.
