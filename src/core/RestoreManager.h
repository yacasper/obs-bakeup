// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "EncryptedContainer.h"
#include "ErrorKind.h"
#include "Manifest.h"

namespace obs_backuper {

// The full "Restore from Backup" flow (Stage 4): validating the archive,
// making a safety backup of the current configuration, extracting the
// archive over it, and rolling back automatically if extraction fails
// partway through. Pure, testable logic with no OBS/Qt dependency -- mirrors
// how BackupManager (Stage 3) is structured.
struct RestoreOptions {
	std::filesystem::path archivePath;
	std::filesystem::path targetDir;	  // the obs-studio directory to restore into
	std::filesystem::path safetyBackupDir;	  // e.g. "<OBS config parent>/obs-backuper-safety"
	int maxSafetyBackups = 5;		  // oldest safety backups beyond this count are deleted

	// Metadata for the safety backup's own manifest.json (see BackupOptions).
	std::string pluginVersion;
	std::string obsVersion;
	std::string sourceOs;
	std::string sourceOsVersion;

	// A locked/unwritable destination file is retried this many times, with
	// this delay between attempts, before the restore is treated as failed
	// (on Windows a file held open by another process can fail with a sharing
	// violation; a short retry usually succeeds).
	int fileWriteMaxAttempts = 5;
	int fileWriteRetryDelayMs = 200;

	// Stage 7: password for an encrypted (.obsbak) archive; ignored for a
	// plain .zip. An encrypted archive is decrypted once into a temporary,
	// owner-only ZIP inside safetyBackupDir, restored from, and that ZIP is
	// deleted whatever the outcome. Never stored, logged or put into a message.
	std::string password;

	// Progress of the safety backup of the current configuration taken before
	// anything is restored (per file, like BackupProgressCallback). It can take
	// a while for a large obs-studio directory, so the UI shows it as its own
	// stage instead of appearing stuck. May be invoked from a background thread.
	std::function<void(std::size_t current, std::size_t total, const std::filesystem::path &currentFile)>
		onSafetyBackupProgress;

	// Progress of that decryption (plaintext bytes done / total). May be
	// invoked from a background thread.
	ContainerProgressCallback onDecryptProgress;
};

struct ArchiveValidationResult {
	bool valid = false;

	// True when the archive is a well-formed encrypted container but no
	// password was supplied (valid is false, errorKind None) -- the caller
	// should ask for one and validate again.
	bool passwordRequired = false;
	ManifestInfo manifest;
	std::string errorMessage;
	ErrorKind errorKind = ErrorKind::None;
};

struct RestoreOutcome {
	bool success = false;
	std::string errorMessage;
	ErrorKind errorKind = ErrorKind::None;

	// True if extraction failed partway through and the target directory was
	// automatically restored from the safety backup taken just before.
	bool rolledBack = false;

	std::filesystem::path safetyBackupPath; // set once the safety backup exists
};

// Result of PerformStagedRestore -- extraction into a staging directory, not
// targetDir itself. See PerformStagedRestore's doc comment for why this
// two-step handoff exists.
struct StagedRestoreOutcome {
	bool success = false;
	std::string errorMessage;
	ErrorKind errorKind = ErrorKind::None;
	std::filesystem::path stagingDir;	 // set once staging succeeds -- holds the extracted archive contents
	std::filesystem::path safetyBackupPath; // set once the safety backup of targetDir succeeds
};

struct CommitOutcome {
	bool success = false;
	std::string errorMessage;
	ErrorKind errorKind = ErrorKind::None;

	// True if copying from stagingDir failed partway through and targetDir was
	// automatically restored from safetyBackupPath.
	bool rolledBack = false;

