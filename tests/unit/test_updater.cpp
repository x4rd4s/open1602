// SPDX-License-Identifier: GPL-3.0-or-later
// The updates from the GitHub releases (engine/app/src/updater.hpp): where a copy was unpacked, the
// check of the signed update information and the install of a package, all against a fake HTTPS
// client and a key of the tests.

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../test_framework.hpp"
#include "app_settings.hpp"
#include "update_menu.hpp"
#include "updater.hpp"
#include "zip_support.hpp"

#include "annokit/core/file.hpp"
#include "annokit/core/hash.hpp"
#include "annokit/update/signature.hpp"

using namespace o1602;
namespace fs = std::filesystem;
namespace update = annokit::update;
using app::Updater;
using app::UpdateTarget;

namespace {

/// Answers each address with what the test put there, or an error.
class FakeHttp final : public update::HttpClient {
public:
    std::map<std::string, std::vector<std::uint8_t>> files;
    annokit::core::ErrorCode failure = annokit::core::ErrorCode::NotFound;

    annokit::core::Result<std::vector<std::uint8_t>> get(const update::HttpRequest& request) override {
        const auto found = files.find(request.url);
        if (found == files.end()) {
            return annokit::core::make_error(failure, request.url + ": not here");
        }
        if (found->second.size() > request.max_size) {
            return annokit::core::make_error(annokit::core::ErrorCode::LimitExceeded, "too large");
        }
        if (request.progress && !request.progress(found->second.size(), found->second.size())) {
            return annokit::core::make_error(annokit::core::ErrorCode::Cancelled, "cancelled");
        }
        return found->second;
    }
};

std::vector<std::uint8_t> bytes_of(const std::string& text) {
    return {text.begin(), text.end()};
}

const update::KeyPair& test_key() {
    static const update::KeyPair pair = update::key_pair_from_seed(std::array<std::uint8_t, 32>{42, 1, 6, 2});
    return pair;
}

constexpr const char* kManifestUrl = "https://example.org/latest/update.toml";
constexpr const char* kSignatureUrl = "https://example.org/latest/update.toml.sig";
constexpr const char* kPackageUrl = "https://example.org/v0.6.0/open1602-0.6.0-windows-x64.zip";

/// A fake server with release 0.6.0 and its signed update information.
std::shared_ptr<FakeHttp> server(const std::vector<std::uint8_t>& package, const std::string& version = "0.6.0") {
    auto http = std::make_shared<FakeHttp>();
    const std::string manifest = "format = 1\nversion = \"" + version +
                                 "\"\nnotes = \"https://example.org/notes\"\n\n[windows-x64]\nurl = \"" + kPackageUrl +
                                 "\"\nsize = " + std::to_string(package.size()) + "\nsha256 = \"" +
                                 update::to_hex(annokit::core::sha256(package)) + "\"\n";
    http->files[kManifestUrl] = bytes_of(manifest);
    http->files[kSignatureUrl] = bytes_of(update::to_hex(update::sign(test_key(), bytes_of(manifest))) + "\n");
    http->files[kPackageUrl] = package;
    return http;
}

Updater::Config config(const std::shared_ptr<FakeHttp>& http, UpdateTarget target = {}) {
    Updater::Config c;
    c.current = "0.5.0";
    c.target = std::move(target);
    c.manifest_url = kManifestUrl;
    c.signature_url = kSignatureUrl;
    c.keys = {test_key().key};
    c.client = [http]() -> annokit::core::Result<std::unique_ptr<update::HttpClient>> {
        auto copy = std::make_unique<FakeHttp>(*http);
        return std::unique_ptr<update::HttpClient>(std::move(copy));
    };
    return c;
}

/// The status once the worker is done.
Updater::Status settle(const Updater& updater) {
    for (int i = 0; i < 1000; ++i) {
        const Updater::Status s = updater.status();
        if (s.phase != Updater::Phase::Checking && s.phase != Updater::Phase::Downloading &&
            s.phase != Updater::Phase::Installing) {
            return s;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return updater.status();
}

/// A folder unpacked from a Windows release package: the game, release.toml and a text.
fs::path windows_copy(const std::string& name) {
    const fs::path root = test::temp_path(name);
    fs::remove_all(root);
    fs::create_directories(root);
    REQUIRE(annokit::core::write_file(root / "open1602.exe", bytes_of("old game")).ok());
    REQUIRE(annokit::core::write_file(root / "release.toml", bytes_of("platform = \"windows-x64\"\n")).ok());
    REQUIRE(annokit::core::write_file(root / "README.txt", bytes_of("old readme")).ok());
    return root;
}

std::string text_of(const fs::path& file) {
    const auto bytes = annokit::core::read_file(file);
    return bytes ? std::string(bytes->begin(), bytes->end()) : std::string("(missing)");
}

}  // namespace

TEST_CASE("updater: a copy updates itself only from a release package it may write") {
    const fs::path base = test::temp_path("o1602-test-update-target");
    fs::remove_all(base);

    // Built by hand: no release.toml anywhere.
    fs::create_directories(base / "build");
    CHECK(app::locate_update_target(base / "build" / "open1602").kind == UpdateTarget::Kind::NotPackaged);

    // Windows: release.toml beside the program.
    const fs::path windows = windows_copy("o1602-test-update-target-windows");
    const UpdateTarget w = app::locate_update_target(windows / "open1602.exe");
    CHECK(w.kind == UpdateTarget::Kind::Package);
    CHECK(w.platform == "windows-x64");
    CHECK(w.installation.root == windows);
    CHECK(w.installation.layout == update::InstallLayout::Files);
    CHECK(w.package_folder.empty());
    CHECK(w.restart_program == windows / "open1602.exe");

    // Linux: bin/open1602 and share/open1602/release.toml.
    fs::create_directories(base / "linux" / "bin");
    fs::create_directories(base / "linux" / "share" / "open1602");
    REQUIRE(annokit::core::write_file(base / "linux" / "share" / "open1602" / "release.toml",
                                      bytes_of("platform = \"linux-x64\"\n"))
                .ok());
    const UpdateTarget l = app::locate_update_target(base / "linux" / "bin" / "open1602");
    CHECK(l.kind == UpdateTarget::Kind::Package);
    CHECK(l.platform == "linux-x64");
    CHECK(l.installation.root == base / "linux");

    // macOS: the bundle is replaced as a whole and started again through Launch Services.
    const fs::path app = base / "mac" / "Open1602.app";
    fs::create_directories(app / "Contents" / "MacOS");
    fs::create_directories(app / "Contents" / "Resources");
    REQUIRE(annokit::core::write_file(app / "Contents" / "Resources" / "release.toml",
                                      bytes_of("platform = \"macos-universal\"\n"))
                .ok());
    const UpdateTarget m = app::locate_update_target(app / "Contents" / "MacOS" / "open1602");
    CHECK(m.kind == UpdateTarget::Kind::Package);
    CHECK(m.installation.root == app);
    CHECK(m.installation.layout == update::InstallLayout::Tree);
    CHECK(m.package_folder == "Open1602.app");
    CHECK(m.restart_program == fs::path("/usr/bin/open"));
    REQUIRE(m.restart_arguments.size() == 2);
    CHECK(m.restart_arguments[0] == "-n");

    // A marker without a platform counts as none.
    REQUIRE(annokit::core::write_file(base / "build" / "release.toml", bytes_of("# nothing\n")).ok());
    CHECK(app::locate_update_target(base / "build" / "open1602").kind == UpdateTarget::Kind::NotPackaged);

    fs::remove_all(base);
    fs::remove_all(windows);
}

TEST_CASE("updater: the check believes only update information signed with the release key") {
    const auto package = test::make_zip({{"open1602-0.6.0-windows-x64/open1602.exe", "new game"}});

    {  // Newer: offered.
        Updater updater(config(server(package)));
        updater.check(true);
        const Updater::Status s = settle(updater);
        CHECK(s.phase == Updater::Phase::Available);
        CHECK(s.latest == "0.6.0");
        CHECK(s.notes == "https://example.org/notes");
        CHECK(s.background);
        CHECK(!updater.can_install());  // built by hand: the menu links to the download page
    }
    {  // The same version: up to date.
        Updater updater(config(server(package, "0.5.0")));
        updater.check(false);
        CHECK(settle(updater).phase == Updater::Phase::UpToDate);
    }
    {  // Changed after signing.
        auto http = server(package);
        http->files[kManifestUrl].back() = ' ';
        Updater updater(config(http));
        updater.check(false);
        const Updater::Status s = settle(updater);
        CHECK(s.phase == Updater::Phase::Failed);
        CHECK(s.problem == Updater::Problem::NotGenuine);
    }
    {  // Signed with another key.
        Updater::Config c = config(server(package));
        c.keys = {update::key_pair_from_seed(std::array<std::uint8_t, 32>{9}).key};
        Updater updater(std::move(c));
        updater.check(false);
        CHECK(settle(updater).problem == Updater::Problem::NotGenuine);
    }
    {  // No update information in the latest release.
        auto http = server(package);
        http->files.erase(kManifestUrl);
        Updater updater(config(http));
        updater.check(false);
        CHECK(settle(updater).problem == Updater::Problem::NoRelease);
    }
    {  // No connection.
        auto http = std::make_shared<FakeHttp>();
        http->failure = annokit::core::ErrorCode::IoError;
        Updater updater(config(http));
        updater.check(true);
        const Updater::Status s = settle(updater);
        CHECK(s.problem == Updater::Problem::Offline);
        CHECK(s.background);
    }
    {  // No HTTPS client on the system.
        Updater::Config c = config(server(package));
        c.client = []() -> annokit::core::Result<std::unique_ptr<update::HttpClient>> {
            return annokit::core::make_error(annokit::core::ErrorCode::PlatformError, "no libcurl");
        };
        Updater updater(std::move(c));
        updater.check(false);
        CHECK(settle(updater).problem == Updater::Problem::NoHttps);
    }
}

TEST_CASE("updater: a release package is installed over the copy, which keeps the previous version") {
    const fs::path root = windows_copy("o1602-test-update-install");
    const UpdateTarget target = app::locate_update_target(root / "open1602.exe");
    REQUIRE(target.kind == UpdateTarget::Kind::Package);
    const auto package = test::make_zip({{"open1602-0.6.0-windows-x64/", ""},
                                         {"open1602-0.6.0-windows-x64/open1602.exe", "new game"},
                                         {"open1602-0.6.0-windows-x64/release.toml", "platform = \"windows-x64\"\n"},
                                         {"open1602-0.6.0-windows-x64/data/new.txt", "new file", true}});

    Updater updater(config(server(package), target));
    CHECK(updater.can_install());
    updater.install();  // nothing found yet: does nothing
    CHECK(updater.status().phase == Updater::Phase::Idle);
    updater.check(false);
    REQUIRE(settle(updater).phase == Updater::Phase::Available);
    updater.install();
    const Updater::Status s = settle(updater);
    CHECK(s.phase == Updater::Phase::Installed);
    CHECK(s.done == package.size());
    CHECK(text_of(root / "open1602.exe") == "new game");
    CHECK(text_of(root / "data" / "new.txt") == "new file");
    CHECK(text_of(root / "README.txt") == "old readme");  // not in the package: left alone
    CHECK(updater.previous_version() == std::optional<std::string>("0.5.0"));

    // Back to 0.5.0.
    CHECK(updater.restore_previous().ok());
    CHECK(text_of(root / "open1602.exe") == "old game");
    CHECK(!fs::exists(root / "data" / "new.txt"));
    updater.clean_up();
    fs::remove_all(root);
}

TEST_CASE("updater: a package that does not match its checksum is not installed") {
    const fs::path root = windows_copy("o1602-test-update-damaged");
    const auto package = test::make_zip({{"open1602-0.6.0-windows-x64/open1602.exe", "new game"}});
    auto http = server(package);
    http->files[kPackageUrl][0] ^= 1;
    Updater updater(config(http, app::locate_update_target(root / "open1602.exe")));
    updater.check(false);
    REQUIRE(settle(updater).phase == Updater::Phase::Available);
    updater.install();
    const Updater::Status s = settle(updater);
    CHECK(s.phase == Updater::Phase::Failed);
    CHECK(s.problem == Updater::Problem::Damaged);
    CHECK(text_of(root / "open1602.exe") == "old game");
    CHECK(!updater.previous_version());
    fs::remove_all(root);
}

TEST_CASE("updater: the setting of the check at the start and the version skipped") {
    CHECK(app::AppSettings{}.update_check == app::UpdateCheck::Ask);
    app::AppSettings s;
    s.update_check = app::UpdateCheck::Off;
    s.skipped_update = "0.6.0";
    const app::AppSettings back = app::parse_settings(app::write_settings(s), {});
    CHECK(back.update_check == app::UpdateCheck::Off);
    CHECK(back.skipped_update == "0.6.0");
    CHECK(app::parse_settings("update_check = on\n", {}).update_check == app::UpdateCheck::On);
    CHECK(app::parse_settings("update_check = ask\n", s).update_check == app::UpdateCheck::Ask);
}

TEST_CASE("updater: the menu's texts of every state") {
    app::UpdateModel model;
    model.current = "0.5.0";
    model.status.phase = Updater::Phase::Downloading;
    model.status.done = 3 << 20;
    model.status.total = 12 << 20;
    CHECK(!app::UpdateMenu::state_text(model).empty());
    for (const auto problem : {Updater::Problem::Offline, Updater::Problem::NoRelease, Updater::Problem::NotGenuine,
                               Updater::Problem::NewerFormat, Updater::Problem::NoPackage, Updater::Problem::Damaged,
                               Updater::Problem::InstallFailed, Updater::Problem::NoHttps}) {
        const std::string text = app::UpdateMenu::problem_text(problem);
        CHECK(!text.empty());
        CHECK(!text.starts_with("update."));  // a key no catalogue has shows as itself
    }
    for (const auto kind :
         {UpdateTarget::Kind::NotPackaged, UpdateTarget::Kind::Translocated, UpdateTarget::Kind::ReadOnly}) {
        CHECK(!app::UpdateMenu::kind_text(kind).starts_with("update."));
    }
}
