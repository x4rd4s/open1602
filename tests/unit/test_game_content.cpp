// SPDX-License-Identifier: GPL-3.0-or-later
// What the game, its tools and a server share to start the same game
// (engine/assets/include/o1602/assets/game_content.hpp, LC-17, DS-5).

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "../test_framework.hpp"
#include "test_support.hpp"

#include "o1602/assets/game_content.hpp"

using namespace o1602;

namespace {

struct Folder {
    std::filesystem::path root;
    explicit Folder(const char* name) : root(test::temp_path(name)) {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }
    ~Folder() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    void write(const char* file, const std::string& text) const { std::ofstream(root / file) << text; }
};

}  // namespace

TEST_CASE("game content: the fingerprint changes with the tables the simulation reads, not with the rest") {
    Folder folder("o1602_content_test");
    for (const char* d : {"toolgfx", "gfx"}) {
        std::filesystem::create_directories(folder.root / d);
    }
    folder.write("toolgfx/stadtfld.col", "x");
    folder.write("gfx/stadtfld.bsh", "x");
    folder.write("haeuser.cod", "a");
    folder.write("figuren.cod", "b");
    folder.write("text.cod", "one");
    auto game = assets::GameDirectory::open(folder.root);
    REQUIRE(game.ok());
    const std::uint64_t first = assets::content_fingerprint(*game);
    CHECK(assets::content_fingerprint(*game) == first);
    folder.write("text.cod", "another language");
    CHECK(assets::content_fingerprint(*game) == first);
    folder.write("figuren.cod", "c");
    CHECK(assets::content_fingerprint(*game) != first);
    // The files' boundaries count: "ab" + "" is not "a" + "b".
    folder.write("haeuser.cod", "ab");
    folder.write("figuren.cod", "");
    const std::uint64_t joined = assets::content_fingerprint(*game);
    folder.write("haeuser.cod", "a");
    folder.write("figuren.cod", "b");
    CHECK(assets::content_fingerprint(*game) == first);
    CHECK(joined != first);
}

TEST_CASE("game content: the mods' packages in the order of their UTF-8 names on every system") {
    Folder mods("o1602_mod_packages_test");
    for (const char* name : {"zeta", "Alpha", "beta", "\xc3\xa4rger"}) {  // "ärger": UTF-8 after ASCII
        std::filesystem::create_directories(
            mods.root / std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(name))));
    }
    mods.write("readme.txt", "not a package");
    const std::vector<assets::ModPackage> packages = assets::mod_packages(mods.root);
    REQUIRE(packages.size() == 4);
    CHECK(packages[0].id == "Alpha");
    CHECK(packages[1].id == "beta");
    CHECK(packages[2].id == "zeta");
    CHECK(packages[3].id == "\xc3\xa4rger");
    CHECK(packages[3].folder.filename() == std::filesystem::path(u8"ärger"));
    CHECK(assets::mod_packages(mods.root / "missing").empty());
}

namespace {

assets::ModPackage package(std::string id, std::string version = {}) {
    assets::ModPackage p;
    p.id = std::move(id);
    p.name = p.id;
    p.version = std::move(version);
    return p;
}

/// The packages' ids with a space between them.
std::string ids(const std::vector<assets::ModPackage>& packages) {
    std::string out;
    for (const auto& p : packages) {
        out += (out.empty() ? "" : " ") + p.id;
    }
    return out;
}

}  // namespace

TEST_CASE("game content: versions of one to four numbers, compared number by number") {
    for (const char* good : {"1", "1.2", "0.10.3", "1.2.3.4", "123456789"}) {
        CHECK(assets::valid_version(good));
    }
    for (const char* bad : {"", "1.", ".1", "1..2", "1.2.3.4.5", "1.2a", "v1", "1234567890", "-1"}) {
        CHECK(!assets::valid_version(bad));
    }
    CHECK(assets::compare_versions("1.2", "1.2.0") == 0);
    CHECK(assets::compare_versions("1.10", "1.9") > 0);
    CHECK(assets::compare_versions("0.9.9", "1") < 0);
    CHECK(assets::compare_versions("2.0.0.1", "2") > 0);
}

TEST_CASE("game content: a manifest gives name, version, dependencies and load order") {
    assets::ModPackage p = package("lighthouse");
    std::vector<std::string> warnings;
    const auto status = assets::read_manifest(p, R"(# A lighthouse for every coast.
name = "Leuchtturm"
version = "1.2.0"
description = "Shines."
depends.harbour = "1.0"
depends.coast = ""
after.weather = true
after.never = false
colour = "red"
)",
                                              warnings);
    REQUIRE(status.ok());
    CHECK(p.name == "Leuchtturm");
    CHECK(p.version == "1.2.0");
    CHECK(p.description == "Shines.");
    REQUIRE(p.depends.size() == 2);
    CHECK(p.depends[0].id == "harbour");
    CHECK(p.depends[0].min_version == "1.0");
    CHECK(p.depends[1].id == "coast");
    CHECK(p.depends[1].min_version.empty());
    REQUIRE(p.after.size() == 1);
    CHECK(p.after[0] == "weather");
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("lighthouse/mod.toml line 9: unknown field colour") == 0);
}

