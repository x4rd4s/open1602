// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sounds of a running game (docs/re/audio.md): turns the simulation's events, the scenery's
// sounds and the player's orders into effects, voice messages and music, and feeds the device.
// Each computer plays its own sounds; nothing here changes the game.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/audio/game_audio.hpp"
#include "o1602/platform/audio_output.hpp"
#include "o1602/render/ambient.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/video/smacker.hpp"

namespace o1602::app {

class GameSound {
public:
    /// Opens the sound device; without one (or with neither sound nor music wanted) the game stays
    /// silent and every call does nothing.
    GameSound(const assets::GameDirectory& game, bool effects, bool music);
    ~GameSound();
    GameSound(const GameSound&) = delete;
    GameSound& operator=(const GameSound&) = delete;

    [[nodiscard]] bool active() const noexcept { return m_output != nullptr; }

    /// Where world tiles are on screen this frame: the offset from the middle of the view in view
    /// half sizes (-1 .. 1 on screen), and the view's size in pixels.
    using Locator = std::function<std::pair<float, float>(double tile_x, double tile_y)>;
    void set_view(Locator locate, float width_px, float height_px);

    /// A sound at a world tile (WAV_* number).
    void at(audio::SampleId sample, double tile_x, double tile_y);
    /// A sound with variants (`sample` + 0 .. variants - 1).
    void at(audio::SampleId sample, std::uint8_t variants, double tile_x, double tile_y);
    /// A click on the panel.
    void click();

    /// The settings of the sounds (the panel MUSIK and ANNO); switching sound or music on opens
    /// the device if the game started without it.
    [[nodiscard]] const audio::AudioSettings& settings() const noexcept { return m_settings; }
    void set_settings(const audio::AudioSettings& settings);
    /// Titles of the music (empty without device) and the one playing now.
    [[nodiscard]] std::vector<std::string> tracks() const;
    [[nodiscard]] std::string playing() const;
    void play_track(const std::string& title);
    /// The test sound of the effects' volume, in the middle of the view.
    void test_effects();
    /// A message for the local player at a world tile (nothing: no place), about another player
    /// (a rival's new city or ship; nothing: the local player). Returns its flags.
    std::uint16_t message(std::uint16_t message, std::optional<std::pair<double, double>> tile = std::nullopt,
                          std::optional<sim::PlayerId> about = std::nullopt);
    /// One of `count` voices from `base` (every second file), without a message (the menu).
    void speak(std::uint16_t base, std::uint8_t count);
    /// Hears every message with its flags (the status bar's icons in the colour of `about`, normal speed).
    using Listener =
        std::function<void(std::uint16_t message, std::uint16_t flags, std::optional<std::pair<double, double>> tile,
                           std::optional<sim::PlayerId> about)>;
    void set_listener(Listener listener) { m_listener = std::move(listener); }

    /// Messages pick their videos when `check` says the number can be played (without: never).
    void set_video_check(std::function<bool(std::uint16_t)> check) { m_voices.set_video_check(std::move(check)); }
    /// The video a message asked for, once.
    [[nodiscard]] std::optional<std::uint16_t> take_video() noexcept { return m_voices.take_video(); }
    /// A video starts (docs/re/videos.md): its tracks play at the effects' volume (bit i of
    /// `tracks` = track i, 0 = all), the music stops, voices and music wait until stop_video().
    void start_video(const video::Smacker& video, std::uint8_t tracks);
    void stop_video();

    // Events of the simulation that sound, for the local player `local`.
    void construction(const sim::World& world, const std::vector<sim::ConstructionEvent>& events);
    void combat(const sim::World& world, const assets::FigureTable& figures,
                const std::vector<sim::MilitaryEvent>& events, sim::PlayerId local, std::uint64_t game_ms);
    void ships(const sim::World& world, const assets::FigureTable& figures, const std::vector<sim::ShipEvent>& events,
               sim::PlayerId local, std::uint64_t game_ms);
    void scenery(const std::vector<render::AmbientSound>& sounds);
    /// Work and animal sounds (AnimSmp) of the figures on screen: once per cycle of an animation
    /// and figure type, at the first figure of that type in view, only at normal speed
    /// (Figure_PlayAnimSound 0x0044F970).
    void figures(const std::vector<const std::vector<render::FigureInstance>*>& lists,
                 const std::vector<render::EffectFigure>& effects, const assets::FigureTable& table,
                 std::uint64_t game_ms, std::uint32_t speed);
    /// Move or attack order for a ship (true) or soldiers at a tile.
    void order(bool ship, bool attack, double tile_x, double tile_y);

    /// Every frame: plays what was asked for, the next voice, the music, and tops up the device.
    void frame(std::uint64_t game_ms, std::uint32_t real_ms);
    /// Before a known pause of the frames (loading a game, a zoom level, a video): fills the
    /// device's queue for longer.
    void prefill();
    /// How long the device plays what it holds now: a sound started now is heard that much later.
    [[nodiscard]] std::uint32_t queued_ms() const noexcept;

private:
    /// Opens the device and the music; false without a device.
    bool open_device();
    /// Mixes until the device holds `ms` of sound.
    void top_up(std::uint32_t ms);

    const assets::GameDirectory* m_game;
    std::unique_ptr<platform::AudioOutput> m_output;
    audio::Mixer m_mixer;
    audio::SoundBank m_bank;
    audio::Effects m_effects;
    audio::Voices m_voices;
    std::unique_ptr<audio::Music> m_music;
    audio::AudioSettings m_settings;
    Locator m_locate;
    Listener m_listener;
    float m_view_w = 1024.0F;
    float m_view_h = 768.0F;
    std::uint32_t m_variant = 0;  ///< own counter for the variants: no game random numbers (S-112)
    std::vector<std::int16_t> m_buffer;
    std::uint32_t m_queue_ms = 120;  ///< what the device holds now (kQueueMs, more after a slow frame)
    std::vector<audio::Mixer::VoiceId> m_video_voices;
    bool m_video = false;
    std::map<std::pair<std::uint16_t, std::uint8_t>, std::uint64_t> m_cycles;  ///< figure, anim -> last cycle heard
};

}  // namespace o1602::app
