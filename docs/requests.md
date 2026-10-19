# How to contribute observations of the original game

Open1602 rebuilds the behaviour of *Anno 1602* from its data and from analysis of the program. Some
points cannot be settled that way: what the original does in a rare situation, how fast something
happens, how a screen looks in a rotated view. An observation in the running original settles them.
The open points of this kind are listed in
[status.md](status.md#13-needs-clarification-behaviour-of-the-original-not-confirmed), section 1.3.

## What helps

- **Savegames before and after an event.** Many systems can be checked by loading a savegame of the
  original, letting Open1602 play on and comparing the result with a second savegame the original made
  later. Useful pairs: shortly before and after a volcanic eruption, during and shortly after a
  drought, before and after building a palace or cathedral, before and after a fight or a conquest,
  two savegames a few seconds of game time apart.
- **Videos** of things a savegame does not hold: fires with the fire brigade arriving, a plague with
  the doctor, robbers, naval battles, how far lava bombs fly, how ships of the computer players sail.
- **Screenshots** for comparing the picture: preferably at the smallest graphics level and
  1024 × 768, and say the zoom level and the rotation of the view.
- **Answers to yes/no questions** from section 1.3, for example whether a unit in training can be
  sent out of the castle, or whether a burning shipyard keeps building its ship.
- **Longer games:** savegames of an endless game after about one and three hours show how far the
  computer players get in the original.

## How to record an observation

1. Note the edition and language of the game (for example the GOG "Königsedition", German) and the
   difficulty.
2. Save in **new slots**. Do not overwrite savegames that tests or earlier reports refer to.
3. Give each savegame a descriptive name, for example "BeforeVolcano" and "AfterVolcano".
4. Write down the exact steps: what you did, in which order, and the game time shown when you saved.
   The original counts game time, not real time; at a higher speed it passes faster.
5. Note any message the game showed, with its exact wording.
6. For a video, say what to look for and at which time in the video it happens.

## How to send it

Open an issue on the project's repository (https://github.com/x4rd4s/open1602/issues) with the
steps and the point of section 1.3 it answers. Savegames, screenshots and videos can be attached to
the issue, but they are never committed to the repository. Other files of the game installation
(graphics, sounds, game videos, `.cod` files) must never be uploaded or committed
([ADR 0002](architecture/0002-license-and-original-data.md)).
