// SPDX-License-Identifier: GPL-3.0-or-later
// Pictures of a package's own: the PNG reader (annokit: modules/core/include/annokit/core/png_reader.hpp),
// graphics.toml (engine/assets/include/o1602/assets/package_graphics.hpp), buildings that draw an
// image and the sprites the atlas gets from it (engine/render/include/o1602/render/
// package_sprites.hpp; docs/modding/graphics.md).

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "../test_framework.hpp"

#include "annokit/core/compress.hpp"
#include "annokit/core/file.hpp"
#include "annokit/core/png_reader.hpp"
#include "annokit/core/png_writer.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/own_buildings.hpp"
#include "o1602/assets/package_graphics.hpp"
#include "o1602/render/package_sprites.hpp"

using namespace o1602;

namespace {

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(v >> shift));
    }
}

void put_chunk(std::vector<std::uint8_t>& out, std::string_view type, const std::vector<std::uint8_t>& data) {
    put_u32(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> typed(type.begin(), type.end());
    typed.insert(typed.end(), data.begin(), data.end());
    out.insert(out.end(), typed.begin(), typed.end());
    put_u32(out, annokit::core::crc32(typed));
}

/// A PNG of the rows given (each with its filter byte), as a test writes what the encoder of
/// png_writer.hpp never does: other colour types, bit depths and filters.
std::vector<std::uint8_t> make_png(std::uint32_t width, std::uint32_t height, std::uint8_t depth, std::uint8_t colour,
                                   const std::vector<std::uint8_t>& rows,
                                   const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& extra = {},
                                   std::uint8_t interlace = 0) {
    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> ihdr;
    put_u32(ihdr, width);
    put_u32(ihdr, height);
    ihdr.insert(ihdr.end(), {depth, colour, 0, 0, interlace});
    put_chunk(png, "IHDR", ihdr);
    for (const auto& [type, data] : extra) {
        put_chunk(png, type, data);
    }
    auto packed = annokit::core::zlib_deflate(rows);
    REQUIRE(packed.ok());
    put_chunk(png, "IDAT", *packed);
    put_chunk(png, "IEND", {});
    return png;
}

std::vector<std::uint8_t> pixel(const annokit::core::PngImage& image, std::uint32_t x, std::uint32_t y) {
    const auto at = image.rgba.begin() + static_cast<std::ptrdiff_t>((std::size_t{y} * image.width + x) * 4);
    return {at, at + 4};
}

std::vector<std::uint8_t> px(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    return {r, g, b, a};
}

void need_zlib() {
    if (!annokit::core::has_zlib()) {
        SKIP("this build has no zlib");
    }
}

/// An RGBA image of `columns` x `rows` sprites of `w` x `h`: sprite k is opaque in colour k from
/// row k down (so k rows at its top are transparent), with a pixel of alpha 100 in its corner.
std::vector<std::uint8_t> grid(std::uint32_t columns, std::uint32_t rows, std::uint32_t w, std::uint32_t h) {
    const std::uint32_t width = columns * w;
    std::vector<std::uint8_t> rgba(std::size_t{width} * rows * h * 4, 0);
    for (std::uint32_t k = 0; k < columns * rows; ++k) {
        for (std::uint32_t y = k; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const std::size_t p = ((std::size_t{k / columns} * h + y) * width + (k % columns) * w + x) * 4;
                rgba[p] = static_cast<std::uint8_t>(10 * (k + 1));
                rgba[p + 3] = static_cast<std::uint8_t>(x == 0 && y == h - 1 ? 100 : 255);
            }
        }
    }
    return rgba;
}

/// A folder of a package with the files given.
std::filesystem::path package_folder(const std::string& name,
                                     const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& files) {
    const std::filesystem::path folder = test::temp_path(name);
    std::filesystem::remove_all(folder);
    for (const auto& [file, bytes] : files) {
        const std::filesystem::path path = folder / file;
        std::filesystem::create_directories(path.parent_path());
        REQUIRE(annokit::core::write_file(path, bytes).ok());
    }
    return folder;
}

}  // namespace

TEST_CASE("png: an image of the writer comes back as it was") {
    need_zlib();
    std::vector<std::uint8_t> rgba(5 * 3 * 4);
    for (std::size_t i = 0; i < rgba.size(); ++i) {
        rgba[i] = static_cast<std::uint8_t>(i * 7);
    }
    const auto png = annokit::core::encode_png_rgba(5, 3, rgba);
    const auto image = annokit::core::decode_png(png);
    REQUIRE(image.ok());
    CHECK(image->width == 5);
    CHECK(image->height == 3);
    CHECK(image->rgba == rgba);
    const auto size = annokit::core::png_size(std::span(png).first(33));
    REQUIRE(size.ok());
    CHECK(size->width == 5);
    CHECK(size->rgba.empty());
}

