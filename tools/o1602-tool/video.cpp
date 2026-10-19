// SPDX-License-Identifier: GPL-3.0-or-later
//
// o1602-tool: Smacker videos (header, frames as PNG, a sound track as WAVE).

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "tool.hpp"

#include "o1602/core/file.hpp"
#include "o1602/core/png_writer.hpp"
#include "o1602/video/smacker.hpp"

namespace o1602::tool {

namespace {

/// 16-bit PCM as a RIFF WAVE file.
std::vector<std::uint8_t> wave_bytes(const audio::Pcm& pcm) {
    std::vector<std::uint8_t> out;
    const auto put32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        }
    };
    const auto put16 = [&](std::uint32_t v) {
        out.push_back(static_cast<std::uint8_t>(v));
        out.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    const auto data = static_cast<std::uint32_t>(pcm.samples.size() * 2);
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    put32(36 + data);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(16);
    put16(1);
    put16(pcm.channels);
    put32(pcm.rate);
    put32(pcm.rate * pcm.channels * 2U);
    put16(pcm.channels * 2U);
    put16(16);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    put32(data);
    for (const std::int16_t v : pcm.samples) {
        put16(static_cast<std::uint16_t>(v));
    }
    return out;
}

}  // namespace

int cmd_smk(const Args& args) {
    if (args.positional.empty()) {
        return usage();
    }
    auto bytes = core::read_file(fs::path(args.positional[0]));
    if (!bytes) {
        return fail(bytes.error());
    }
    auto video = video::Smacker::open(std::move(*bytes));
    if (!video) {
        return fail(video.error());
    }
    const auto& info = video->info();
    std::printf("SMK%u %ux%u, %u frames, %.3f fps%s%s%s\n", info.version, info.width, info.height, info.frames,
                1e6 / static_cast<double>(info.frame_us), info.ring_frame ? ", ring frame" : "",
                info.interlaced ? ", interlaced" : "", info.doubled ? ", doubled" : "");
    for (std::size_t t = 0; t < video::kSmackerTracks; ++t) {
        const auto& track = info.tracks[t];
        if (track.present) {
            std::printf("track %zu: %u Hz, %s, %d-bit%s\n", t, track.rate, track.stereo ? "stereo" : "mono",
                        track.bits16 ? 16 : 8, track.compressed ? ", compressed" : "");
        }
    }
    const auto png_dir = args.options.find("--png");
    const std::size_t every = std::max<std::size_t>(args.number("--every").value_or(1), 1);
    std::vector<std::uint8_t> rgba;
    std::size_t palettes = 0;
    while (!video->finished()) {
        const std::uint32_t frame = video->position();
        if (auto st = video->next(); !st) {
            return fail(st.error());
        }
        palettes += video->palette_changed() ? 1U : 0U;
        if (png_dir != args.options.end() && frame % every == 0) {
            video->to_rgba(rgba);
            char name[32];
            std::snprintf(name, sizeof name, "frame%04u.png", frame);
            if (auto st =
                    core::write_png_rgba(fs::path(png_dir->second) / name, info.width, info.display_height(), rgba);
                !st) {
                return fail(st.error());
            }
        }
    }
    std::printf("decoded %u frames, %zu palette changes\n", info.frames, palettes);
    if (const auto it = args.options.find("--wav"); it != args.options.end()) {
        if (args.positional.size() < 2) {
            return usage();
        }
        auto pcm = video->audio(static_cast<std::size_t>(std::stoul(std::string(it->second))));
        if (!pcm) {
            return fail(pcm.error());
        }
        std::printf("track: %zu frames, %.2f s\n", pcm->frames(), static_cast<double>(pcm->duration_ms()) / 1000.0);
        if (auto st = core::write_file(fs::path(args.positional[1]), wave_bytes(*pcm)); !st) {
            return fail(st.error());
        }
    }
    return 0;
}

}  // namespace o1602::tool
