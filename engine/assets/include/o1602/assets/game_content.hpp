// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the game, its tools and a server share to start the same game (LC-17): the fingerprint of
// the data the simulation computes with, and the packages of the mods folder with their manifests,
// in their load order (ADR 0007 section 2, docs/modding/packages.md).
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/game_directory.hpp"
#include "o1602/core/result.hpp"

namespace o1602::assets {

/// The data the simulation computes with, as one number the computers of a multiplayer game
/// compare before they play: the bytes of haeuser.cod and figuren.cod. The names of new cities and
/// ships travel with the game (sim::Simulation::offer_names), so installations in other languages
/// play together; the corrections of assets::fixes change nothing the simulation reads and may
/// differ (data_fixes.hpp). The packages a game uses are compared one by one (ContentPackage).
[[nodiscard]] std::uint64_t content_fingerprint(const GameDirectory& game);

/// The file in a package's folder that describes it.
inline constexpr std::string_view kManifestFile = "mod.toml";
/// Upper limits of a package's id and version, in bytes (savegames and the network carry them).
inline constexpr std::size_t kMaxPackageIdLength = 64;
inline constexpr std::size_t kMaxPackageVersionLength = 32;

/// A package a game uses, as the game names it: in its savegame, its lobby and the network's Hello.
struct ContentPackage {
    std::string id;          ///< UTF-8, the folder's name
    std::string version;     ///< as the manifest gives it, empty without one
    std::uint64_t hash = 0;  ///< of every file of the package (package_hash)
    friend bool operator==(const ContentPackage&, const ContentPackage&) = default;
};

/// "Needs that package, at least at that version" (`depends.<id> = "1.2"` in the manifest).
struct PackageDependency {
    std::string id;
    std::string min_version;  ///< empty: any version
};

/// A folder of <data folder>/mods: its id is the folder's name. The rest comes from its manifest
/// (read_manifest); a folder without one is a package with no version, no name of its own and no
/// dependencies.
struct ModPackage {
    std::string id;  ///< UTF-8
    std::filesystem::path folder;
    std::string name;     ///< for lists and messages; the id if the manifest names none
    std::string version;  ///< empty without a manifest
    std::string description;
    std::vector<PackageDependency> depends;
    std::vector<std::string> after;  ///< loads after these when they are there, without needing them
    std::uint64_t hash = 0;          ///< package_hash(folder), set by load_mod_packages

    [[nodiscard]] ContentPackage content() const { return ContentPackage{id, version, hash}; }
};

/// The folders in `mods`, ordered by the bytes of their UTF-8 names, the same on every system
/// (std::filesystem::path compares wchar_t on Windows); none if the folder does not exist. Their
/// manifests are not read yet.
[[nodiscard]] std::vector<ModPackage> mod_packages(const std::filesystem::path& mods);

/// Whether `version` is one a manifest may give: one to four numbers of at most nine digits,
/// separated by dots ("1", "1.2", "0.10.3").
[[nodiscard]] bool valid_version(std::string_view version) noexcept;
/// Compares two valid versions number by number, a missing number counting as 0 ("1.2" == "1.2.0",
/// "1.10" > "1.9"): negative, 0 or positive.
[[nodiscard]] int compare_versions(std::string_view a, std::string_view b) noexcept;

/// Reads the manifest text of `package` (the contents of its mod.toml) into it. What is wrong is
/// passed over with a warning that names the file and the line; a file that cannot be read at all
/// is an error and changes nothing.
core::Status read_manifest(ModPackage& package, std::string_view utf8, std::vector<std::string>& warnings);

/// One number for every file of a package's folder and its subfolders: their paths relative to the
/// folder (UTF-8, '/' between folders, ordered by their bytes), sizes and bytes. Equal on every
/// system for equal files; a file that cannot be read counts differently from an empty one.
[[nodiscard]] std::uint64_t package_hash(const std::filesystem::path& folder);

/// The packages of `mods` that can be loaded, in their load order: each with its manifest read and
/// its hash. A package comes after the ones it depends on and the ones it wants to come after;
/// otherwise in the order of the bytes of their ids. A package whose manifest cannot be read, whose
/// dependency is missing or too old, or that is part of a circle of dependencies is left out, and
/// a warning says why (as are the packages that depend on it).
[[nodiscard]] std::vector<ModPackage> load_mod_packages(const std::filesystem::path& mods,
                                                        std::vector<std::string>& warnings);
/// The same for packages whose manifests were read already (and whose hashes are set): the order,
/// without the packages that cannot be loaded.
[[nodiscard]] std::vector<ModPackage> order_packages(std::vector<ModPackage> packages,
                                                     std::vector<std::string>& warnings);

/// What a game names (ContentPackage) of the packages it was started with.
[[nodiscard]] std::vector<ContentPackage> content_of(std::span<const ModPackage> packages);

/// The packages of `wanted` (a game's) among `installed`, in the game's order. A package that is
/// not installed is left out, one installed with other files is taken (this computer's), each with
/// a warning. The installed packages the game does not name are not taken.
[[nodiscard]] std::vector<ModPackage> packages_for(std::span<const ModPackage> installed,
                                                   std::span<const ContentPackage> wanted,
                                                   std::vector<std::string>& warnings);

/// The packages of `needed` that `present` has not with the same id and hash: what a computer lacks
/// to join a game.
[[nodiscard]] std::vector<ContentPackage> missing_packages(std::span<const ContentPackage> needed,
                                                           std::span<const ContentPackage> present);

/// "lighthouse 1.2" (or "lighthouse" without a version), for messages.
[[nodiscard]] std::string package_label(const ContentPackage& package);

}  // namespace o1602::assets