	// How many plugin files the restore actually wrote (added or replaced;
	// files that were already identical don't count). OBS builds its list of
	// plugins before this plugin gets to apply a restore, so plugins restored
	// here are only loaded on the launch after this one.
	int pluginFilesChanged = 0;
};

// Everything CommitStagedRestore needs, persisted to disk by the UI layer
// between "stage now" (while OBS is still running) and "commit" (at the next
// obs_module_load(), before OBS has loaded any scene collection into memory
// this session). OBS writes its own in-memory copy of any scene collection it
// has loaded back to disk on exit, which would silently undo a restore applied
// earlier in the same session.
struct PendingRestoreMarker {
	std::filesystem::path stagingDir;
	std::filesystem::path targetDir;
	std::filesystem::path safetyBackupPath;
	int fileWriteMaxAttempts = 5;
	int fileWriteRetryDelayMs = 200;
};

// current/total — 1-based index of the file being processed; currentFile —
// its relative path inside the archive (PerformRestore/PerformStagedRestore)
// or relative to stagingDir (CommitStagedRestore).
using RestoreProgressCallback =
	std::function<void(std::size_t current, std::size_t total, const std::filesystem::path &currentFile)>;

class RestoreManager {
public:
	// Opens the archive and parses+validates manifest.json (present, well
	// formed, backup_format_version supported). Does not touch targetDir --
	// safe to call before showing the user the confirmation dialog.
	//
	// Three outcomes: a valid archive; an encrypted one when `password` is
	// empty (passwordRequired = true); anything else invalid, with an
	// errorKind. With a password, an encrypted archive is decrypted into a
	// temporary file in tempDir (system temp dir if empty) to read its
	// manifest, which is deleted before returning -- this costs a full
	// decryption, so run it off the UI thread.
	static ArchiveValidationResult ValidateArchive(const std::filesystem::path &archivePath,
							const std::string &password = {},
							const std::filesystem::path &tempDir = {},
							const ContainerProgressCallback &onDecryptProgress = {});

	// Removes "*.bakeup-old"/"*.bakeup-new" files that replacing a plugin's files
	// left under <targetDir>/plugins (a plugin library that was loaded at the
	// time could only be renamed aside, not deleted). Call at startup.
	static void RemovePluginReplacementLeftovers(const std::filesystem::path &targetDir);

	// Deletes decrypted-archive temporaries a crash or kill left behind in
	// dir (see RestoreOptions::password). Call at startup.
	static void RemoveStaleTemporaryFiles(const std::filesystem::path &dir);

	// Runs the full restore flow directly against targetDir. Assumes the
	// archive was already validated (e.g. via ValidateArchive, as the caller
	// must do to build the confirmation dialog) -- still re-validates
	// internally so this function alone is safe to call/test in isolation.
	//
	// Restoring directly into a live obs-studio directory this way is only
	// safe if targetDir's scene collections/profiles have not been loaded
	// into the running OBS process's memory this session -- OBS overwrites
	// whatever's on disk with its own in-memory copy of anything it has
	// touched when it next exits cleanly, silently undoing the restore. Prefer PerformStagedRestore +
	// CommitStagedRestore from a UI that can't guarantee that.
	static RestoreOutcome PerformRestore(const RestoreOptions &options, const RestoreProgressCallback &onProgress = {});

	// Stage 1 of the deferred restore: validates the archive, snapshots
	// targetDir for safety, then extracts the archive into stagingDir instead
	// of targetDir. Safe to run at any time while OBS is live, since nothing
	// under targetDir is touched.
	static StagedRestoreOutcome PerformStagedRestore(const RestoreOptions &options,
							  const std::filesystem::path &stagingDir,
							  const RestoreProgressCallback &onProgress = {});

	// Stage 2: copies a previously staged restore from stagingDir onto
	// targetDir (same locked-file retry policy as PerformRestore), rolling
	// back from safetyBackupPath on failure. Deletes stagingDir once done
	// (whether it succeeded or had to roll back). Meant to be called from
	// obs_module_load(), before OBS has read any scene collection from disk
	// this session.
	static CommitOutcome CommitStagedRestore(const std::filesystem::path &stagingDir,
						  const std::filesystem::path &targetDir,
						  const std::filesystem::path &safetyBackupPath, int fileWriteMaxAttempts,
						  int fileWriteRetryDelayMs, const RestoreProgressCallback &onProgress = {});

	// Persists/reads/removes the marker that hands PerformStagedRestore's
	// result off to the next obs_module_load(). A plain key=value text file,
	// not JSON -- the field set is small and fixed, same rationale as
	// Manifest.h's hand-rolled (de)serialization.
	static bool WritePendingRestoreMarker(const std::filesystem::path &markerPath, const PendingRestoreMarker &marker,
					       std::string &errorMessage);
	static bool ReadPendingRestoreMarker(const std::filesystem::path &markerPath, PendingRestoreMarker &outMarker,
					      std::string &errorMessage);
	static void RemovePendingRestoreMarker(const std::filesystem::path &markerPath);
};

} // namespace obs_backuper
