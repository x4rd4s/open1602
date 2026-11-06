// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/audio/wav.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>

#include "o1602/core/byte_reader.hpp"

namespace o1602::audio {

using core::ErrorCode;
using core::make_error;

namespace {

constexpr std::uint16_t kFormatPcm = 1;

struct Format {
    std::uint16_t tag = 0;
    std::uint16_t channels = 0;
    std::uint32_t rate = 0;
    std::uint16_t block_align = 0;
    std::uint16_t bits = 0;
};

bool tag_is(std::span<const std::uint8_t> bytes, std::size_t at, const char* tag) {
    return bytes.size() >= at + 4 && std::memcmp(bytes.data() + at, tag, 4) == 0;
}

core::Result<Pcm> decode_pcm(const Format& f, std::span<const std::uint8_t> data) {
    Pcm pcm;
    pcm.rate = f.rate;
    pcm.channels = static_cast<std::uint8_t>(f.channels);
    if (f.bits == 8) {
        pcm.samples.resize(data.size());
        for (std::size_t i = 0; i < data.size(); ++i) {
            pcm.samples[i] = static_cast<std::int16_t>((static_cast<std::int32_t>(data[i]) - 128) * 256);
        }
    } else if (f.bits == 16) {
        pcm.samples.resize(data.size() / 2);
        for (std::size_t i = 0; i < pcm.samples.size(); ++i) {
            pcm.samples[i] = static_cast<std::int16_t>(data[2 * i] | (data[2 * i + 1] << 8));
        }
    } else {
        return make_error(ErrorCode::Unsupported, "WAVE: " + std::to_string(f.bits) + "-bit PCM");
    }
    // Whole frames only.
    pcm.samples.resize(pcm.samples.size() - pcm.samples.size() % pcm.channels);
    return pcm;
}

// Step and index tables of IMA ADPCM (Intel/DVI), the standard ones.
constexpr std::array<std::int32_t, 89> kStep = {
    7,    8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,   28,
    31,   34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,  118,
    130,  143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,  494,
    544,  598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878, 2066,
    2272, 2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845, 8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
constexpr std::array<std::int32_t, 16> kIndexStep = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

/// The music of MUSIC8 (docs/formats/wav.md): one byte per stereo frame, high nibble left, IMA
/// steps without the rounding term of the reference code, one stream from the start of the file.
Pcm decode_music4(const Format& f, std::span<const std::uint8_t> data) {
    Pcm pcm;
    pcm.rate = f.rate;
    pcm.channels = 2;
    pcm.samples.resize(data.size() * 2);
    std::array<std::int32_t, 2> predictor{};
    std::array<std::int32_t, 2> index{};
    for (std::size_t i = 0; i < data.size(); ++i) {
        for (std::size_t c = 0; c < 2; ++c) {
            const std::uint32_t n = c == 0 ? data[i] >> 4 : data[i] & 0x0FU;
            const std::int32_t step = kStep[static_cast<std::size_t>(index[c])];
            index[c] = std::clamp(index[c] + kIndexStep[n], 0, 88);
            std::int32_t diff =
                (((n & 4U) != 0 ? 4 * step : 0) + ((n & 2U) != 0 ? 2 * step : 0) + ((n & 1U) != 0 ? step : 0)) >> 2;
            if ((n & 8U) != 0) {
                diff = -diff;
            }
            predictor[c] = std::clamp(predictor[c] + diff, -32768, 32767);
            pcm.samples[2 * i + c] = static_cast<std::int16_t>(predictor[c]);
        }
    }
    return pcm;
}

}  // namespace

core::Result<Pcm> decode_wav(std::span<const std::uint8_t> bytes) {
    if (!tag_is(bytes, 0, "RIFF") || !tag_is(bytes, 8, "WAVE")) {
        return make_error(ErrorCode::InvalidFormat, "not a RIFF WAVE file");
    }
    core::ByteReader r(bytes);
    r.seek(12);
    std::optional<Format> format;
    std::optional<std::span<const std::uint8_t>> data;
    while (r.remaining() >= 8) {
        const std::size_t at = r.offset();
        r.skip(4);
        const auto size = r.u32();
        if (!size) {
            break;
        }
        // A data chunk running past the end (some writers leave the size open) is cut to the file.
        const std::size_t n = std::min<std::size_t>(*size, r.remaining());
        const auto body = bytes.subspan(r.offset(), n);
        if (tag_is(bytes, at, "fmt ") && n >= 16) {
            core::ByteReader fr(body);
            Format f;
            f.tag = fr.u16().value_or(0);
            f.channels = fr.u16().value_or(0);
            f.rate = fr.u32().value_or(0);
            fr.skip(4);  // bytes per second
            f.block_align = fr.u16().value_or(0);
            f.bits = fr.u16().value_or(0);
            format = f;
        } else if (tag_is(bytes, at, "data")) {
            data = body;
        }
        r.skip(n + (n & 1U));  // chunks are padded to even sizes
    }
    if (!format || !data) {
        return make_error(ErrorCode::InvalidFormat, "WAVE without fmt or data chunk");
    }
    if (format->channels == 0 || format->channels > 2 || format->rate == 0) {
        return make_error(ErrorCode::Unsupported, "WAVE: unsupported channels or rate");
    }
    if (format->tag == kFormatPcm && format->bits == 4 && format->channels == 2) {
        return decode_music4(*format, *data);
    }
    if (format->tag == kFormatPcm) {
        return decode_pcm(*format, *data);
    }
    return make_error(ErrorCode::Unsupported,
                      "WAVE: encoding " + std::to_string(format->tag) + ", " + std::to_string(format->bits) + " bit");
}

}  // namespace o1602::audio