TEST_CASE("game content: what is wrong in a manifest is passed over, a broken one changes nothing") {
    assets::ModPackage p = package("lighthouse");
    std::vector<std::string> warnings;
    REQUIRE(assets::read_manifest(p, "version = \"one\"\ndepends.lighthouse = \"\"\ndepends.x = 2\n[table]\n", warnings)
                .ok());
    CHECK(p.version.empty());
    CHECK(p.depends.empty());
    CHECK(warnings.size() == 4);
    p = package("lighthouse");
    p.version = "3";
    warnings.clear();
    CHECK(!assets::read_manifest(p, "version = [\"1\"]\n", warnings).ok());
    CHECK(p.version == "3");
}

TEST_CASE("game content: packages load after their dependencies and the ones they want to follow") {
    std::vector<assets::ModPackage> list;
    list.push_back(package("alpha"));
    list.back().depends.push_back({"zeta", ""});
    list.push_back(package("beta"));
    list.back().after = {"alpha", "not-installed"};
    list.push_back(package("zeta", "2.1"));
    list.push_back(package("gamma"));
    std::vector<std::string> warnings;
    CHECK(ids(assets::order_packages(list, warnings)) == "gamma zeta alpha beta");
    CHECK(warnings.empty());
}

TEST_CASE("game content: a missing or too old dependency and a circle leave packages out") {
    std::vector<assets::ModPackage> list;
    list.push_back(package("needs-new", "1"));
    list.back().depends.push_back({"base", "2.0"});
    list.push_back(package("base", "1.9"));
    list.push_back(package("needs-needs-new"));
    list.back().depends.push_back({"needs-new", ""});
    list.push_back(package("needs-gone"));
    list.back().depends.push_back({"gone", ""});
    list.push_back(package("round-a"));
    list.back().after = {"round-b"};
    list.push_back(package("round-b"));
    list.back().depends.push_back({"round-a", ""});
    list.push_back(package("versionless"));
    list.push_back(package("needs-version"));
    list.back().depends.push_back({"versionless", "1"});
    std::vector<std::string> warnings;
    CHECK(ids(assets::order_packages(list, warnings)) == "base versionless");
    CHECK(warnings.size() == 6);
}

TEST_CASE("game content: a package's hash covers its files' paths and bytes, not when they were made") {
    Folder a("o1602_package_hash_a");
    Folder b("o1602_package_hash_b");
    std::filesystem::create_directories(a.root / "gfx");
    std::filesystem::create_directories(b.root / "gfx");
    a.write("mod.toml", "version = \"1\"");
    a.write("gfx/tower.txt", "tower");
    b.write("gfx/tower.txt", "tower");  // the other way round
    b.write("mod.toml", "version = \"1\"");
    CHECK(assets::package_hash(a.root) == assets::package_hash(b.root));
    b.write("gfx/tower.txt", "tower!");
    CHECK(assets::package_hash(a.root) != assets::package_hash(b.root));
    b.write("gfx/tower.txt", "tower");
    std::filesystem::rename(b.root / "gfx" / "tower.txt", b.root / "gfx" / "other.txt");
    CHECK(assets::package_hash(a.root) != assets::package_hash(b.root));
}

TEST_CASE("game content: the installed packages with manifests, hashes and load order") {
    Folder mods("o1602_load_packages_test");
    for (const char* name : {"addon", "base", "broken", "plain"}) {
        std::filesystem::create_directories(mods.root / name);
    }
    mods.write("addon/mod.toml", "version = \"1.0\"\ndepends.base = \"2\"\n");
    mods.write("base/mod.toml", "name = \"Base\"\nversion = \"2.3\"\n");
    mods.write("broken/mod.toml", "version = 1.0\n");  // a float: not read
    std::vector<std::string> warnings;
    const std::vector<assets::ModPackage> packages = assets::load_mod_packages(mods.root, warnings);
    CHECK(ids(packages) == "base addon plain");
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("broken/mod.toml") == 0);
    CHECK(packages[0].name == "Base");
    CHECK(packages[2].name == "plain");
    CHECK(packages[2].version.empty());
    CHECK(packages[0].hash != 0);
    CHECK(packages[0].hash == assets::package_hash(mods.root / "base"));

    // Turned off: left out, and so is what depends on it.
    const std::vector<std::string> off{"base"};
    warnings.clear();
    CHECK(ids(assets::load_mod_packages(mods.root, warnings, off)) == "plain");
    CHECK(warnings.size() == 3);
}