TEST_CASE("png: every filter, palettes, grey and transparency") {
    need_zlib();
    // RGB, 8 bits: the rows filtered with Sub, Up, Average and Paeth.
    const std::vector<std::uint8_t> rgb = {1, 10, 20, 30, 5, 5, 5,   // Sub: (10,20,30) (15,25,35)
                                           2, 1,  1,  1,  1, 1, 1,   // Up: (11,21,31) (16,26,36)
                                           3, 10, 10, 10, 3, 3, 3,   // Average: (15,20,25) (18,26,33)
                                           4, 1,  1,  1,  0, 0, 0};  // Paeth: (16,21,26) (18,26,33)
    const auto image = annokit::core::decode_png(make_png(2, 4, 8, 2, rgb));
    REQUIRE(image.ok());
    CHECK(pixel(*image, 1, 0) == px(15, 25, 35, 255));
    CHECK(pixel(*image, 1, 1) == px(16, 26, 36, 255));
    CHECK(pixel(*image, 0, 2) == px(15, 20, 25, 255));
    CHECK(pixel(*image, 1, 2) == px(18, 26, 33, 255));  // 3 + (15 + 16) / 2, ...
    CHECK(pixel(*image, 0, 3) == px(16, 21, 26, 255));
    CHECK(pixel(*image, 1, 3) == px(18, 26, 33, 255));  // Paeth picks the pixel above

    // A palette of 2 bits with three colours, the first transparent; an index beyond them is refused.
    const std::vector<std::uint8_t> three = {0, 0, 0, 255, 0, 0, 0, 255, 0};
    CHECK(!annokit::core::decode_png(make_png(4, 1, 2, 3, {0, 0b00011011}, {{"PLTE", three}})).ok());
    const auto pal3 =
        annokit::core::decode_png(make_png(3, 1, 2, 3, {0, 0b00011000}, {{"PLTE", three}, {"tRNS", {0}}}));
    REQUIRE(pal3.ok());
    CHECK(pixel(*pal3, 0, 0) == px(0, 0, 0, 0));
    CHECK(pixel(*pal3, 1, 0) == px(255, 0, 0, 255));
    CHECK(pixel(*pal3, 2, 0) == px(0, 255, 0, 255));

    // Grey of 16 bits, one value transparent; grey of 4 bits scaled to 8.
    const auto grey =
        annokit::core::decode_png(make_png(2, 1, 16, 0, {0, 0x12, 0x34, 0xAB, 0xCD}, {{"tRNS", {0x12, 0x34}}}));
    REQUIRE(grey.ok());
    CHECK(pixel(*grey, 0, 0)[3] == 0);
    CHECK(pixel(*grey, 1, 0) == px(0xAB, 0xAB, 0xAB, 255));
    const auto grey4 = annokit::core::decode_png(make_png(2, 1, 4, 0, {0, 0xF5}));
    REQUIRE(grey4.ok());
    CHECK(pixel(*grey4, 0, 0)[0] == 255);
    CHECK(pixel(*grey4, 1, 0)[0] == 85);
}

TEST_CASE("png: a damaged or unsupported file is refused") {
    need_zlib();
    auto png = make_png(1, 1, 8, 6, {0, 1, 2, 3, 4});
    CHECK(annokit::core::decode_png(png).ok());
    auto broken = png;
    broken[40] ^= 0xFF;  // in IDAT: its CRC no longer fits
    CHECK(!annokit::core::decode_png(broken).ok());
    CHECK(!annokit::core::decode_png(std::span(png).first(png.size() - 12)).ok());                // no IEND
    CHECK(!annokit::core::decode_png(make_png(1, 1, 8, 6, {5, 1, 2, 3, 4})).ok());                // filter 5
    CHECK(!annokit::core::decode_png(make_png(1, 1, 8, 6, {0, 1, 2})).ok());                      // too few pixels
    CHECK(!annokit::core::decode_png(make_png(1, 1, 8, 6, {0, 1, 2, 3, 4}, {}, 1)).ok());         // interlaced
    CHECK(!annokit::core::decode_png(make_png(1, 1, 3, 6, {0, 1, 2, 3, 4})).ok());                // 3 bits
    CHECK(!annokit::core::png_size(make_png(annokit::core::kMaxPngSide + 1, 1, 8, 6, {})).ok());  // too wide
}

