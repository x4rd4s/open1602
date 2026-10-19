// SPDX-License-Identifier: GPL-3.0-or-later
// Sound files and the mixer (docs/formats/wav.md, docs/re/audio.md).

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/audio/game_audio.hpp"
#include "o1602/audio/mixer.hpp"
#include "o1602/audio/wav.hpp"
#include "o1602/core/file.hpp"

using namespace o1602;

namespace {

void put16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
}
void put32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    put16(v, static_cast<std::uint16_t>(x));
    put16(v, static_cast<std::uint16_t>(x >> 16));
}
void tag(std::vector<std::uint8_t>& v, const char* t) {
    v.insert(v.end(), t, t + 4);
}

/// A WAVE file with the given fmt fields and data.
std::vector<std::uint8_t> wave(std::uint16_t format, std::uint16_t channels, std::uint32_t rate, std::uint16_t bits,
                               const std::vector<std::uint8_t>& data, std::uint16_t block_align = 0) {
    std::vector<std::uint8_t> v;
    tag(v, "RIFF");
    put32(v, static_cast<std::uint32_t>(4 + 8 + 16 + 8 + data.size()));
    tag(v, "WAVE");
    tag(v, "fmt ");
    put32(v, 16);
    put16(v, format);
    put16(v, channels);
    put32(v, rate);
    put32(v, rate * channels * bits / 8);
    put16(v, block_align != 0 ? block_align : static_cast<std::uint16_t>(channels * bits / 8));
    put16(v, bits);
    tag(v, "data");
    put32(v, static_cast<std::uint32_t>(data.size()));
    v.insert(v.end(), data.begin(), data.end());
    return v;
}

std::shared_ptr<audio::Pcm> constant(std::uint32_t rate, std::size_t frames, std::int16_t value) {
    auto p = std::make_shared<audio::Pcm>();
    p->rate = rate;
    p->channels = 1;
    p->samples.assign(frames, value);
    return p;
}

}  // namespace

TEST_CASE("audio: 8-bit PCM as in SAMPLES and Speech8") {
    const auto bytes = wave(1, 1, 22050, 8, {128, 255, 0, 192});
    auto pcm = audio::decode_wav(bytes);
    REQUIRE(pcm.ok());
    CHECK(pcm->rate == 22050);
    CHECK(pcm->channels == 1);
    REQUIRE(pcm->samples.size() == 4);
    CHECK(pcm->samples[0] == 0);
    CHECK(pcm->samples[1] == 127 * 256);
    CHECK(pcm->samples[2] == -128 * 256);
    CHECK(pcm->samples[3] == 64 * 256);
    CHECK(pcm->duration_ms() == 0);
}

TEST_CASE("audio: damaged and unknown files fail without crashing") {
    CHECK(!audio::decode_wav(std::vector<std::uint8_t>{1, 2, 3}).ok());
    auto bytes = wave(1, 1, 22050, 8, {128, 128});
    bytes.resize(30);  // cut inside the fmt chunk
    CHECK(!audio::decode_wav(bytes).ok());
    CHECK(!audio::decode_wav(wave(0x55, 2, 22050, 16, {0, 0, 0, 0})).ok());  // MP3
    CHECK(!audio::decode_wav(wave(1, 7, 22050, 8, {0})).ok());
}

TEST_CASE("audio: the mixer resamples, pans and clips") {
    audio::Mixer m(44100, 4);
    // A 22050 Hz sound lasts twice as many output frames.
    const auto id = m.play(constant(22050, 100, 10000));
    REQUIRE(id != audio::Mixer::kNoVoice);
    std::vector<std::int16_t> out(2 * 150);
    m.mix(out.data(), 150);
    CHECK(out[0] == 10000);
    CHECK(out[1] == 10000);
    CHECK(m.playing(id));
    m.mix(out.data(), 60);
    CHECK(!m.playing(id));
    // Hard left.
    audio::PlayOptions left;
    left.pan = -1.0F;
    m.play(constant(44100, 10, 8000), left);
    m.mix(out.data(), 1);
    CHECK(out[0] == 8000);
    CHECK(out[1] == 0);
    // Two loud sounds clip instead of wrapping around.
    m.play(constant(44100, 10, 30000));
    m.play(constant(44100, 10, 30000));
    m.mix(out.data(), 1);
    CHECK(out[0] == 32767);
}

