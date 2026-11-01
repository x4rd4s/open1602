// SPDX-License-Identifier: GPL-3.0-or-later
// Zip archives for tests (engine/core/include/o1602/core/zip_archive.hpp).
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "o1602/core/compress.hpp"

namespace o1602::test {

struct ZipFile {
    std::string path;
    std::string text;
    bool deflate = false;  ///< packed with deflate (needs zlib), else stored
};

/// A zip archive of these files, as a zip tool writes it: local headers, the central directory and
/// its end.
inline std::vector<std::uint8_t> make_zip(const std::vector<ZipFile>& files) {
    std::vector<std::uint8_t> out;
    std::vector<std::uint8_t> central;
    const auto u16 = [](std::vector<std::uint8_t>& to, std::uint32_t v) {
        to.push_back(static_cast<std::uint8_t>(v));
        to.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    const auto u32 = [&](std::vector<std::uint8_t>& to, std::uint32_t v) {
        u16(to, v & 0xFFFF);
        u16(to, v >> 16);
    };
    for (const ZipFile& f : files) {
        const std::vector<std::uint8_t> bytes(f.text.begin(), f.text.end());
        std::vector<std::uint8_t> data = bytes;
        if (f.deflate) {
            const auto packed = core::zlib_deflate(bytes);
            // A zlib stream without its 2 bytes of header and 4 of checksum is raw deflate.
            data.assign(packed->begin() + 2, packed->end() - 4);
        }
        const std::uint32_t crc = core::crc32(bytes);
        const auto offset = static_cast<std::uint32_t>(out.size());
        const std::uint32_t method = f.deflate ? 8 : 0;
        const auto header = [&](std::vector<std::uint8_t>& to) {
            u16(to, 20);      // version needed
            u16(to, 0x0800);  // flags: UTF-8 names
            u16(to, method);
            u32(to, 0);  // time, date
            u32(to, crc);
            u32(to, static_cast<std::uint32_t>(data.size()));
            u32(to, static_cast<std::uint32_t>(bytes.size()));
            u16(to, static_cast<std::uint32_t>(f.path.size()));
        };
        u32(out, 0x04034B50);
        header(out);
        u16(out, 0);  // extra
        out.insert(out.end(), f.path.begin(), f.path.end());
        out.insert(out.end(), data.begin(), data.end());
        u32(central, 0x02014B50);
        u16(central, 20);  // made by
        header(central);
        u16(central, 0);  // extra
        u16(central, 0);  // comment
        u16(central, 0);  // disk
        u16(central, 0);  // internal attributes
        u32(central, 0);  // external attributes
        u32(central, offset);
        central.insert(central.end(), f.path.begin(), f.path.end());
    }
    const auto directory = static_cast<std::uint32_t>(out.size());
    out.insert(out.end(), central.begin(), central.end());
    u32(out, 0x06054B50);
    u16(out, 0);
    u16(out, 0);
    u16(out, static_cast<std::uint32_t>(files.size()));
    u16(out, static_cast<std::uint32_t>(files.size()));
    u32(out, static_cast<std::uint32_t>(central.size()));
    u32(out, directory);
    u16(out, 0);  // comment
    return out;
}

}  // namespace o1602::test
