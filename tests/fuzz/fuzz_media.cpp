// SPDX-License-Identifier: GPL-3.0-or-later
// The media decoders: Smacker videos (header, Huffman trees, every frame, RGBA, the audio tracks)
// and WAVE files (PCM, IMA ADPCM). The first byte picks the decoder: even = Smacker, odd = WAVE.
// A Smacker file declares its picture size and frame count in the header; the decoder must refuse
// what it cannot hold instead of allocating it.
#include <cstdint>
#include <vector>

#include "fuzz_support.hpp"

#include "o1602/audio/wav.hpp"
#include "o1602/video/smacker.hpp"

using namespace o1602;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < 1) {
        return 0;
    }
    std::vector<std::uint8_t> bytes(data + 1, data + size);
    if ((data[0] & 1U) != 0) {
        if (auto pcm = audio::decode_wav(bytes)) {
            fuzz::require(pcm->channels == 0 || pcm->samples.size() % pcm->channels == 0, "whole frames");
        }
        return 0;
    }
    auto video = video::Smacker::open(std::move(bytes));
    if (!video) {
        return 0;
    }
    std::vector<std::uint8_t> rgba;
    for (int frame = 0; frame < 64 && !video->finished(); ++frame) {
        if (!video->next().ok()) {
            break;
        }
        video->to_rgba(rgba);
    }
    video->rewind();
    (void)video->next();
    for (std::size_t track = 0; track < video::kSmackerTracks; ++track) {
        (void)video->audio(track);
    }
    return 0;
}
