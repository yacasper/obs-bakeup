// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "EncryptedContainer.h"
#include "ErrorKind.h"
#include "FileCollector.h"
#include "Manifest.h"

namespace obs_backuper {

// Sections intentionally excluded from the backup by default --
// purely diagnostic data that doesn't affect configuration recoverability.
extern const std::vector<std::string> kDefaultExcludedTopLevelSections;

struct BackupOptions {
	std::filesystem::path destinationDir;
	std::string pluginVersion;
	std::string obsVersion;
	std::string sourceOs;	      // "windows" | "macos"
	std::string sourceOsVersion; // may be empty

	// Prefix for the generated archive file name (see
	// GenerateBackupBaseFileName). RestoreManager overrides this to
	// "obs-backup_before-restore" for its automatic safety backups.
	std::string archiveBaseNamePrefix = "obs-backup";

	// Put obsVersion into the archive file name. RestoreManager turns this off
	// for its safety backups, whose names are sorted to rotate the oldest ones.
	bool appendObsVersionToFileName = true;

	// Stage 7: non-empty = protect the backup with this password (UTF-8,
	// already normalized by the caller). The archive is then built as a ZIP in
	// a temporary file inside destinationDir, encrypted into
	// "<prefix>_<date>.obsbak", and the temporary ZIP is deleted whatever the
	// outcome. Empty = the plain .zip, exactly as before. The password is
	// never stored, logged or put into any message.
	std::string password;
	EncryptionParams encryptionParams = DefaultEncryptionParams();

	// Progress of the encryption phase (plaintext bytes done / total), called
	// after the last file was archived. May be invoked from a background thread.
	ContainerProgressCallback onEncryptProgress;
};

struct BackupFileWarning {
	std::filesystem::path relativePath;
	std::string message;
};

struct BackupOutcome {
	bool success = false;
	std::filesystem::path archivePath;
	std::uintmax_t archiveSizeBytes = 0;
	std::string errorMessage;
	ErrorKind errorKind = ErrorKind::None;
	std::vector<BackupFileWarning> warnings;
};

// current/total — 1-based index of the file being processed; currentFile —
// its relative path inside the archive.
using BackupProgressCallback =
	std::function<void(std::size_t current, std::size_t total, const std::filesystem::path &currentFile)>;

// Stage 3: the pure, testable half of the "Create Backup" flow — packing an
// already-collected file list into a ZIP archive. Collecting the file list
// itself (obs_backuper::CollectFiles(GetObsDataDir())) stays with the caller,
// since GetObsDataDir() depends on OBS/Qt (see src/ObsConfigPathProvider.h)
// and must not be linked into the OBS/Qt-free obs-backuper-core.
class BackupManager {
public:
	// Builds the base archive file name "<prefix>_YYYY-MM-DD_HHMM<extension>"
	// from local time (extension: ".zip", or ".obsbak" for encrypted backups).
	// A non-empty obsVersion is appended as "_OBS-<version>" before the
	// extension, so it is visible which OBS version a backup was made with
	// (characters that are unsafe in a file name are replaced by '-').
	static std::string GenerateBackupBaseFileName(std::chrono::system_clock::time_point now,
						       const std::string &prefix = "obs-backup",
						       const std::string &extension = ".zip",
						       const std::string &obsVersion = {});

	// Returns a path in destinationDir guaranteed not to collide with an
	// existing file: on collision, appends a "_1", "_2", ... suffix before the
	// extension.
	static std::filesystem::path ResolveUniqueBackupPath(const std::filesystem::path &destinationDir,
							       const std::string &baseFileName);

	// Runs the full backup flow: checks write permissions/free disk space,
	// archives collected.files into a ZIP in options.destinationDir, adds
	// manifest.json. A failure to read an individual file does not abort the
	// whole backup — that file is skipped and recorded in
	// BackupOutcome::warnings. onProgress, if set, is called synchronously
	// before adding each file (it may be invoked from a background thread —
	// the caller is responsible for delivering it to the UI thread safely).
	static BackupOutcome CreateBackup(const CollectionResult &collected, const BackupOptions &options,
					   const BackupProgressCallback &onProgress = {});
};

} // namespace obs_backuper
