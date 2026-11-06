# The History Edition's own data

Ubisoft's *Anno 1602 History Edition* (2019) is the Königsedition's data with a few changes and a
new renderer. Most files are the same byte for byte; what Open1602 has to know:

| What | Where | Open1602 |
|---|---|---|
| Files of a language | `Loca/<language>/` (`de-DE`: Text.cod, Editor.cod, ToolGfx/START.BSH, TOOLS.BSH, Editor.bsh, Samples, Speech8, VideoSmk); the English ones in the installation's own folders | `GameDirectory::resolve()` looks there first ([languages.md](../modding/languages.md#the-history-edition)) |
| Menu art in true colour | START.BSH, five buttons of TOOLS.BSH: record type 13 | [bsh.md](bsh.md#type-13-the-history-editions-true-colour-sprites) |
| A sixth main menu button (Options), two START.BSH sprites more, five TOOLS.BSH sprites more | `GADDATA/*.GAD`, `TOOLS.INC` (the numbers follow) | the menu counts its buttons by BASE.GAD; Options opens Open1602's window "Einstellungen" |
| Colours with a mark: `COL_RED = 249c` | `TOOLS.INC`, `MARKT.GAD` | [cod.md](cod.md#4-syntax-of-a-line-cod_parseline) |
| Scenarios named by numbers: `Szenes/[[7]].szs` is "Das Atoll", `[[48]]12.szs` "Endlosspiel12" | `Szenes/` | the names come from texts.xml (below) |
| Placeholders `[[N]]` in texts | Text.cod, the missions of the scenarios (`AUFTRAG4`: `[[8]]`) | replaced by `GameDirectory::expand()` |
| Two help lines inserted into Text.cod's `[LEISTE]`: `[[676]]` after "Auftrag ansehen", `[[807]]` after "Einheit auflösen" (the later lines count one or two further) | Text.cod | left out, so the lines keep the original's numbers |
| Buttons for its own settings screen (OPTION.GAD 47016) and for a new savegame (DISK.GAD 48014) | `GADDATA/` | left out (`kUnusedHistoryButtons`) |
| Ten savegame rows instead of twelve, with arrows of their own (DISK.GAD 48016, 48017) | `GADDATA/DISK.GAD` | the list counts its rows and scrolls with these arrows |
| Videos in Bink 2 (`VideoSmk/*.bk2`) | `VideoSmk/`, `Loca/<language>/VideoSmk/` | played (Bink 2 decoder, [bk2.md](bk2.md)) |
| `Game.dat` (sound and video settings, no savegame names), `Savegame/*.gam`, `Highscore/` in the user's `Documents/Anno 1602 History Edition/` (below Wine in the prefix: `drive_c/users/<user>/Documents/...`) | – | `GameDirectory::resolve()` looks there after the installation: `--savegame neue` loads `Savegame/neue.gam` |
| Names as placeholders in savegames: `NAME` "[[48]]" (Endlosspiel), the mission text "[[353]]" | `*.gam` | filled in with the History Edition's texts; with the Königsedition they stay as they are |
| No `Language.dll` | – | the system's mouse cursors |

The scenario files themselves carry no name. Compared with the Königsedition's they leave out
the empty chunks.

## ahe/a1he0.rda

An archive of the later Anno games, version 2.2. All numbers little-endian.

```c
char     magic[18];          // "Resource File V2.2"
uint8_t  unused[766];        // up to 0x310
uint64_t first_block;        // offset of the first block header

struct BlockHeader {         // 32 bytes
    uint32_t flags;          // 1 compressed, 2 encrypted, 4 memory resident, 8 deleted
    uint32_t files;
    uint64_t directory_stored;   // bytes of the directory in the archive
    uint64_t directory_size;     // after decompression
    uint64_t next_block;         // the last block's points at the end of the file
};
// A block's directory lies right before its header (at header - directory_stored).

struct Entry {               // 560 bytes
    char16_t name[260];      // UTF-16LE, ends with 0, '/' between folders
    uint64_t offset;         // of the data in the archive
    uint64_t stored;         // bytes in the archive
    uint64_t size;           // bytes of the file
    uint64_t timestamp;      // 0
    uint64_t unknown;        // 0
};
```

The History Edition's archive has six blocks, all with flags 0 (nothing compressed or
encrypted), 2 044 files, nearly all its new interface (`data/ui/...`). Open1602 reads the
directories of such blocks only and refuses a broken block chain or a directory that does not
fit its block.

## data/texts.xml

UTF-8 without a byte order mark, CR LF line ends:

```xml
<root>
<languages><language languageID="1">en_us</language><language languageID="16">de_de</language>...</languages>
<texts>
<line lineID="7"><translation languageID="1">Atoll</translation><translation languageID="16">Das Atoll</translation>...</line>
...
</texts></root>
```

14 languages (1 en_us, 9 cs_cz, 11 nl_NL, 15 fr_fr, 16 de_de, 17 it_IT, 18 jp_JP, 19 ko_KO, 21
pl_pl, 22 ru_ru, 23 sp_sp, 27 ch_ch, 28 ch_TW, 29 hu_hu), 727 lines numbered 1 to 809. Texts may
span several lines. Open1602 takes the language of the language folder in use, else English.
Several lines can have the same text: "Endlosspiel" is both 48 (one player, `.szs`) and 363 (several
players, `.szm`); a scenario looked up by its name is the file of one of them that exists.
