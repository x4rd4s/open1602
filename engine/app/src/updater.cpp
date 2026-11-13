// SPDX-License-Identifier: GPL-3.0-or-later
#include "updater.hpp"

#include <cstdio>
#include <utility>

#include "annokit/core/data_file.hpp"
#include "annokit/core/file.hpp"
#include "annokit/core/hash.hpp"
#include "annokit/update/archive.hpp"
#include "annokit/update/version.hpp"

namespace o1602::app {

namespace fs = std::filesystem;
using annokit::core::ErrorCode;

namespace {

// The public half of the release key; its private half signs update.toml in the release workflow
// (the secret UPDATE_SIGNING_KEY, docs/updates.md).
constexpr std::string_view kReleaseKey = "2deb8db7d33ca5e80a11846c95c6d942fdf3b8a509114246d656272be069ecdb";

/// The platform release.toml names, or empty.
std::string read_marker(const fs::path& file) {
    auto bytes = annokit::core::read_file(file);
    if (!bytes) {
        return {};
    }
    auto tables =
        annokit::core::parse_data_file(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    if (!tables || tables->empty() || !(*tables)[0].name.empty()) {
        return {};
    }
    const annokit::core::DataValue* platform = (*tables)[0].find("platform");
    return platform != nullptr && platform->string() != nullptr ? *platform->string() : std::string();
}

std::string user_agent(const std::string& version) {
    return "Open1602/" + version;
}

}  // namespace

std::span<const annokit::update::PublicKey> release_keys() {
    static const std::vector<annokit::update::PublicKey> keys = [] {
        std::vector<annokit::update::PublicKey> out;
        if (const auto key = annokit::update::from_hex<32>(kReleaseKey)) {
            out.push_back(*key);
        }
        return out;
    }();
    return keys;
}

UpdateTarget locate_update_target(const fs::path& executable) {
    UpdateTarget target;
    target.restart_program = executable;
    const fs::path folder = executable.parent_path();
    std::error_code ec;
    struct Candidate {
        fs::path marker;
        fs::path root;
        bool bundle;
    };
    const Candidate candidates[] = {
        {folder / kReleaseMarker, folder, false},                                                         // Windows
        {folder.parent_path() / "share" / "open1602" / kReleaseMarker, folder.parent_path(), false},      // Linux
        {folder.parent_path() / "Resources" / kReleaseMarker, folder.parent_path().parent_path(), true},  // macOS
    };
    for (const Candidate& c : candidates) {
        if (!fs::is_regular_file(c.marker, ec)) {
            continue;
        }
        target.platform = read_marker(c.marker);
        if (target.platform.empty()) {
            continue;
        }
        target.installation.root = c.root;
        if (c.bundle) {
            target.installation.layout = annokit::update::InstallLayout::Tree;
            target.installation.work = c.root.parent_path() / ".Open1602-update";
            target.package_folder = "Open1602.app";  // as the package holds it, whatever the copy is named
            // A new process of the app through Launch Services, as a double click starts it.
            target.restart_program = "/usr/bin/open";
            target.restart_arguments = {"-n", annokit::core::path_to_utf8(c.root)};
        } else {
            target.installation.work = c.root / ".update";
        }
        if (annokit::core::path_to_utf8(c.root).find("/AppTranslocation/") != std::string::npos) {
            target.kind = UpdateTarget::Kind::Translocated;
        } else if (!annokit::update::check_writable(target.installation)) {
            target.kind = UpdateTarget::Kind::ReadOnly;
        } else {
            target.kind = UpdateTarget::Kind::Package;
        }
        return target;
    }
    return target;
}

Updater::Updater(Config config) : m_config(std::move(config)) {}

Updater::~Updater() {
    m_cancel = true;
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

Updater::Status Updater::status() const {
    std::lock_guard lock(m_mutex);
    return m_status;
}

bool Updater::busy() const {
    const Phase phase = status().phase;
    return phase == Phase::Checking || phase == Phase::Downloading || phase == Phase::Installing;
}

void Updater::set(const std::function<void(Status&)>& change) {
    std::lock_guard lock(m_mutex);
    change(m_status);
}

void Updater::finish(Phase phase, Problem problem) {
    set([&](Status& s) {
        s.phase = phase;
        s.problem = problem;
    });
}

void Updater::check(bool background) {
    if (busy() || status().phase == Phase::Installed) {
        return;
    }
    if (m_worker.joinable()) {
        m_worker.join();
    }
    m_cancel = false;
    set([&](Status& s) {
        s = Status{};
        s.phase = Phase::Checking;
        s.background = background;
    });
    m_worker = std::thread([this] { run(false); });
}

void Updater::install() {
    if (!can_install() || status().phase != Phase::Available) {
        return;
    }
    if (m_worker.joinable()) {
        m_worker.join();
    }
    m_cancel = false;
    set([](Status& s) {
        s.phase = Phase::Downloading;
        s.problem = Problem::None;
        s.background = false;
        s.done = 0;
        s.total = 0;
    });
    m_worker = std::thread([this] { run(true); });
}

void Updater::cancel() {
    m_cancel = true;
}

std::optional<std::string> Updater::previous_version() const {
    if (m_config.target.kind != UpdateTarget::Kind::Package) {
        return std::nullopt;
    }
    return annokit::update::previous_version(m_config.target.installation);
}

annokit::core::Status Updater::restore_previous() {
    if (busy()) {
        return annokit::core::make_error(ErrorCode::IoError, "update: busy");
    }
    if (m_config.target.kind != UpdateTarget::Kind::Package) {
        return annokit::core::make_error(ErrorCode::Unsupported, "update: this copy does not update itself");
    }
    auto status = annokit::update::restore_previous(m_config.target.installation);
    if (status) {
        finish(Phase::Installed);
    }
    return status;
}

void Updater::clean_up() {
    if (m_config.target.kind == UpdateTarget::Kind::Package) {
        annokit::update::clean_up(m_config.target.installation);
    }
}

void Updater::run(bool install) {
    auto client = m_config.client ? m_config.client()
                                  : annokit::core::Result<std::unique_ptr<annokit::update::HttpClient>>(
                                        annokit::core::make_error(ErrorCode::PlatformError, "none"));
    if (!client) {
        std::fprintf(stderr, "update: %s\n", client.error().message.c_str());
        finish(Phase::Failed, Problem::NoHttps);
        return;
    }
    annokit::update::HttpClient& http = **client;
    const auto offline_or = [](const annokit::core::Error& e, Problem otherwise) {
        return e.code == ErrorCode::IoError ? Problem::Offline : otherwise;
    };
    if (!install) {
        annokit::update::HttpRequest request;
        request.url = m_config.manifest_url;
        request.user_agent = user_agent(m_config.current);
        request.max_size = 64 * 1024;
        request.progress = [this](std::uint64_t, std::uint64_t) { return !m_cancel.load(); };
        auto manifest_bytes = http.get(request);
        if (!manifest_bytes) {
            std::fprintf(stderr, "update: %s\n", manifest_bytes.error().message.c_str());
            finish(Phase::Failed, offline_or(manifest_bytes.error(), Problem::NoRelease));
            return;
        }
        request.url = m_config.signature_url;
        request.max_size = 1024;
        auto signature = http.get(request);
        if (!signature) {
            std::fprintf(stderr, "update: %s\n", signature.error().message.c_str());
            finish(Phase::Failed, offline_or(signature.error(), Problem::NotGenuine));
            return;
        }
        auto manifest = annokit::update::read_signed_manifest(
            *manifest_bytes, std::string_view(reinterpret_cast<const char*>(signature->data()), signature->size()),
            m_config.keys);
        if (!manifest) {
            std::fprintf(stderr, "update: %s\n", manifest.error().message.c_str());
            finish(Phase::Failed,
                   manifest.error().code == ErrorCode::Unsupported ? Problem::NewerFormat : Problem::NotGenuine);
            return;
        }
        const auto current = annokit::update::parse_version(m_config.current);
        const bool newer = !current || manifest->version > *current;
        std::fprintf(stderr, "update: the latest release is %s, this is %s\n",
                     annokit::update::to_string(manifest->version).c_str(), m_config.current.c_str());
        set([&](Status& s) {
            s.latest = annokit::update::to_string(manifest->version);
            s.notes = manifest->notes;
        });
        {
            std::lock_guard lock(m_mutex);
            m_manifest = std::move(manifest).value();
        }
        finish(newer ? Phase::Available : Phase::UpToDate);
        return;
    }

    std::optional<annokit::update::ManifestPackage> package;
    {
        std::lock_guard lock(m_mutex);
        if (m_manifest) {
            if (const auto* p = m_manifest->package(m_config.target.platform)) {
                package = *p;
            }
        }
    }
    if (!package) {
        std::fprintf(stderr, "update: the release has no package for %s\n", m_config.target.platform.c_str());
        finish(Phase::Failed, Problem::NoPackage);
        return;
    }
    annokit::update::HttpRequest request;
    request.url = package->url;
    request.user_agent = user_agent(m_config.current);
    request.max_size = package->size;
    request.progress = [this, size = package->size](std::uint64_t done, std::uint64_t) {
        set([&](Status& s) {
            s.done = done;
            s.total = size;
        });
        return !m_cancel.load();
    };
    auto bytes = http.get(request);
    if (!bytes) {
        std::fprintf(stderr, "update: %s\n", bytes.error().message.c_str());
        if (bytes.error().code == ErrorCode::Cancelled) {
            finish(Phase::Available);
        } else {
            finish(Phase::Failed, offline_or(bytes.error(), Problem::Damaged));
        }
        return;
    }
    finish(Phase::Installing);
    if (bytes->size() != package->size || annokit::core::sha256(*bytes) != package->sha256) {
        std::fprintf(stderr, "update: %s does not match its size or checksum\n", package->url.c_str());
        finish(Phase::Failed, Problem::Damaged);
        return;
    }
    const std::string name = package->url.substr(package->url.find_last_of('/') + 1);
    auto files = annokit::update::read_package(*bytes, name);
    if (files) {
        std::string folder = annokit::update::top_folder(*files);
        if (!m_config.target.package_folder.empty()) {
            folder += (folder.empty() ? "" : "/") + m_config.target.package_folder;
        }
        files = folder.empty() ? std::move(files) : annokit::update::files_below(std::move(files).value(), folder);
    }
    if (!files) {
        std::fprintf(stderr, "update: %s: %s\n", name.c_str(), files.error().message.c_str());
        finish(Phase::Failed, Problem::Damaged);
        return;
    }
    if (auto installed = annokit::update::install_files(m_config.target.installation, *files, m_config.current);
        !installed) {
        std::fprintf(stderr, "update: %s\n", installed.error().message.c_str());
        finish(Phase::Failed, Problem::InstallFailed);
        return;
    }
    std::fprintf(stderr, "update: %s installed (%zu files), %s kept\n", name.c_str(), files->size(),
                 m_config.current.c_str());
    finish(Phase::Installed);
}

}  // namespace o1602::app
