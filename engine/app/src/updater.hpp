// SPDX-License-Identifier: GPL-3.0-or-later
//
// Open1602's updates from its GitHub releases (the menu "Updates", docs/updates.md). Each release
// carries update.toml, signed with the release key (update.toml.sig): the latest version and the
// package of every platform with its size and SHA-256. The packages the release workflow builds
// carry release.toml, which names their platform; only such a package updates itself. A build of
// its own, a distribution's package or an app macOS runs from a read-only copy only learns that a
// new version is out.
//
// The work runs beside the game on a thread of its own; the menu reads status() every frame.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "annokit/core/result.hpp"
#include "annokit/update/http.hpp"
#include "annokit/update/install.hpp"
#include "annokit/update/manifest.hpp"

namespace o1602::app {

/// Where the update information of the latest release lies (releases/latest skips pre-releases).
inline constexpr std::string_view kUpdateManifestUrl =
    "https://github.com/x4rd4s/open1602/releases/latest/download/update.toml";
inline constexpr std::string_view kUpdateSignatureUrl =
    "https://github.com/x4rd4s/open1602/releases/latest/download/update.toml.sig";
/// The file in a release package that names its platform.
inline constexpr std::string_view kReleaseMarker = "release.toml";

/// The release key's public half: update.toml must carry its signature.
[[nodiscard]] std::span<const annokit::update::PublicKey> release_keys();

/// How this copy of Open1602 can update itself.
struct UpdateTarget {
    enum class Kind : std::uint8_t {
        Package,       ///< a release package in a folder it may write: updates itself
        NotPackaged,   ///< built by hand or packaged by others: only told about new versions
        Translocated,  ///< macOS runs the app from a read-only copy: it must be moved first
        ReadOnly,      ///< a release package in a folder it may not write
    };
    Kind kind = Kind::NotPackaged;
    std::string platform;  ///< as release.toml names it ("windows-x64"); empty if NotPackaged
    annokit::update::Installation installation;
    /// The folder of the package's files that is installed, below the package's top folder: empty
    /// for all of them, "Open1602.app" on macOS.
    std::string package_folder;
    /// What starts the new version: the program and its arguments.
    std::filesystem::path restart_program;
    std::vector<std::string> restart_arguments;
};

/// Where the program at `executable` was unpacked and whether it can update itself: release.toml
/// beside it (Windows), in ../share/open1602 (Linux) or ../Resources (macOS, the app bundle).
[[nodiscard]] UpdateTarget locate_update_target(const std::filesystem::path& executable);

class Updater {
public:
    enum class Phase : std::uint8_t {
        Idle,
        Checking,
        UpToDate,
        Available,    ///< a newer version is out (`latest`)
        Downloading,  ///< `done` of `total` bytes
        Installing,
        Installed,  ///< the new version is in place: restart
        Failed,     ///< `problem` says why; the installed version is unchanged
    };
    enum class Problem : std::uint8_t {
        None,
        Offline,        ///< no connection to GitHub
        NoRelease,      ///< the latest release has no update information
        NotGenuine,     ///< the signature or the information is wrong
        NewerFormat,    ///< the information is of a later format: update by hand
        NoPackage,      ///< the release has no package for this platform
        Damaged,        ///< the package does not match its size or checksum, or cannot be unpacked
        InstallFailed,  ///< the files could not be put in place
        NoHttps,        ///< the system has no HTTPS client (no libcurl)
    };
    struct Status {
        Phase phase = Phase::Idle;
        Problem problem = Problem::None;
        std::string latest;  ///< the newest version, once known ("0.6.0")
        std::string notes;   ///< https address of its release notes, or empty
        std::uint64_t done = 0;
        std::uint64_t total = 0;
        /// The check was started by the program (at the start), not by the player.
        bool background = false;
    };
    using ClientFactory = std::function<annokit::core::Result<std::unique_ptr<annokit::update::HttpClient>>()>;

    struct Config {
        std::string current;  ///< this version ("0.5.0")
        UpdateTarget target;
        std::string manifest_url = std::string(kUpdateManifestUrl);
        std::string signature_url = std::string(kUpdateSignatureUrl);
        std::vector<annokit::update::PublicKey> keys;
        ClientFactory client = annokit::update::system_http_client;
    };

    explicit Updater(Config config);
    ~Updater();
    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;

    /// Looks for a newer release (unless busy). `background`: the check at the start.
    void check(bool background);
    /// Downloads and installs the newer release found by check().
    void install();
    /// Stops a download; nothing is installed.
    void cancel();
    [[nodiscard]] Status status() const;
    [[nodiscard]] const UpdateTarget& target() const noexcept { return m_config.target; }
    [[nodiscard]] const std::string& current() const noexcept { return m_config.current; }
    /// Whether this copy installs updates itself (else the menu links to the download page).
    [[nodiscard]] bool can_install() const noexcept { return m_config.target.kind == UpdateTarget::Kind::Package; }

    /// The version kept from the last update, which restore_previous() brings back.
    [[nodiscard]] std::optional<std::string> previous_version() const;
    [[nodiscard]] annokit::core::Status restore_previous();
    /// Deletes what an earlier update left behind (at the start).
    void clean_up();

private:
    void run(bool install);
    void finish(Phase phase, Problem problem = Problem::None);
    void set(const std::function<void(Status&)>& change);
    [[nodiscard]] bool busy() const;

    Config m_config;
    mutable std::mutex m_mutex;
    Status m_status;
    std::optional<annokit::update::Manifest> m_manifest;  ///< from the last check (worker only)
    std::thread m_worker;
    std::atomic<bool> m_cancel{false};
};

}  // namespace o1602::app
