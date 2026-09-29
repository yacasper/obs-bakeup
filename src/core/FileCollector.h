// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace obs_backuper {

struct CollectedFile {
	std::filesystem::path relativePath;
	std::filesystem::path absolutePath;
	std::uintmax_t sizeBytes = 0;
};

struct CollectionResult {
	std::vector<CollectedFile> files;
	std::uintmax_t totalSizeBytes = 0;
};

// A folder OUTSIDE the obs-studio directory that holds installed plugins:
// on Windows OBS looks for third-party plugins in %ProgramData%\obs-studio\
// plugins (or <OBS folder>\plugins in portable mode), not next to its
// settings. Its files go into an archive under "<archivePrefix>/..." and are
// put back into `dir` on restore. (macOS keeps plugins inside the obs-studio
// directory, so it needs none.)
struct PluginRoot {
	std::string archivePrefix; // first path component inside the archive
	std::filesystem::path dir; // where the folder is on this machine
};

// Archive prefixes for those folders. They are fixed strings because backups
// are restored later, possibly elsewhere, and must be recognised by name.
inline constexpr const char *kSystemPluginsPrefix = "system-plugins";
inline constexpr const char *kPortablePluginsPrefix = "portable-plugins";

// True if an archive path lies under one of the plugin-root prefixes above.
bool IsPluginRootEntry(const std::filesystem::path &archiveRelativePath);

// Appends every file under root.dir to `result`, named "<prefix>/<path inside
// dir>". A missing or unreadable folder adds nothing.
void CollectPluginRoot(const PluginRoot &root, CollectionResult &result);

// Sections of the obs-studio directory included in the backup by default.
//
// This is an allow-list, not a deny-list: any internal OBS directories/files
// not on this list (logs, crashes, plugin_manager, profiler_data, safe_mode,
// updates, .sentinel, .DS_Store, etc.) are automatically excluded from the
// backup without needing to maintain an explicit deny-list that could go
// stale as OBS is updated. Within the included sections themselves there is
// no further filtering, apart from macOS ".DS_Store" files, which are always
// skipped -- priority is given to maximum recoverability over
// archive size (e.g. obs-browser keeps widget logins in its cookies/local
// storage, so its cache is kept too).
//
// "plugins" is the per-user plugin folder, so installed plugins are backed up
// together with their settings (plugin_config). Plugins installed system-wide
// (outside the obs-studio directory) are not covered.
extern const std::vector<std::string> kDefaultIncludedTopLevelEntries;

// Recursively walks rootDir, including only the top-level entries (files or
// directories) whose name is present in includedTopLevelEntries.
CollectionResult CollectFiles(const std::filesystem::path &rootDir,
			       const std::vector<std::string> &includedTopLevelEntries =
				       kDefaultIncludedTopLevelEntries);

} // namespace obs_backuper