TEST_CASE("graphics: graphics.toml numbers the images of the packages") {
    need_zlib();
    const auto large = annokit::core::encode_png_rgba(4 * 64, 2 * 32, grid(4, 2, 64, 32));  // 8 sprites
    const auto medium = annokit::core::encode_png_rgba(4 * 32, 2 * 16, grid(4, 2, 32, 16));
    const auto small = annokit::core::encode_png_rgba(2 * 32, 16, grid(2, 1, 32, 16));  // 2 sprites
    const auto folder = package_folder("o1602-test-graphics",
                                       {{"gfx/tower.png", large}, {"gfx/tower_m.png", medium}, {"small.png", small}});
    std::vector<assets::PackageImage> images;
    std::vector<std::string> warnings;
    assets::apply_graphics_file(images, "lights", folder,
                                "[tower]\n"
                                "image = \"gfx/tower.png\"\n"
                                "image_medium = \"gfx/tower_m.png\"\n"
                                "width = 64\n"
                                "height = 32\n"
                                "[beacon]\n"
                                "image = \"gfx/tower.png\"\n"
                                "width = 64\n"
                                "height = 32\n"
                                "count = 3\n",
                                warnings);
    CHECK(warnings.empty());
    REQUIRE(images.size() == 2);
    CHECK(images[0].id == "lights:tower");
    CHECK(images[0].first == assets::kFirstPackageSprite);
    CHECK(images[0].count == 8);
    CHECK(images[0].files[1] == folder / "gfx" / "tower_m.png");
    CHECK(images[0].files[2].empty());
    CHECK(images[1].first == assets::kFirstPackageSprite + 8);
    CHECK(images[1].count == 3);
    CHECK(assets::find_package_image(images, "lights:beacon") == &images[1]);
    CHECK(assets::package_sprite_width(images[0], 2) == 16);

    // The next package's images come after these.
    warnings.clear();
    assets::apply_graphics_file(images, "more", folder, "[flag]\nimage = \"small.png\"\nwidth = 32\nheight = 16\n",
                                warnings);
    CHECK(warnings.empty());
    REQUIRE(images.size() == 3);
    CHECK(images[2].first == assets::kFirstPackageSprite + 11);
    CHECK(images[2].count == 2);
}

TEST_CASE("graphics: a table that is wrong is passed over with a warning") {
    need_zlib();
    const auto large = annokit::core::encode_png_rgba(4 * 64, 32, grid(4, 1, 64, 32));
    const auto small = annokit::core::encode_png_rgba(32, 16, grid(1, 1, 32, 16));
    const auto folder = package_folder("o1602-test-graphics-bad", {{"tower.png", large}, {"small.png", small}});
    std::vector<assets::PackageImage> images;
    std::vector<std::string> warnings;
    assets::apply_graphics_file(images, "lights", folder,
                                "[Tower]\n"                                             // line 1: upper case
                                "[a]\nimage = \"../tower.png\"\n"                       // line 3: outside the package
                                "[b]\nimage = \"tower.png\"\n"                          // line 4: no width
                                "[c]\nimage = \"none.png\"\nwidth = 64\nheight = 32\n"  // line 6: no file
                                "[d]\nimage = \"tower.png\"\nimage_medium = \"small.png\"\nwidth = 64\nheight = 32\n"
                                "[e]\nimage = \"tower.png\"\nwidth = 64\nheight = 32\ncount = 5\n"  // line 15
                                "[f]\nimage = \"tower.png\"\nwidth = 2000\nheight = 32\nframes = 2\n",
                                warnings);
    CHECK(images.empty());
    REQUIRE(warnings.size() == 8);
    CHECK(warnings[0] ==
          "lights/graphics.toml line 1: [Tower]: an image's name holds only lower case letters, digits and '_'");
    CHECK(warnings[1] ==
          "lights/graphics.toml line 3: 'image' is the path of a PNG file in the package, folders split "
          "by '/'");
    CHECK(warnings[2] == "lights/graphics.toml line 4: lights:b needs an 'image', a 'width' and a 'height'");
    CHECK(warnings[3].rfind("lights/graphics.toml line 6: lights:c: none.png: ", 0) == 0);
    CHECK(warnings[4] ==
          "lights/graphics.toml line 10: lights:d: small.png holds 1 sprites of 32 x 16, the large image 4");
    CHECK(warnings[5] == "lights/graphics.toml line 15: lights:e: 'count' is 5, the image holds 4 sprites of 64 x 32");
    CHECK(warnings[6] == "lights/graphics.toml line 22: 'width' is a number from 1 to 1024");
    CHECK(warnings[7] == "lights/graphics.toml line 24: 'frames' is no field of an image and is passed over");
}

