# File formats of Anno 1602

Status legend: ✅ verified (code + test against original data) · 🟡 partly · ⬜ open

| Format | Files | Status | Docs | Implementation |
|---|---|---|---|---|
| Chunk container | `.BSH .ZEI .COL .SCP .SZS .SZM .GAM` | ✅ | [chunks.md](chunks.md) | `engine/assets/chunk_file.*` |
| COD scripts | `haeuser.cod figuren.cod` | ✅ encoding, grammar, objects | [cod.md](cod.md) | `engine/assets/cod.*`, `cod_parser.*` |
| Texts | `Text.cod Editor.cod` | 🟡 encoding, sections; mapping of the lines only where needed (city names, player names) | [cod.md](cod.md#7-textcod--editorcod) | `engine/assets/text_table.*` |
| Palette | `TOOLGFX/STADTFLD.COL` | ✅ | [col.md](col.md) | `engine/assets/palette.*` |
| Sprites | `GFX/ MGFX/ SGFX/ TOOLGFX/ *.BSH` | ✅ | [bsh.md](bsh.md) | `engine/assets/sprite_sheet.*` |
| Fonts | `TOOLGFX/*.ZEI` | ✅ glyphs, metrics, colours, line wrapping | [bsh.md](bsh.md#zei-fonts), [ui.md](../re/ui.md#fonts) | `engine/assets/sprite_sheet.*`, `engine/ui/font.*` |
| Islands | `NORD/ SUED/ NORDNAT/ SUEDNAT/ NOKLIMA/ *.SCP` | ✅ tiles, attributes · 🟡 island header | [scp.md](scp.md) | `engine/assets/island.*`, `engine/sim/island_map.*` |
| Scenarios / savegames | `Szenes/*.SZS *.SZM`, `SAVEGAME/*.GAM` | 🟡 all chunks of the savegame read and written (byte for byte round trip); open: scenario attitude in `PLAYER4`, AI mission fields in `SHIP4`, parts of `TIMERS`, the `SZENE` chunks of the scenarios (read: island list, `RANDTAB`, mission number) | [gam.md](gam.md), [savegame-writer.md](../re/savegame-writer.md) | `engine/assets/scenario.*`, `savegame.*`, `engine/sim/world.*`, `savegame_writer.*` |
| Open1602 savegame | `*.o1602` | ✅ (Open1602, own format) | [o1602-save.md](o1602-save.md) | `engine/sim/save_game.*` |
| Own chunk | `O1602`, `O1602MAP` in exported `.GAM` | ✅ (Open1602) | [o1602-chunk.md](o1602-chunk.md) | `engine/sim/savegame_extension.*` |
| Figures | `figuren.cod` (FIGUR) | 🟡 drawing, walking and combat data, `FORMATION` (chosen with the formation key, ADR 0010) | [gam.md](gam.md#how-ships-are-drawn), [figures.md](../re/figures.md) | `engine/assets/figure_table.*` |
| UI gadgets | `GADDATA/*.GAD`, `*.INC` | ✅ all 45 layouts of the game (not the editor's `Gadedit/`) | [gad.md](gad.md) | `engine/assets/gad.*`, `engine/ui/panel.*` |
| Videos | `VideoSmk/*.SMK`, `TOOLGFX/6s.smk` | ✅ Smacker 2 (image, palette, all audio types; `SMK4` only from the description) | [smk.md](smk.md) | `engine/video/smacker.*` |
| Audio | `SAMPLES/ Speech8/ MUSIC8/ *.wav` | ✅ PCM and the custom 4-bit music (an IMA variant) | [wav.md](wav.md) | `engine/audio/wav.*` |

All binary formats are little-endian. Texts are Windows-1252.
