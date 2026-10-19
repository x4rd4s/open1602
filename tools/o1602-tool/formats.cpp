// SPDX-License-Identifier: GPL-3.0-or-later
//
// o1602-tool: the original's file formats (chunk containers, .cod scripts, BSH/ZEI sprites).

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

#include "tool.hpp"

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/sprite_sheet.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/png_writer.hpp"

namespace o1602::tool {

namespace {

std::string format_value(const assets::cod::Value& v) {
    if (v.type == assets::cod::ValueType::String) {
        return "\"" + v.text + "\"";
    }
    if (v.real != static_cast<double>(v.integer)) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%g", v.real);
        return buf;
    }
    return std::to_string(v.integer);
}

void print_object(const assets::cod::Object& obj, int indent, const std::string& only_type) {
    const std::string pad(static_cast<std::size_t>(indent) * 2, ' ');
    for (const auto& [name, field] : obj.fields) {
        if (field.assigned_with_equals) {
            continue;  // constants (NAME = value) are not record properties
        }
        std::string line = pad + name + ":";
        for (std::size_t i = 0; i < field.values.size(); ++i) {
            line += (i == 0 ? " " : ", ") + format_value(field.values[i]);
        }
        std::printf("%s\n", line.c_str());
    }
    for (const auto& [type, records] : obj.children) {
        if (indent == 0 && !only_type.empty() && type != only_type) {
            continue;
        }
        for (const auto& [nummer, record] : records) {
            std::printf("%s[%s #%d]\n", pad.c_str(), type.c_str(), nummer);
            print_object(record, indent + 1, only_type);
        }
    }
}

}  // namespace

int cmd_chunks(const Args& args) {
    if (args.positional.size() != 1) {
        return usage();
    }
    auto file = assets::ChunkFile::load(fs::path(args.positional[0]));
    if (!file) {
        return fail(file.error());
    }
    std::printf("%-18s %10s %10s\n", "name", "offset", "size");
    for (const auto& chunk : file->chunks()) {
        std::printf("%-18s %10zu %10zu\n", chunk.name.c_str(), chunk.offset, chunk.size);
    }
    std::printf("%zu chunks\n", file->chunks().size());
    return 0;
}

int cmd_cod(const Args& args) {
    if (args.positional.empty() || args.positional.size() > 2) {
        return usage();
    }
    auto text = assets::load_cod_utf8(fs::path(args.positional[0]));
    if (!text) {
        return fail(text.error());
    }
    if (args.positional.size() == 2) {
        const auto* begin = reinterpret_cast<const std::uint8_t*>(text->data());
        if (auto status = core::write_file(fs::path(args.positional[1]), std::span(begin, text->size())); !status) {
            return fail(status.error());
        }
    } else {
        std::fwrite(text->data(), 1, text->size(), stdout);
    }
    return 0;
}