TEST_CASE("graphics: a building of a package draws an image of a package") {
    need_zlib();
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}, {"HAUS_PRODTYP", 2}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20501\n  Gfx: 10\n  Size: 1, 1\n  Rotate: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 14\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    const auto folder = package_folder("o1602-test-graphics-building",
                                       {{"tower.png", annokit::core::encode_png_rgba(4 * 64, 32, grid(4, 1, 64, 32))}});
    std::vector<assets::PackageImage> images;
    std::vector<std::string> warnings;
    assets::apply_graphics_file(images, "lights", folder,
                                "[tower]\nimage = \"tower.png\"\nwidth = 64\nheight = 32\n"
                                "[half]\nimage = \"tower.png\"\nwidth = 64\nheight = 32\ncount = 3\n",
                                warnings);
    REQUIRE(images.size() == 2);
    std::vector<assets::OwnBuilding> own;
    assets::apply_own_buildings_file(*table, own, "lights",
                                     "[tower]\nlike = \"anno1602:20501\"\ngfx = \"lights:tower\"\n"
                                     "[short]\nlike = \"anno1602:20501\"\ngfx = \"lights:half\"\n"     // line 4
                                     "[none]\nlike = \"anno1602:20501\"\ngfx = \"lights:nothing\"\n",  // line 9
                                     warnings, {}, {}, {}, images);
    REQUIRE(own.size() == 1);
    const assets::BuildingType* tower = table->by_id(own[0].number);
    REQUIRE(tower != nullptr);
    CHECK(tower->gfx == assets::kFirstPackageSprite);
    CHECK(table->by_sprite(assets::kFirstPackageSprite) == tower);
    REQUIRE(warnings.size() == 2);
    CHECK(warnings[0] ==
          "lights/own_buildings.toml line 4: lights:short: its image lights:half holds 3 sprites, the "
          "building draws 4");
    CHECK(warnings[1] ==
          "lights/own_buildings.toml line 9: 'gfx' names no image of a package (<package>:<name>, graphics.toml)");
}

TEST_CASE("graphics: the sprites of an image at the three zoom levels") {
    need_zlib();
    const auto folder = package_folder("o1602-test-graphics-sprites",
                                       {{"tower.png", annokit::core::encode_png_rgba(2 * 4, 8, grid(2, 1, 4, 8))}});
    assets::PackageImage image;
    image.id = "lights:tower";
    image.first = assets::kFirstPackageSprite;
    image.count = 2;
    image.width = 4;
    image.height = 8;
    image.files[0] = folder / "tower.png";

    const render::PackageImageSource large(image, 0);
    CHECK(large.error().empty());
    REQUIRE(large.count() == 2);
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    large.size(1, w, h);
    CHECK(w == 4);
    CHECK(h == 7);  // its top row is transparent and left out
    std::vector<std::uint8_t> rgba;
    REQUIRE(large.pixels(1, rgba));
    REQUIRE(rgba.size() == 4 * 7 * 4);
    CHECK(rgba[0] == 20);
    CHECK(rgba[3] == 255);
    CHECK(rgba[(6 * 4) * 4 + 3] == 0);  // alpha 100 in the corner: transparent

    // The medium level halves the large one; the small level halves it again.
    const render::PackageImageSource medium(image, 1);
    medium.size(0, w, h);
    CHECK(w == 2);
    CHECK(h == 4);
    const render::PackageImageSource small(image, 2);
    small.size(0, w, h);
    CHECK(w == 1);
    CHECK(h == 2);

    // A file that is gone gives empty sprites and says why.
    assets::PackageImage gone = image;
    gone.files[0] = folder / "gone.png";
    const render::PackageImageSource missing(gone, 0);
    CHECK(!missing.error().empty());
    CHECK(missing.count() == 2);
    missing.size(0, w, h);
    CHECK(w == 0);

    // The terrain and the images at their numbers, nothing between them.
    render::PlacedSources placed;
    placed.place(0, missing);
    placed.place(10, large);
    CHECK(placed.count() == 12);
    placed.size(11, w, h);
    CHECK(h == 7);
    placed.size(5, w, h);
    CHECK(w == 0);
    CHECK(placed.pixels(5, rgba));
    CHECK(rgba.empty());
}
