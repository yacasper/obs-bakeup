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

// Sections of the obs-studio directory included in the backup by default.
//
// This is an allow-list, not a deny-list: any internal OBS directories/files
// not on this list (logs, crashes, plugins, plugin_manager, profiler_data,
// safe_mode, updates, .sentinel, .DS_Store, etc.) are automatically excluded from the
// backup without needing to maintain an explicit deny-list that could go
// stale as OBS is updated. Within the included sections themselves
// (primarily plugin_config/**) there is no further filtering — priority is
// given to maximum recoverability over archive size (e.g. obs-browser keeps
// widget logins in its cookies/local storage, so its cache is kept too).
extern const std::vector<std::string> kDefaultIncludedTopLevelEntries;

// Recursively walks rootDir, including only the top-level entries (files or
// directories) whose name is present in includedTopLevelEntries.
CollectionResult CollectFiles(const std::filesystem::path &rootDir,
			       const std::vector<std::string> &includedTopLevelEntries =
				       kDefaultIncludedTopLevelEntries);

} // namespace obs_backuper
