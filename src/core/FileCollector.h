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
	// Top-level entries of `dir` to leave out, matched case-insensitively by
	// the part of the name before the first dot ("obs-ffmpeg" covers
	// obs-ffmpeg.dll, obs-ffmpeg.pdb and a folder called obs-ffmpeg).
	std::vector<std::string> excludedStems;
};

// Archive prefixes for those folders. They are fixed strings because backups
// are restored later, possibly elsewhere, and must be recognised by name.
inline constexpr const char *kSystemPluginsPrefix = "system-plugins";
inline constexpr const char *kPortablePluginsPrefix = "portable-plugins";

// macOS: plugins installed for all users, in /Library/Application Support/
// obs-studio/plugins (the ones for the current user sit inside the obs-studio
// settings folder and need no root). Its own prefix, so a Windows backup never
// lands there and the other way round.
inline constexpr const char *kMacSystemPluginsPrefix = "mac-system-plugins";

// Plugins installed the classic way, into OBS's own program folder: module
// files in <OBS folder>\obs-plugins\64bit and their data in
// <OBS folder>\data\obs-plugins. Two roots, because the halves live apart.
inline constexpr const char *kProgramPluginsBinPrefix = "program-plugins-bin";
inline constexpr const char *kProgramPluginsDataPrefix = "program-plugins-data";

// Names (see PluginRoot::excludedStems) of the plugins and helper files OBS
// installs by itself in those two folders. They belong to one exact OBS
// version, so backing them up would let a restore overwrite a newer OBS's own
// files with old ones; every other file there is a third-party plugin.
const std::vector<std::string> &ObsShippedPluginStems();

// Disk caches the embedded browser (obs-browser) rebuilds by itself, plus the
// files it keeps open while OBS runs. They are large, worthless in a backup and
// the ones that cannot be written back on restore. Logins (cookies, local
// storage) are not among them. `pathInsideObsDir` is relative to the obs-studio
// folder.
bool IsDisposableBrowserData(const std::filesystem::path &pathInsideObsDir);

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
