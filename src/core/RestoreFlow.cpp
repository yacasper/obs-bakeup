// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "RestoreFlow.h"

namespace obs_backuper {

std::filesystem::path PendingRestoreMarkerPath(const std::filesystem::path &safetyBackupDir)
{
	return safetyBackupDir / "pending-restore.marker";
}

std::filesystem::path StoredRestoreResultPath(const std::filesystem::path &safetyBackupDir)
{
	return safetyBackupDir / "restore-result.ini";
}

AppliedRestore ApplyPendingRestore(const std::filesystem::path &safetyBackupDir,
				   const std::filesystem::path &obsDataDir)
{
	AppliedRestore applied;

	// A crash or kill mid-restore can leave a decrypted temporary archive
	// behind; never let plaintext outlive the operation that made it.
	RestoreManager::RemoveStaleTemporaryFiles(safetyBackupDir);
	// Files a previous restore had to rename aside while replacing plugins that
	// were loaded at the time.
	RestoreManager::RemovePluginReplacementLeftovers(obsDataDir);

	const auto markerPath = PendingRestoreMarkerPath(safetyBackupDir);
	std::string readError;
	if (!RestoreManager::ReadPendingRestoreMarker(markerPath, applied.marker, readError))
		return applied; // nothing pending -- the common case

	applied.wasPending = true;
	applied.commit = RestoreManager::CommitStagedRestore(
		applied.marker.stagingDir, applied.marker.targetDir, applied.marker.safetyBackupPath,
		applied.marker.fileWriteMaxAttempts, applied.marker.fileWriteRetryDelayMs);
	RestoreManager::RemovePendingRestoreMarker(markerPath);
	return applied;
}

AppliedRestore ApplyPendingRestoreAndStoreResult(const std::filesystem::path &safetyBackupDir,
						 const std::filesystem::path &obsDataDir)
{
	AppliedRestore applied = ApplyPendingRestore(safetyBackupDir, obsDataDir);
	if (applied.wasPending) {
		WriteRestoreResult(StoredRestoreResultPath(safetyBackupDir),
				   {applied.commit.success, applied.commit.rolledBack, applied.commit.errorKind,
				    applied.commit.errorMessage});
	}
	return applied;
}

bool TakeStoredRestoreResult(const std::filesystem::path &safetyBackupDir, StoredRestoreResult &result)
{
	const auto path = StoredRestoreResultPath(safetyBackupDir);
	if (!ReadRestoreResult(path, result))
		return false;
	RemoveRestoreResult(path);
	return true;
}

} // namespace obs_backuper
