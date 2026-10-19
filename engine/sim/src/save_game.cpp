// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/save_game.hpp"

#include <algorithm>
#include <array>

#include "save/sections.hpp"

#include "o1602/core/byte_reader.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/core/compress.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/validate.hpp"

namespace o1602::sim {

using core::ErrorCode;
using core::make_error;

namespace {

constexpr std::array<std::uint8_t, 8> kMagic = {'O', 'P', 'E', 'N', '1', '6', '0', '2'};
constexpr std::uint16_t kVersion = 1;
constexpr std::uint16_t kCompressed = 1;  ///< flag: the sections are a zlib stream
constexpr std::size_t kHeaderSize = 32;
/// More than any game needs; a header promising more is damaged (and would cost the memory).
constexpr std::uint64_t kMaxSize = 1ULL << 28;  // 256 MiB; a game on the largest map needs about 100 MB

struct Header {
    std::uint16_t version = 0;
    std::uint16_t flags = 0;
    std::uint64_t size = 0;    ///< sections, uncompressed
    std::uint32_t crc = 0;     ///< CRC-32 of the sections, uncompressed
    std::uint32_t stored = 0;  ///< bytes behind the header
};

core::Result<Header> read_header(std::span<const std::uint8_t> bytes) {
    if (!is_open1602_save(bytes)) {
        return make_error(ErrorCode::InvalidFormat, "not an Open1602 savegame");
    }
    core::ByteReader in(bytes.subspan(kMagic.size()));
    Header h;
    const auto version = in.u16();
    const auto flags = in.u16();
    const auto reserved = in.u32();
    const auto size = in.u64();
    const auto crc = in.u32();
    const auto stored = in.u32();
    if (!version || !flags || !reserved || !size || !crc || !stored) {
        return make_error(ErrorCode::InvalidFormat, "savegame: header too short");
    }
    h.version = *version;
    h.flags = *flags;
    h.size = *size;
    h.crc = *crc;
    h.stored = *stored;
    if (h.version > kVersion) {
        return make_error(ErrorCode::Unsupported,
                          "savegame of a newer Open1602 (format " + std::to_string(h.version) + ")");
    }
    if (h.size > kMaxSize || bytes.size() - kHeaderSize != h.stored) {
        return make_error(ErrorCode::InvalidFormat, "savegame: cut off or damaged");
    }
    return h;
}

}  // namespace

bool is_open1602_save(std::span<const std::uint8_t> bytes) noexcept {
    return bytes.size() >= kHeaderSize && std::equal(kMagic.begin(), kMagic.end(), bytes.begin());
}

core::Result<core::Bytes> write_game(const World& world, const assets::BuildingTable& buildings,
                                     const Simulation* simulation, const SaveOptions& options) {
    const core::Bytes sections =
        save::encode_sections(world, buildings, simulation, save::TileMode::Full, std::span<const std::uint8_t>{});
    std::uint16_t flags = 0;
    core::Bytes stored;
    if (options.compress && core::has_zlib()) {
        auto packed = core::zlib_deflate(sections);
        if (!packed) {
            return packed.error();
        }
        stored = std::move(packed).value();
        flags |= kCompressed;
    } else {
        stored = sections;
    }
    if (stored.size() > 0xFFFFFFFFULL || sections.size() > kMaxSize) {
        return make_error(ErrorCode::OutOfRange, "savegame: the game is too large");
    }
    core::ByteWriter out;
    out.bytes(kMagic);
    out.u16(kVersion);
    out.u16(flags);
    out.u32(0);
    out.u64(sections.size());
    out.u32(core::crc32(sections));
    out.u32(static_cast<std::uint32_t>(stored.size()));
    out.bytes(stored);
    return out.take();
}

core::Result<SaveInfo> save_info(std::span<const std::uint8_t> bytes) {
    auto h = read_header(bytes);
    if (!h) {
        return h.error();
    }
    return SaveInfo{h->version, (h->flags & kCompressed) != 0, h->size};
}

core::Result<World> read_game(std::span<const std::uint8_t> bytes, const assets::BuildingTable& buildings,
                              std::vector<std::string>* warnings, const assets::FigureTable* figures) {
    auto h = read_header(bytes);
    if (!h) {
        return h.error();
    }
    const auto stored = bytes.subspan(kHeaderSize);
    core::Bytes unpacked;
    std::span<const std::uint8_t> sections = stored;
    if ((h->flags & kCompressed) != 0) {
        auto data = core::zlib_inflate(stored, static_cast<std::size_t>(h->size));
        if (!data) {
            return data.error();
        }
        unpacked = std::move(data).value();
        sections = unpacked;
    } else if (stored.size() != h->size) {
        return make_error(ErrorCode::InvalidFormat, "savegame: cut off or damaged");
    }
    if (core::crc32(sections) != h->crc) {
        return make_error(ErrorCode::InvalidFormat, "savegame: damaged (checksum)");
    }
    auto world = save::world_from_sections(sections, buildings, warnings);
    if (!world) {
        return world.error();
    }
    // world_from_sections already reported the sections it skipped.
    if (auto status = save::apply_sections(*world, buildings, sections, nullptr); !status) {
        return status.error();
    }
    if (auto status = validate(*world); !status) {
        return status.error();
    }
    if (figures != nullptr && !Simulation::state_readable(*world, buildings, *figures)) {
        return make_error(ErrorCode::InvalidFormat, "savegame: the state of the running game is damaged (SIMU)");
    }
    return world;
}

}  // namespace o1602::sim