int cmd_cod_dump(const Args& args) {
    if (args.positional.size() != 1) {
        return usage();
    }
    const fs::path path(args.positional[0]);
    auto bytes = core::read_file(path);
    if (!bytes) {
        return fail(bytes.error());
    }
    std::string predefined = "auto";
    if (const auto it = args.options.find("--predefined"); it != args.options.end()) {
        predefined = std::string(it->second);
    }
    if (predefined == "auto") {
        std::string stem = path.stem().string();
        std::transform(stem.begin(), stem.end(), stem.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        predefined = stem == "haeuser" || stem == "figuren" ? stem : "none";
    }
    std::span<const assets::CodPredefinedSymbol> symbols;
    if (predefined == "haeuser") {
        symbols = assets::cod_predefined_haeuser();
    } else if (predefined == "figuren") {
        symbols = assets::cod_predefined_figuren();
    }

    const auto doc = assets::cod::parse(assets::decode_cod(*bytes), symbols);
    std::string only_type;
    if (const auto it = args.options.find("--type"); it != args.options.end()) {
        only_type = std::string(it->second);
    }
    print_object(doc.root, 0, only_type);

    std::fprintf(stderr, "-- %zu symbols, %zu diagnostics (predefined: %s)\n", doc.symbols.size(),
                 doc.diagnostics.size(), predefined.c_str());
    for (const auto& d : doc.diagnostics) {
        std::fprintf(stderr, "   line %u: %s\n", d.line, d.message.c_str());
    }
    return 0;
}

int cmd_bsh_info(const Args& args) {
    if (args.positional.size() != 1) {
        return usage();
    }
    auto sheet = assets::SpriteSheet::load(fs::path(args.positional[0]));
    if (!sheet) {
        return fail(sheet.error());
    }
    std::printf("%6s %6s %6s %5s %8s %10s\n", "index", "width", "height", "type", "length", "offset");
    for (std::size_t i = 0; i < sheet->size(); ++i) {
        const auto& info = sheet->info(i);
        std::printf("%6zu %6u %6u %5u %8u %10u\n", i, info.width, info.height, info.type, info.length, info.offset);
    }
    std::printf("%zu sprites\n", sheet->size());
    return 0;
}

int cmd_bsh_export(const Args& args) {
    if (args.positional.size() != 2) {
        return usage();
    }
    auto sheet = assets::SpriteSheet::load(fs::path(args.positional[0]));
    if (!sheet) {
        return fail(sheet.error());
    }
    auto palette = load_palette(args);
    if (!palette) {
        return fail(palette.error());
    }

    const fs::path out_dir(args.positional[1]);
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    if (ec) {
        return fail(core::make_error(core::ErrorCode::IoError, "cannot create " + out_dir.string()));
    }

    const std::size_t first = std::min(args.number("--first").value_or(0), sheet->size());
    const std::size_t count = std::min(args.number("--count").value_or(sheet->size()), sheet->size() - first);
    const std::string stem = fs::path(args.positional[0]).stem().string();

    std::vector<assets::IndexedImage> images;
    for (std::size_t i = first; i < first + count; ++i) {
        auto image = sheet->decode(i);
        if (!image) {
            return fail(image.error());
        }
        if (args.flag("--sheet")) {
            images.push_back(std::move(image).value());
            continue;
        }
        char name[64];
        std::snprintf(name, sizeof(name), "%s_%05zu.png", stem.c_str(), i);
        if (auto status = core::write_png_rgba(out_dir / name, image->width, image->height, image->to_rgba(*palette));
            !status) {
            return fail(status.error());
        }
    }

    if (args.flag("--sheet") && !images.empty()) {
        // Grid of equally sized cells, sprites bottom-aligned (as they are drawn in game).
        std::uint32_t cell_w = 1;
        std::uint32_t cell_h = 1;
        for (const auto& img : images) {
            cell_w = std::max(cell_w, img.width);
            cell_h = std::max(cell_h, img.height);
        }
        const auto columns =
            static_cast<std::uint32_t>(std::max<std::size_t>(1, std::min<std::size_t>(16, images.size())));
        const auto rows = static_cast<std::uint32_t>((images.size() + columns - 1) / columns);
        const std::uint32_t width = cell_w * columns;
        const std::uint32_t height = cell_h * rows;
        std::vector<std::uint8_t> rgba(std::size_t{width} * height * 4, 0);
        for (std::size_t k = 0; k < images.size(); ++k) {
            const auto& img = images[k];
            const auto pixels = img.to_rgba(*palette);
            const std::uint32_t ox = static_cast<std::uint32_t>(k % columns) * cell_w;
            const std::uint32_t oy = static_cast<std::uint32_t>(k / columns) * cell_h + (cell_h - img.height);
            for (std::uint32_t y = 0; y < img.height; ++y) {
                std::copy_n(pixels.begin() + static_cast<std::ptrdiff_t>(std::size_t{y} * img.width * 4),
                            std::size_t{img.width} * 4,
                            rgba.begin() + static_cast<std::ptrdiff_t>((std::size_t{oy + y} * width + ox) * 4));
            }
        }
        const fs::path out = out_dir / (stem + "_sheet.png");
        if (auto status = core::write_png_rgba(out, width, height, rgba); !status) {
            return fail(status.error());
        }
        std::printf("wrote %s (%ux%u, %zu sprites)\n", out.string().c_str(), width, height, images.size());
    } else {
        std::printf("wrote %zu sprites to %s\n", count, out_dir.string().c_str());
    }
    return 0;
}

}  // namespace o1602::tool