TEST_CASE("audio: group volumes; a full mixer drops the oldest effect, never the music") {
    audio::Mixer m(22050, 2);
    audio::PlayOptions music;
    music.group = audio::Group::Music;
    music.loop = true;
    const auto song = m.play(constant(22050, 4, 1000), music);
    const auto first = m.play(constant(22050, 1000, 1000));
    const auto second = m.play(constant(22050, 1000, 1000));
    CHECK(m.playing(song));
    CHECK(!m.playing(first));
    CHECK(m.playing(second));
    std::vector<std::int16_t> out(2 * 16);
    m.mix(out.data(), 16);  // the music loops
    CHECK(m.playing(song));
    m.stop_group(audio::Group::Effects);
    m.set_group_volume(audio::Group::Effects, 0.5F);
    m.play(constant(22050, 10, 2000));
    m.stop(song);
    m.mix(out.data(), 1);
    CHECK(out[0] == 1000);
    CHECK(m.playing_count(audio::Group::Music) == 0);
}

namespace {

/// A game directory in the temporary folder with the files GameDirectory asks for, some sounds
/// and music.
struct FakeGame {
    std::filesystem::path root;
    FakeGame() {
        root = test::temp_path("o1602_audio_test");
        std::filesystem::remove_all(root);
        for (const char* d : {"toolgfx", "gfx", "SAMPLES", "Speech8", "MUSIC8"}) {
            std::filesystem::create_directories(root / d);
        }
        for (const char* f : {"haeuser.cod", "figuren.cod", "toolgfx/stadtfld.col", "gfx/stadtfld.bsh"}) {
            std::ofstream(root / f) << "x";
        }
        const auto tone = wave(1, 1, 22050, 8, std::vector<std::uint8_t>(2205, 200));  // 0.1 s
        write("SAMPLES/Event.wav", tone);
        write("Speech8/282.wav", tone);
        write("Speech8/284.wav", tone);
        write("Speech8/286.wav", tone);
        for (const char* t : {"b Song", "A Song", "c song", "Fight1", "Fight2"}) {
            write(std::string("MUSIC8/") + t + ".wav", wave(1, 2, 22050, 4, std::vector<std::uint8_t>(2205, 0x11), 4));
        }
    }
    ~FakeGame() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    void write(const std::string& path, const std::vector<std::uint8_t>& bytes) const {
        REQUIRE(core::write_file(root / path, bytes).ok());
    }
};

}  // namespace

TEST_CASE("audio: the 4-bit music of MUSIC8 (IMA steps without the rounding term)") {
    // 0x7F: left nibble 7 (step 7: (28 + 14 + 7) >> 2 = 12), right nibble 15 (-12).
    auto pcm = audio::decode_wav(wave(1, 2, 22050, 4, {0x7F, 0x00}, 4));
    REQUIRE(pcm.ok());
    CHECK(pcm->channels == 2);
    REQUIRE(pcm->samples.size() == 4);
    CHECK(pcm->samples[0] == 12);
    CHECK(pcm->samples[1] == -12);
    // Nibble 0 after index 8 (step 16): +16 >> 2 * 0 = 0, index back to 7.
    CHECK(pcm->samples[2] == 12);
    CHECK(pcm->samples[3] == -12);
}

