// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shows the original's Smacker videos (docs/re/videos.md): the logos and the intro at the start,
// the videos of messages, victory and defeat, and the pause video. The picture fills the window
// in black with the video in the middle, twice as large as the original at 1024 pixels width and
// grown with the window, never larger than it; the sound goes to GameSound. One video at a time.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "o1602/assets/game_directory.hpp"
#include "o1602/video/smacker.hpp"

struct SDL_Renderer;
struct SDL_Texture;

namespace o1602::app {

class VideoPlayer {
public:
    VideoPlayer(SDL_Renderer* renderer, const assets::GameDirectory& game);
    ~VideoPlayer();
    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    /// Whether `VideoSmk/<number>.smk` exists (the installation may miss some, the original names
    /// two that GOG does not have).
    [[nodiscard]] bool available(std::uint16_t number) const;

    /// Opens a video and shows its first frame at `now_ms`; a running one stops. False if the
    /// file is missing or damaged.
    bool start(std::uint16_t number, bool loop, std::uint64_t now_ms);
    /// The first frame stays until `ms`: the video's sound comes after what the device holds.
    void hold_until(std::uint64_t ms) noexcept;
    void stop();
    [[nodiscard]] bool playing() const noexcept { return m_video.has_value(); }
    [[nodiscard]] std::uint16_t number() const noexcept { return m_number; }
    [[nodiscard]] const video::Smacker* video() const noexcept { return m_video ? &*m_video : nullptr; }

    enum class Step : std::uint8_t { Playing, Restarted, Ended };
    /// Decodes up to the frame of `now_ms`. The last frame stays for its whole time (the original
    /// closes on it, U-023); a looping video starts again (its sound must be restarted).
    Step update(std::uint64_t now_ms);
    /// Draws the current frame into a window of `width` x `height` pixels.
    void draw(std::int32_t width, std::int32_t height);
    /// Draws the current frame as large as it fits into a rectangle of the screen, on black (the
    /// minimap's window, ADR 0010).
    void draw_in(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
    /// The render device was reset: the texture is made again, the current frame uploaded anew.
    void recreate_texture();
    /// The device was lost: the texture goes before the renderer does ...
    void release_texture();
    /// ... and is made again with the new one, the current frame uploaded anew.
    void set_renderer(SDL_Renderer* renderer);

private:
    /// A texture of the video's size; the one before goes.
    void make_texture(std::int32_t w, std::int32_t h);
    SDL_Renderer* m_renderer;
    const assets::GameDirectory* m_game;
    mutable std::map<std::uint16_t, bool> m_available;
    std::optional<video::Smacker> m_video;
    std::uint16_t m_number = 0;
    bool m_loop = false;
    std::uint64_t m_start_ms = 0;
    SDL_Texture* m_texture = nullptr;
    std::int32_t m_texture_w = 0;
    std::int32_t m_texture_h = 0;
    std::vector<std::uint8_t> m_rgba;
    bool m_dirty = false;
};

}  // namespace o1602::app
