// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include "RestoreManager.h"
#include "RestoreResult.h"

#include <filesystem>

namespace obs_backuper {

// Where the restore that is waiting to be applied, and the outcome of one that
// was, are kept. Both live in the safety-backup folder.
std::filesystem::path PendingRestoreMarkerPath(const std::filesystem::path &safetyBackupDir);
std::filesystem::path StoredRestoreResultPath(const std::filesystem::path &safetyBackupDir);

struct AppliedRestore {
	bool wasPending = false; // false: there was nothing to apply, the rest is unset
	PendingRestoreMarker marker;
	CommitOutcome commit;
};

// Applies the restore staged earlier, if there is one: clears leftovers of
// earlier restores, commits the staged files and removes the marker.
AppliedRestore ApplyPendingRestore(const std::filesystem::path &safetyBackupDir,
				   const std::filesystem::path &obsDataDir);

// The shutdown path: like ApplyPendingRestore, and then stores the outcome for
// the next start to show. Returns the same result.
AppliedRestore ApplyPendingRestoreAndStoreResult(const std::filesystem::path &safetyBackupDir,
						 const std::filesystem::path &obsDataDir);

// Reads and deletes the outcome stored by the shutdown path. False if none.
bool TakeStoredRestoreResult(const std::filesystem::path &safetyBackupDir, StoredRestoreResult &result);

} // namespace obs_backuper