TEST_CASE("audio: sound files and the settings of Game.dat") {
    CHECK(audio::sample_file(1) == std::optional<std::string>("SAMPLES/kanone5.wav"));
    CHECK(audio::sample_file(54) == audio::sample_file(55));
    CHECK(!audio::sample_file(0));
    CHECK(!audio::sample_file(60));
    CHECK(!audio::sample_file(200));
    CHECK(audio::sample_file(91) == std::optional<std::string>("SAMPLES/shpmove5.wav"));
    const auto s = audio::parse_audio_settings(
        "Objekt: OPTIONS\r\n  Musik: FALSE, 6\r\n  Samples: TRUE\r\n  Random: FALSE\r\n"
        "  Volume: -750, -200, -595\r\n  Speach: 5, TRUE\r\n  Video: 7, TRUE\r\n");
    CHECK(!s.music);
    CHECK(s.effects);
    CHECK(!s.random_music);
    CHECK(s.music_volume == -750);
    CHECK(s.effect_volume == -200);
    CHECK(!s.speech[5]);
    CHECK(s.speech[1]);
    CHECK(!s.video[7]);
    CHECK(s.video[5]);
    CHECK(std::fabs(audio::gain_of(-2000) - 0.1F) < 1e-4F);
    CHECK(audio::gain_of(0) == 1.0F);
}

TEST_CASE("audio: effects play inside the view only, pan with x, and ten per frame") {
    CHECK(!audio::Effects::placement(1.0F, 0.0F));
    CHECK(!audio::Effects::placement(0.0F, -1.0F));
    const auto centre = audio::Effects::placement(0.0F, 0.0F);
    REQUIRE(centre);
    CHECK(centre->first == 1.0F);
    CHECK(centre->second == 0.0F);
    const auto right = audio::Effects::placement(0.9F, 0.9F);
    REQUIRE(right);
    CHECK(right->first < 1.0F);
    CHECK(right->second > 0.8F);

    FakeGame fake;
    auto game = assets::GameDirectory::open(fake.root);
    REQUIRE(game.ok());
    audio::SoundBank bank(*game);
    audio::Mixer mixer(22050, 64);
    audio::Effects effects(mixer, bank);
    for (int i = 0; i < 15; ++i) {
        effects.request(audio::kWavEvent, 0.0F, 0.0F);
    }
    effects.request(audio::kWavEvent, 2.0F, 0.0F);
    effects.flush();
    CHECK(mixer.playing_count(audio::Group::Effects) == 10);
}

TEST_CASE("audio: a voice without a message (the menu's banner choice), not when speech is off") {
    FakeGame fake;
    auto game = assets::GameDirectory::open(fake.root);
    REQUIRE(game.ok());
    audio::SoundBank bank(*game);
    audio::Mixer mixer(22050, 8);
    audio::Effects effects(mixer, bank);
    audio::Voices voices(mixer, bank, effects);
    CHECK(voices.speak(107, 5));
    CHECK(voices.waiting() == 1);
    audio::AudioSettings s;
    s.speech[0] = false;
    voices.set_settings(s);
    CHECK(!voices.speak(107, 5));
    CHECK(voices.waiting() == 1);
}

TEST_CASE("audio: voice messages wait in four slots and speak one at a time") {
    FakeGame fake;
    auto game = assets::GameDirectory::open(fake.root);
    REQUIRE(game.ok());
    audio::SoundBank bank(*game);
    audio::Mixer mixer(22050, 8);
    audio::Effects effects(mixer, bank);
    audio::Voices voices(mixer, bank, effects);
    // Plague (message 1): voices 282, 284, 286; it asks for normal speed.
    CHECK((voices.message(1, true) & audio::kMessageNormalSpeed) != 0);
    for (int i = 0; i < 4; ++i) {
        voices.message(1, false);
    }
    CHECK(voices.waiting() == 4);
    effects.flush();  // the fifth found no room: event.wav
    CHECK(mixer.playing_count(audio::Group::Effects) == 1);
    voices.update();
    CHECK(voices.speaking());
    CHECK(voices.waiting() == 3);
    voices.update();
    CHECK(voices.waiting() == 3);  // never cut off
    std::vector<std::int16_t> out(2 * 4000);
    mixer.mix(out.data(), 4000);
    voices.update();
    CHECK(voices.waiting() == 2);
    // Voices of a kind switched off: only the sound.
    audio::AudioSettings s;
    s.speech[1] = false;
    voices.set_settings(s);
    voices.stop_all();
    voices.message(2, false);
    CHECK(voices.waiting() == 0);
}

