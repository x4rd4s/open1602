// SPDX-License-Identifier: GPL-3.0-or-later
//
// o1602_fuzz_seeds <dir>
//
// Writes the seed corpora of the fuzz targets (tests/fuzz) into <dir>/<target>/, from synthetic
// files built like the unit tests build them (tests/unit/test_support.hpp, test_save_fuzz.cpp,
// test_video.cpp, test_audio.cpp, tests/tool/write_tool_files.cpp), so that no original data is
// needed. Seeds from the original data are added on a machine that has it (tests/fuzz/README.md).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "app_settings.hpp"
#include "endless_setup.hpp"
#include "fuzz_support.hpp"
#include "multiplayer_menu.hpp"
#include "test_support.hpp"

#include "o1602/assets/chunk_writer.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/core/file.hpp"
#include "o1602/net/lan.hpp"
#include "o1602/net/protocol.hpp"
#include "o1602/sim/command_io.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/savegame_writer.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/ui/key_bindings.hpp"

namespace fs = std::filesystem;
using namespace o1602;
using Bytes = std::vector<std::uint8_t>;

namespace {

fs::path g_dir;
int g_written = 0;

void seed(std::string_view target, std::string_view name, const Bytes& bytes) {
    const fs::path dir = g_dir / target;
    fs::create_directories(dir);
    if (auto status = core::write_file(dir / name, bytes); !status) {
        std::fprintf(stderr, "error: %s\n", status.error().message.c_str());
        std::exit(1);
    }
    ++g_written;
}

Bytes text_bytes(std::string_view text) {
    return {text.begin(), text.end()};
}

Bytes with_mode(std::uint8_t mode, const Bytes& rest) {
    Bytes out{mode};
    out.insert(out.end(), rest.begin(), rest.end());
    return out;
}

void put_u16(Bytes& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

// --- .cod ---

constexpr std::string_view kHaeuser =
    "Nahrung = 4\n"
    "Version: 2\n"
    "Objekt: HAUS\n"
    "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
    "  @Nummer: 1\n  Id: 21201\n  Kind: 19\n  Gfx: 1\n  Size: 1, 1\n"
    "  @Nummer: 2\n  Id: 20501\n  Kind: 23\n  Gfx: 2\n  Size: 1, 1\n  Destroyflg: 1\n"
    "  @Nummer: 3\n  Id: 20202\n  Kind: 10\n  Gfx: 3\n  Size: 1, 1\n"
    "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 53\n  EndObj\n"
    "  @Nummer: 4\n  Id: 20401\n  Kind: 1\n  Gfx: 4\n  Size: 1, 1\n  KreuzBase: 4\n"
    "  Objekt: HAUS_BAUKOST\n    Money: 5\n  EndObj\n"
    "  @Nummer: +1\n  Id: 20402\n  Kind: 1\n  Gfx: 5\n  Size: 1, 1\n  KreuzBase: 4\n"
    "  @Nummer: 12\n  Id: 22101\n  Kind: 35\n  Gfx: 20\n  Size: 2, 3\n  Strandflg: 1\n  PlaceFlg: 1\n"
    "  KreuzBase: 12\n  Ausbauflg: 1\n"
    "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 8\n    Maxlager: 30\n    Maxware: 4, 2\n  EndObj\n"
    "  Objekt: HAUS_BAUKOST\n    Werkzeug: 3\n    Holz: 4\n    Money: 100\n  EndObj\n"
    "  ObjFill: 0, 20\n"
    "EndObj\n";

constexpr std::string_view kFiguren =
    "Objekt: FIGUR\n"
    "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 4\n  Maxkanon: 6\n  Speed: 500\n"
    "  Drehtime: 0.13\n  Maxenergy: 50\n  Hitpoint: 2.0\n  Preis: 1000\n  Rohstoff: 23\n  Rohmenge: 25\n"
    "  Nummer: 101\n  Speed: 330\n  Worktime: 5.7\n  Rotate: 24\n  Gfx: 0\n"
    "  Objekt: ANIM\n    Nummer: 0\n    Kind: 1\n    AnimAnz: 15\n    AnimSpeed: 71\n  EndObj\n"
    "  Nummer: 70\n  Rotate: 8\n  Gfx: 0\n  Fahnoffs: 2.1, 0, 0.6\n"
    "  Objekt: ANIM\n    Nummer: 0\n    Kind: 1\n    AnimAnz: 8\n    AnimSpeed: 90\n  EndObj\n"
    "  Soldat: SOLDAT1, 10, 20\n"
    "EndObj\n";

void cod_seeds() {
    seed("cod", "haeuser", with_mode(0, text_bytes(kHaeuser)));
    seed("cod", "figuren", with_mode(1, text_bytes(kFiguren)));
    seed("cod", "haeuser-encoded", with_mode(2, assets::encode_cod(kHaeuser)));
    seed("cod", "figuren-encoded", with_mode(3, assets::encode_cod(kFiguren)));
}

// --- GAD ---

void gad_seeds() {
    constexpr std::string_view kInclude = "  BLK_TOOL = 0\n  GFXPFEIL = 1\n";
    constexpr std::string_view kLayout =
        "  Include: \"TOOLS.INC\"\n"
        "  Objekt: GADGET\n"
        "    @Nummer:    0\n    Id:         0\n    Blocknr:    BLK_TOOL\n    Kind:       GAD_UNUSED\n"
        "    ObjFill:    0,MAXGADGET\n"
        "    IDHAUPT = 500\n"
        "    @Nummer:    0\n    BASE = Nummer\n    Id:         IDHAUPT+0\n    Kind:       GAD_GFX\n"
        "    Gfxnr:      GFXPFEIL\n    Pressoff:   2\n    Flipflg:    1\n    Color:      1, 3\n"
        "    Pos:        10, 20\n    Size:       40, 30\n    Posoffs:    12-Pos[0], 25-Pos[1]\n"
        "    @Nummer:    +1\n    Id:         IDHAUPT+1\n    Kind:       GAD_TEXTZ\n    Color:      5\n"
        "    Reiheflg:   1\n    Basenr:     BASE+1\n    Pos:        0, 100\n    Size:       50, 20\n"
        "    @Nummer:    +1\n    Id:         IDHAUPT+2\n    Kind:       GAD_TEXTZ\n    Reiheflg:   1\n"
        "    Basenr:     BASE+1\n    @Pos:       +50\n    Size:       50, 20\n"
        "    @Nummer:    +1\n    Id:         IDHAUPT+3\n    Kind:       GAD_BALKEN\n    Slidhorflg: 1\n"
        "    Pos:        0, 200\n    Size:       100, 10\n"
        "  EndObj\n";
    Bytes b = text_bytes(kLayout);
    b.push_back(0);
    const Bytes inc = text_bytes(kInclude);
    b.insert(b.end(), inc.begin(), inc.end());
    seed("gad", "layout", b);
    seed("gad", "no-include", text_bytes(kLayout));
}

// --- sprites ---

void sprite_seeds() {
    seed("sprites", "sheet", test::sheet_bytes({{4, 2}, {8, 6}, {1, 1}}, 5));
    seed("sprites", "glyphs", test::sheet_bytes_each({{6, 9}, {8, 9}, {3, 9}, {7, 9}}, {1, 2, 3, 4}));
    // One 4x2 sprite with transparent runs (tests/tool/write_tool_files.cpp).
    const Bytes rle = {2, 2, 0xA, 0xB, 0xFE, 0, 1, 0xC, 0xFF};
    Bytes payload;
    test::put_u32(payload, 4);
    test::put_u32(payload, 4);
    test::put_u32(payload, 2);
    test::put_u32(payload, 1);
    const auto length = static_cast<std::uint32_t>((16 + rle.size() + 3) & ~std::size_t{3});
    test::put_u32(payload, length);
    payload.insert(payload.end(), rle.begin(), rle.end());
    payload.resize(4 + length, 0);
    Bytes file;
    test::put_chunk(file, "BSH", payload);
    seed("sprites", "rle", file);
    Bytes pal;
    for (int i = 0; i < 256; ++i) {
        pal.insert(pal.end(), {static_cast<std::uint8_t>(i), 0, 0, 0});
    }
    Bytes col;
    test::put_chunk(col, "COL", pal);
    seed("sprites", "palette", col);
}

// --- chunk files, .GAM ---

/// Island 3 (20x15 at 40,30) with two buildings, one human player with 12345 gold.
Bytes small_gam() {
    Bytes island = {3, 20, 15, 0};
    put_u16(island, 40);
    put_u16(island, 30);
    island.resize(116, 0);
    Bytes tiles;
    for (const std::uint8_t x : {std::uint8_t{2}, std::uint8_t{5}}) {
        put_u16(tiles, 1201);
        tiles.push_back(x);
        tiles.push_back(4);
        test::put_u32(tiles, 0);
    }
    Bytes player(0x430, 0);
    player[0] = 0x39;
    player[1] = 0x30;
    player[7] = 1;
    constexpr std::string_view kName = "Tester";
    std::memcpy(player.data() + 0x3C0, kName.data(), kName.size());
    assets::ChunkWriter writer;
    writer.add("INSEL5", island);
    writer.add("INSELHAUS", tiles);
    writer.add("PLAYER4", player);
    writer.add_checksum();
    return writer.take();
}

void chunk_seeds() {
    const Bytes gam = small_gam();
    seed("chunks", "small.gam", gam);
    seed("gam_world", "small.gam", gam);
    // The test world as the original's savegame, if the writer can make one without a source file.
    sim::World world = fuzz::make_world();
    if (auto written = sim::write_savegame(world, fuzz::buildings())) {
        seed("chunks", "world.gam", *written);
        seed("gam_world", "world.gam", *written);
    } else {
        std::fprintf(stderr, "note: no .GAM of the test world: %s\n", written.error().message.c_str());
    }
}

// --- .o1602 ---

void save_seeds() {
    sim::World world = fuzz::make_world();
    sim::Simulation simulation(world, fuzz::buildings(), fuzz::figures());
    for (int t = 0; t < 20; ++t) {
        simulation.tick();
    }
    auto plain = sim::write_game(world, fuzz::buildings(), &simulation, sim::SaveOptions{false});
    fuzz::require(plain.ok(), "uncompressed save");
    seed("o1602_save", "running.o1602", *plain);
    auto packed = sim::write_game(world, fuzz::buildings(), &simulation, sim::SaveOptions{true});
    fuzz::require(packed.ok(), "compressed save");
    seed("o1602_save", "running-zlib.o1602", *packed);
    sim::World fresh = fuzz::make_world();
    auto unrun = sim::write_game(fresh, fuzz::buildings(), nullptr, sim::SaveOptions{false});
    fuzz::require(unrun.ok(), "save without simulation");
    seed("o1602_save", "fresh.o1602", *unrun);
}

// --- commands ---

template <std::size_t I = 0>
void all_commands(std::vector<sim::Command>& out) {
    if constexpr (I < std::variant_size_v<sim::Command>) {
        std::variant_alternative_t<I, sim::Command> c{};
        if constexpr (requires { c.player; }) {
            c.player = 0;
        }
        out.emplace_back(std::move(c));
        all_commands<I + 1>(out);
    }
}

std::vector<sim::Command> sample_commands() {
    std::vector<sim::Command> commands;
    all_commands(commands);
    sim::BuildOrder build;
    build.player = 0;
    build.island = 0;
    build.items = {{fuzz::kRoad, 2, 2, 0, 0}, {fuzz::kRoad, 2, 3, 0, 0}, {fuzz::kHouse, 7, 6, 1, 0}};
    commands.emplace_back(build);
    sim::DemolishOrder demolish;
    demolish.player = 0;
    demolish.from = {3, 4};
    demolish.to = {6, 6};
    commands.emplace_back(demolish);
    sim::ShipOrder ship;
    ship.player = 0;
    ship.ship = 1;
    ship.target = sim::Target::position(60, 100);
    commands.emplace_back(ship);
    sim::PlayerRename rename;
    rename.player = 0;
    rename.name = "Neuer Name";
    commands.emplace_back(rename);
    return commands;
}

void command_seeds() {
    const auto commands = sample_commands();
    Bytes stream{0x02};  // fuzz_command_apply: three ticks between commands, submitted
    for (std::size_t i = 0; i < commands.size(); ++i) {
        const Bytes one = sim::encode_command(commands[i]);
        seed("command", "c" + std::to_string(i), one);
        seed("command_apply", "c" + std::to_string(i), with_mode(0x01, one));
        stream.insert(stream.end(), one.begin(), one.end());
    }
    seed("command_apply", "all", stream);
    core::ByteWriter scheduled;
    for (std::size_t i = 0; i < commands.size(); ++i) {
        sim::write_scheduled(scheduled, sim::ScheduledCommand{100 + i, commands[i]});
    }
    seed("command", "scheduled", scheduled.take());
}

// --- network ---

void net_seeds() {
    const auto commands = sample_commands();
    std::vector<net::Message> messages;
    net::Hello hello;
    hello.name = "Gast";
    hello.seat = 1;
    hello.password = "geheim";
    messages.emplace_back(hello);
    messages.emplace_back(net::CommandBatch{commands});
    messages.emplace_back(net::Progress{});
    messages.emplace_back(net::Welcome{});
    messages.emplace_back(net::Refused{});
    net::Snapshot snapshot;
    snapshot.save = Bytes(64, 7);
    messages.emplace_back(snapshot);
    net::Turn turn;
    turn.first = 10;
    turn.closed = 12;
    turn.frames = {net::FrameCommands{10, commands}, net::FrameCommands{11, {}}};
    messages.emplace_back(turn);
    net::Roster roster;
    roster.members = {net::Member{0, "Host", net::Member::State::Playing, true}, net::Member{1, "Gast"}};
    roster.successor = 1;
    roster.successor_address = "192.168.0.2:4321";
    messages.emplace_back(roster);
    messages.emplace_back(net::Desync{12, {0, 1}});
    net::Lobby lobby;
    lobby.title = "Spiel";
    lobby.game = "Endlosspiel";
    lobby.seats = {net::LobbySeat{net::LobbySeat::Kind::Human, "Host", true, 0},
                   net::LobbySeat{net::LobbySeat::Kind::Open, "", false, 1},
                   net::LobbySeat{net::LobbySeat::Kind::Computer, "", false, 2}};
    lobby.setup = app::lobby_setup(app::EndlessSetup::defaults(app::SetupScope::MultiplayerEndless),
                                   app::SetupScope::MultiplayerEndless);
    lobby.banners = {0, 1, 2, 3};
    messages.emplace_back(lobby);
    messages.emplace_back(net::TakeSeat{2});
    messages.emplace_back(net::SetReady{true});
    messages.emplace_back(net::Chat{0, "Host", "Hallo"});
    messages.emplace_back(net::Standby{{{0, 1234}, {1, 5678}}, {2}});
    messages.emplace_back(net::SetBanner{3});
    for (std::size_t i = 0; i < messages.size(); ++i) {
        seed("net_message", "m" + std::to_string(i), net::encode(messages[i]));
    }
    seed("net_message", "lan-search", net::encode_search(0x12345678));
    seed("net_message", "lan-answer", net::encode_answer(0x12345678, net::GameInfo{}));
    seed("net_message", "setup", lobby.setup);
}

// --- settings and other text files ---

void settings_seeds() {
    app::AppSettings settings;
    settings.player_name = "Spieler";
    settings.remembered_hosts = {"host.example:4321", "10.0.0.1"};
    seed("settings", "settings.cfg", text_bytes(app::write_settings(settings)));
    app::SaveSlots slots;
    seed("settings", "slots.cfg", text_bytes(app::write_slots(slots)));
    ui::KeyBindings keys;
    seed("settings", "keys.cfg", text_bytes(keys.write_text([](std::int32_t key, bool physical) {
             return physical ? "[" + std::to_string(key) + "]" : std::string(1, static_cast<char>(key & 0x7F));
         })));
    seed("settings", "lang", text_bytes("# comment\nmenu.start = Start\nmenu.quit = Ende\\nJetzt\n"));
    seed("settings", "hss", text_bytes("Objekt: HISCORE\n  Entry: 0, \"Name\", 1000, 3, 7\nEndObj\n"));
    seed("settings", "address", text_bytes("[::1]:4321"));
}

// --- media ---

void put32_at(Bytes& v, std::size_t at, std::uint32_t x) {
    for (std::size_t i = 0; i < 4; ++i) {
        v[at + i] = static_cast<std::uint8_t>(x >> (8 * i));
    }
}

Bytes wave(std::uint16_t format, std::uint16_t channels, std::uint32_t rate, std::uint16_t bits, const Bytes& data,
           std::uint16_t block_align = 0) {
    Bytes v;
    auto tag = [&](const char* t) { v.insert(v.end(), t, t + 4); };
    tag("RIFF");
    test::put_u32(v, static_cast<std::uint32_t>(4 + 8 + 16 + 8 + data.size()));
    tag("WAVE");
    tag("fmt ");
    test::put_u32(v, 16);
    put_u16(v, format);
    put_u16(v, channels);
    test::put_u32(v, rate);
    test::put_u32(v, rate * channels * bits / 8);
    put_u16(v, block_align != 0 ? block_align : static_cast<std::uint16_t>(channels * bits / 8));
    put_u16(v, bits);
    tag("data");
    test::put_u32(v, static_cast<std::uint32_t>(data.size()));
    v.insert(v.end(), data.begin(), data.end());
    return v;
}

class Bits {
public:
    void bit(std::uint32_t b) {
        if (m_used == 0) {
            m_bytes.push_back(0);
        }
        m_bytes.back() = static_cast<std::uint8_t>(m_bytes.back() | ((b & 1U) << m_used));
        m_used = (m_used + 1) % 8;
    }
    void bits(std::uint32_t v, int n) {
        for (int i = 0; i < n; ++i) {
            bit(v >> i);
        }
    }
    void single_leaf_tree(std::uint8_t value) {
        bit(1);
        bit(0);
        bits(value, 8);
        bit(0);
    }
    [[nodiscard]] const Bytes& bytes() const noexcept { return m_bytes; }

private:
    Bytes m_bytes;
    int m_used = 0;
};

/// All four trees absent but the block types (tests/unit/test_video.cpp).
Bytes constant_type_trees(std::uint16_t descriptor) {
    Bits b;
    b.bit(0);
    b.bit(0);
    b.bit(0);
    b.bit(1);
    b.single_leaf_tree(static_cast<std::uint8_t>(descriptor));
    b.single_leaf_tree(static_cast<std::uint8_t>(descriptor >> 8));
    b.bits(0xFFF0, 16);
    b.bits(0xFFF1, 16);
    b.bits(0xFFF2, 16);
    b.bit(0);
    b.bit(0);
    return b.bytes();
}

/// A Smacker file of two frames: a palette in the first, solid blocks, one 8-bit audio track.
Bytes smacker(std::uint32_t width, std::uint32_t height) {
    const Bytes trees = constant_type_trees(0x2A07);
    Bytes f(0x68, 0);
    std::memcpy(f.data(), "SMK2", 4);
    put32_at(f, 4, width);
    put32_at(f, 8, height);
    put32_at(f, 12, 2);
    put32_at(f, 16, static_cast<std::uint32_t>(-6666));
    put32_at(f, 52, static_cast<std::uint32_t>(trees.size()));
    for (std::size_t t = 0; t < 4; ++t) {
        put32_at(f, 56 + t * 4, 4096);
    }
    put32_at(f, 72, 0x40000000U | 22050U);  // track 0: present, 8-bit mono
    Bytes first;
    const Bytes palette = {0x3F, 0x00, 0x10, 0x80 | 40, 0x10, 0x20, 0x30};
    const std::size_t psize = (palette.size() + 1 + 3) / 4 * 4;
    first.push_back(static_cast<std::uint8_t>(psize / 4));
    first.insert(first.end(), palette.begin(), palette.end());
    first.resize(psize, 0);
    const Bytes audio = {0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87};
    const std::size_t at = first.size();
    first.resize(at + 4);
    put32_at(first, at, static_cast<std::uint32_t>(audio.size() + 4));
    first.insert(first.end(), audio.begin(), audio.end());
    first.resize((first.size() + 3) / 4 * 4, 0);
    Bytes second(4, 0);
    const std::size_t table = f.size();
    f.resize(table + 8);
    put32_at(f, table, static_cast<std::uint32_t>(first.size()) | 1U);
    put32_at(f, table + 4, static_cast<std::uint32_t>(second.size()));
    f.push_back(1 | 2);
    f.push_back(0);
    f.insert(f.end(), trees.begin(), trees.end());
    f.insert(f.end(), first.begin(), first.end());
    f.insert(f.end(), second.begin(), second.end());
    return f;
}

void media_seeds() {
    seed("media", "wav-pcm8", with_mode(1, wave(1, 1, 22050, 8, {0x80, 0x90, 0x70, 0x80})));
    seed("media", "wav-pcm16", with_mode(1, wave(1, 2, 44100, 16, {0, 1, 2, 3, 4, 5, 6, 7})));
    Bytes adpcm(4 + 8, 0x11);
    seed("media", "wav-ima", with_mode(1, wave(0x11, 1, 22050, 4, adpcm, 12)));
    seed("media", "smk-8x4", with_mode(0, smacker(8, 4)));
    seed("media", "smk-16x8", with_mode(0, smacker(16, 8)));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fputs("usage: o1602_fuzz_seeds <dir>\n", stderr);
        return 2;
    }
    g_dir = argv[1];
    cod_seeds();
    gad_seeds();
    sprite_seeds();
    chunk_seeds();
    save_seeds();
    command_seeds();
    net_seeds();
    settings_seeds();
    media_seeds();
    std::printf("wrote %d seeds to %s\n", g_written, g_dir.string().c_str());
    return 0;
}