TEST_CASE("game content: a game takes the packages it names, in its order, and no others") {
    std::vector<assets::ModPackage> installed{package("a", "1"), package("b", "2"), package("c")};
    installed[0].hash = 1;
    installed[1].hash = 2;
    installed[2].hash = 3;
    const std::vector<assets::ContentPackage> wanted{{"c", "", 3}, {"a", "1", 9}, {"gone", "4", 4}};
    std::vector<std::string> warnings;
    CHECK(ids(assets::packages_for(installed, wanted, warnings)) == "c a");
    REQUIRE(warnings.size() == 2);
    CHECK(warnings[0].find("other files") != std::string::npos);
    CHECK(warnings[1].find("gone 4") != std::string::npos);
    const auto missing = assets::missing_packages(wanted, assets::content_of(installed));
    REQUIRE(missing.size() == 2);
    CHECK(missing[0].id == "a");
    CHECK(missing[1].id == "gone");
    CHECK(assets::missing_packages(assets::content_of(installed), assets::content_of(installed)).empty());
    CHECK(assets::package_label(missing[1]) == "gone 4");
    CHECK(assets::package_label({"c", "", 3}) == "c");
}

TEST_CASE("game content: only paths inside a package are written") {
    CHECK(assets::safe_package_path("mod.toml"));
    CHECK(assets::safe_package_path("gfx/towers/tower.png"));
    CHECK(!assets::safe_package_path(""));
    CHECK(!assets::safe_package_path("/etc/passwd"));
    CHECK(!assets::safe_package_path("gfx/../../x"));
    CHECK(!assets::safe_package_path("./x"));
    CHECK(!assets::safe_package_path("gfx//x"));
    CHECK(!assets::safe_package_path("gfx/"));
    CHECK(!assets::safe_package_path("gfx\\x"));
    CHECK(!assets::safe_package_path("C:x"));
    CHECK(!assets::safe_package_path("a\nb"));
    CHECK(!assets::safe_package_path(std::string(assets::kMaxPackagePathLength + 1, 'a')));
}

TEST_CASE("game content: a received package is kept in the cache only with the package's hash") {
    Folder source("o1602_store_source");
    Folder cache("o1602_store_cache");
    std::filesystem::create_directories(source.root / "gfx");
    source.write("mod.toml", "name = \"Beacon\"\nversion = \"1.2\"\n");
    source.write("gfx/tower.txt", "tower");
    const auto files = assets::read_package_files(source.root);
    REQUIRE(files.ok());
    REQUIRE(files->size() == 2);
    CHECK((*files)[0].path == "gfx/tower.txt");
    CHECK(assets::package_size(source.root) == files->at(0).bytes.size() + files->at(1).bytes.size());
    const assets::ContentPackage beacon{"beacon", "1.2", assets::package_hash(source.root)};

    const auto stored = assets::store_package(cache.root, beacon, *files);
    REQUIRE(stored.ok());
    CHECK(stored->folder == assets::cached_package_folder(cache.root, beacon));
    CHECK(stored->name == "Beacon");
    CHECK(stored->content() == beacon);

    assets::ContentPackage other = beacon;
    other.hash ^= 1;
    CHECK(!assets::store_package(cache.root, other, *files).ok());
    CHECK(!std::filesystem::exists(assets::cached_package_folder(cache.root, other)));
    std::vector<assets::PackageFile> outside = *files;
    outside[0].path = "../escape.txt";
    CHECK(!assets::store_package(cache.root, beacon, outside).ok());
    CHECK(!std::filesystem::exists(cache.root / "escape.txt"));
    CHECK(!assets::store_package(cache.root, {"..", "", beacon.hash}, *files).ok());

    std::vector<std::string> warnings;
    auto cached = assets::cached_packages(cache.root, warnings);
    REQUIRE(cached.size() == 1);
    CHECK(warnings.empty());
    CHECK(cached[0].content() == beacon);

    Folder mods("o1602_adopt_mods");
    const auto adopted = assets::adopt_package(mods.root, cached[0]);
    REQUIRE(adopted.ok());
    CHECK(*adopted == mods.root / "beacon");
    CHECK(assets::package_hash(*adopted) == beacon.hash);
    CHECK(!assets::adopt_package(mods.root, cached[0]).ok());  // there already

    std::ofstream(stored->folder / "gfx" / "tower.txt") << "changed";
    CHECK(assets::cached_packages(cache.root, warnings).empty());
    CHECK(warnings.size() == 1);
}