TEST_CASE("audio: messages with a video play it instead of their voice (Msg_Audio 0x00488E80)") {
    FakeGame fake;
    auto game = assets::GameDirectory::open(fake.root);
    REQUIRE(game.ok());
    audio::SoundBank bank(*game);
    audio::Mixer mixer(22050, 8);
    audio::Effects effects(mixer, bank);
    audio::Voices voices(mixer, bank, effects);
    // Without a way to play videos (the viewer without --videos, the tests): the voice.
    voices.message(1, false);
    CHECK(!voices.take_video());
    CHECK(voices.waiting() == 1);
    voices.stop_all();

    std::vector<std::uint16_t> asked;
    voices.set_video_check([&](std::uint16_t n) {
        asked.push_back(n);
        return n != 703;  // the original names 703.smk, GOG has none
    });
    // Plague off screen: its video 282, no voice.
    voices.message(1, false);
    CHECK(voices.take_video() == std::optional<std::uint16_t>(282));
    CHECK(!voices.take_video());
    CHECK(voices.waiting() == 0);
    // On screen it speaks (no flag 0x01 or 0x08).
    voices.message(1, true);
    CHECK(!voices.take_video());
    CHECK(voices.waiting() == 1);
    voices.stop_all();
    // Flag 0x01 (the cathedral, 0x43): the video also on screen.
    voices.message(0x43, true);
    CHECK(voices.take_video() == std::optional<std::uint16_t>(262));
    // Flag 0x08 (ore found, 0x2B): on screen at about a quarter of the times.
    int videos = 0;
    for (int i = 0; i < 400; ++i) {
        voices.message(0x2B, true);
        videos += voices.take_video() ? 1 : 0;
        voices.stop_all();
    }
    CHECK(videos > 60);
    CHECK(videos < 140);
    // A missing video: the voice instead (the original stays silent, U-024).
    voices.message(8, false);
    CHECK(!voices.take_video());
    CHECK(voices.waiting() == 1);
    voices.stop_all();
    // Videos of a kind switched off (kind 1, Ereignisse): the voice.
    audio::AudioSettings s;
    s.video[1] = false;
    voices.set_settings(s);
    voices.message(1, false);
    CHECK(!voices.take_video());
    CHECK(voices.waiting() == 1);
    // The master switch blocks kind 0 too (mission success 0x5F, video 2000).
    s.video[1] = true;
    s.video[0] = false;
    voices.set_settings(s);
    voices.message(0x5F, false);
    CHECK(!voices.take_video());
}

TEST_CASE("audio: music by name, battle music near shots, back after 25 s with a fade") {
    FakeGame fake;
    auto game = assets::GameDirectory::open(fake.root);
    REQUIRE(game.ok());
    std::vector<std::string> tracks;
    std::vector<std::string> fights;
    audio::Music::scan(*game, tracks, fights);
    CHECK((tracks == std::vector<std::string>{"A Song", "b Song", "c song"}));
    CHECK((fights == std::vector<std::string>{"Fight1", "Fight2"}));
    audio::SoundBank bank(*game);
    audio::Mixer mixer(22050, 8);
    audio::Music music(mixer, bank, tracks, fights);
    audio::AudioSettings s;
    s.random_music = false;
    music.set_settings(s);
    music.update(0, 16);
    CHECK(music.playing() == "A Song");
    // A shot far away does nothing; one in the middle fades over to the battle music.
    music.shot(0.99F, 0.99F, 4000.0F, 4000.0F, 1000);
    CHECK(!music.battle());
    music.shot(0.1F, 0.1F, 1024.0F, 768.0F, 1000);
    CHECK(music.battle());
    music.update(1000, 1000);
    CHECK(music.playing() == "A Song");  // still fading
    music.update(2000, 3000);            // faded out: the battle music starts
    CHECK(music.playing().rfind("Fight", 0) == 0);
    music.update(27'000, 16);
    CHECK(!music.battle());
    music.update(27'000, 4000);
    CHECK(music.playing() == "b Song");
}
