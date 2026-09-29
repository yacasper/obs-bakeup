// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "RestoreManager.h"

#include "BackupManager.h"
#include "FileCollector.h"
#include "SecureFile.h"
#include "ZipArchive.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <set>
#include <system_error>
#include <thread>

namespace obs_backuper {

namespace {

constexpr const char *kManifestEntryName = "manifest.json";
constexpr const char *kSafetyBackupPrefix = "obs-backup_before-restore";
constexpr const char *kDecryptedTempPrefix = ".obs-backuper-decrypted-";
constexpr const char *kDecryptedTempSuffix = ".tmp";

bool IsManifestEntry(const std::filesystem::path &relativePath)
{
	return relativePath.generic_string() == kManifestEntryName;
}

// Extracts one archive entry to destDir, retrying a locked/unwritable
// destination file up to maxAttempts times (on Windows a file held open by another process can fail with a sharing
// violation; a short retry usually succeeds).
bool ExtractWithRetries(const ZipReader &reader, const ZipReader::Entry &entry, const std::filesystem::path &destDir,
			 int maxAttempts, int retryDelayMs, std::string &errorMessage)
{
	const std::filesystem::path destination = destDir / entry.relativePath;
	// UTF-8, matching how ZipArchive::AddFile/AddData named the entry --
	// generic_string() would go through the narrow "native" encoding, which
	// mangles non-ASCII names (e.g. a Cyrillic scene collection name) on
	// Windows.
	const std::string entryName = entry.relativePath.generic_u8string();

	for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
		if (reader.ExtractEntryToFile(entryName, destination, errorMessage))
			return true;

		if (attempt < maxAttempts)
			std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
	}
	return false;
}

std::string AsciiFold(std::string value);

// Installed plugins live in "plugins/<name>..." inside the obs-studio
// directory. Unlike settings they are executable code that OBS may already
// have loaded (mapped into memory) when a restore is applied, so they are
// never overwritten in place: overwriting a loaded library corrupts the
// running process on macOS and is refused by the OS on Windows.
constexpr const char *kPluginsDirName = "plugins";
constexpr const char *kOwnPluginPrefix = "obs-backuper"; // this plugin's own folder/bundle
constexpr const char *kReplacementNewSuffix = ".bakeup-new";
constexpr const char *kReplacementOldSuffix = ".bakeup-old";
constexpr const char *kRollbackTempSuffix = ".bakeup-rollback";

std::string NthComponent(const std::filesystem::path &path, std::size_t index)
{
	auto it = path.begin();
	for (std::size_t i = 0; i < index && it != path.end(); ++i)
		++it;
	return it == path.end() ? std::string() : AsciiFold(it->generic_u8string());
}

bool IsPluginPath(const std::filesystem::path &relativePath)
{
	return NthComponent(relativePath, 0) == kPluginsDirName && !NthComponent(relativePath, 1).empty();
}

// This plugin is running while a restore is applied, so its own files are
// never touched: replacing them could crash the very code doing the restore.
bool IsOwnPluginPath(const std::filesystem::path &relativePath)
{
	return IsPluginPath(relativePath) && NthComponent(relativePath, 1).rfind(kOwnPluginPrefix, 0) == 0;
}

// The same rule for a path inside a PluginRoot folder ("<plugin>/...").
bool IsOwnPluginInRoot(const std::filesystem::path &pathInsideRoot)
{
	return NthComponent(pathInsideRoot, 0).rfind(kOwnPluginPrefix, 0) == 0;
}

// The PluginRoot an archive path belongs to (by prefix), or null if this
// machine has no such folder.
const PluginRoot *FindPluginRoot(const std::vector<PluginRoot> &roots, const std::filesystem::path &archiveRelativePath)
{
	const std::string prefix = NthComponent(archiveRelativePath, 0);
	for (const auto &root : roots) {
		if (AsciiFold(root.archivePrefix) == prefix)
			return &root;
	}
	return nullptr;
}

// "system-plugins/foo/x.dll" -> "foo/x.dll".
std::filesystem::path StripFirstComponent(const std::filesystem::path &path)
{
	std::filesystem::path out;
	auto it = path.begin();
	if (it != path.end())
		++it;
	for (; it != path.end(); ++it)
		out /= *it;
	return out;
}

bool FilesAreIdentical(const std::filesystem::path &a, const std::filesystem::path &b)
{
	std::error_code ec;
	if (std::filesystem::file_size(a, ec) != std::filesystem::file_size(b, ec) || ec)
		return false;

	std::ifstream in1(a, std::ios::binary);
	std::ifstream in2(b, std::ios::binary);
	if (!in1.is_open() || !in2.is_open())
		return false;

	std::vector<char> buf1(64 * 1024);
	std::vector<char> buf2(64 * 1024);
	while (in1 && in2) {
		in1.read(buf1.data(), static_cast<std::streamsize>(buf1.size()));
		in2.read(buf2.data(), static_cast<std::streamsize>(buf2.size()));
		if (in1.gcount() != in2.gcount() || !std::equal(buf1.begin(), buf1.begin() + in1.gcount(), buf2.begin()))
			return false;
	}
	return true;
}

std::filesystem::path WithSuffix(const std::filesystem::path &path, const char *suffix)
{
	return std::filesystem::path(path.string() + suffix);
}

// Gives `destination` the same permission bits as `source` (macOS/Linux; on
// Windows there is nothing to copy). Best effort. Used so a restored plugin
// keeps its execute bits, including when its content was already identical.
void CopyPermissions(const std::filesystem::path &source, const std::filesystem::path &destination)
{
#if !defined(_WIN32)
	std::error_code ec;
	const auto permissions = std::filesystem::status(source, ec).permissions();
	if (!ec)
		std::filesystem::permissions(destination, permissions, std::filesystem::perm_options::replace, ec);
#else
	(void)source;
	(void)destination;
#endif
}

// Puts a copy of `source` at `destination` without ever modifying the
// destination's existing file in place:
//  - a destination with identical content is left alone (the usual case when
//    restoring on the machine the backup came from, so nothing is written;
//    only its permission bits are brought in line, which touches no content);
//  - otherwise the new content is written next to it and renamed over it,
//    which on macOS/Linux leaves anything that has the old file mapped
//    untouched (it keeps the old inode);
//  - if the rename is refused (Windows, library loaded), the old file is
//    renamed aside instead -- Windows allows that for a loaded DLL -- and the
//    new one takes its place. The "*.bakeup-old" file is removed on a later
//    start (RemovePluginReplacementLeftovers).
bool ReplaceFileSafely(const std::filesystem::path &source, const std::filesystem::path &destination,
		       std::string &errorMessage)
{
	std::error_code ec;
	if (std::filesystem::exists(destination, ec) && FilesAreIdentical(source, destination)) {
		CopyPermissions(source, destination);
		return true;
	}

	const auto fresh = WithSuffix(destination, kReplacementNewSuffix);
	std::filesystem::copy_file(source, fresh, std::filesystem::copy_options::overwrite_existing, ec);
	if (ec) {
		errorMessage = ec.message();
		return false;
	}

	CopyPermissions(source, fresh);

	std::filesystem::rename(fresh, destination, ec);
	if (!ec)
		return true;

	const auto old = WithSuffix(destination, kReplacementOldSuffix);
	std::error_code asideEc;
	std::filesystem::remove(old, asideEc);
	std::filesystem::rename(destination, old, asideEc);
	if (asideEc) {
		errorMessage = asideEc.message();
		std::filesystem::remove(fresh, asideEc);
		return false;
	}

	std::error_code placeEc;
	std::filesystem::rename(fresh, destination, placeEc);
	if (placeEc) {
		errorMessage = placeEc.message();
		std::filesystem::rename(old, destination, asideEc); // put the original back
		std::filesystem::remove(fresh, asideEc);
		return false;
	}
	return true;
}

// ReplaceFileSafely with the same retry policy as the other file writes.
bool PlaceFileWithRetries(const std::filesystem::path &source, const std::filesystem::path &destination,
			  int maxAttempts, int retryDelayMs, std::string &errorMessage)
{
	std::error_code dirEc;
	std::filesystem::create_directories(destination.parent_path(), dirEc);

	for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
		if (ReplaceFileSafely(source, destination, errorMessage))
			return true;
		if (attempt < maxAttempts)
			std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
	}
	return false;
}

// Copies one file (already sitting on disk, e.g. in a staging directory) to
// destDir, retrying a locked/unwritable destination file the same way
// ExtractWithRetries does. Plugin files go through ReplaceFileSafely, and this
// plugin's own files are skipped.
bool CopyWithRetries(const std::filesystem::path &sourceFile, const std::filesystem::path &destDir,
		      const std::filesystem::path &relativePath, int maxAttempts, int retryDelayMs,
		      std::string &errorMessage)
{
	if (IsOwnPluginPath(relativePath))
		return true;

	const std::filesystem::path destination = destDir / relativePath;
	std::error_code dirEc;
	std::filesystem::create_directories(destination.parent_path(), dirEc);

	const bool isPlugin = IsPluginPath(relativePath);
	for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
		if (isPlugin) {
			if (ReplaceFileSafely(sourceFile, destination, errorMessage))
				return true;
		} else {
			std::error_code copyEc;
			if (std::filesystem::copy_file(sourceFile, destination,
							std::filesystem::copy_options::overwrite_existing, copyEc)) {
				CopyPermissions(sourceFile, destination);
				return true;
			}
			errorMessage = copyEc.message();
		}

		if (attempt < maxAttempts)
			std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
	}
	return false;
}

std::string AsciiFold(std::string value)
{
	for (char &c : value) {
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}
	return value;
}

bool HasNonAscii(const std::string &value)
{
	return std::any_of(value.begin(), value.end(), [](char c) { return static_cast<unsigned char>(c) >= 0x80; });
}

// A relative path that stays inside its base: no root, no ".." component.
bool IsSafeRelativePath(const std::filesystem::path &path)
{
	if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
		return false;
	return std::none_of(path.begin(), path.end(), [](const std::filesystem::path &part) { return part == ".."; });
}

// Deletes the files a failed restore ADDED to targetDir: those in
// restoredPaths (what the restore wrote or would have written) that the safety
// backup does not contain, i.e. that did not exist before the restore. Files
// the restore never touched (anything outside restoredPaths, such as logs) are
// left alone, and so is anything that might be a safety-backup file under a
// different spelling -- targetDir is normally on a case-insensitive
// filesystem, where "Basic/x.json" and "basic/x.json" are the same file that
// was just rolled back. When in doubt, the file is kept. Directories the
// removal leaves empty are removed too, up to (not including) targetDir.
void RemoveFilesAddedByRestore(const std::vector<ZipReader::Entry> &safetyEntries,
			       const std::filesystem::path &targetDir,
			       const std::vector<std::filesystem::path> &restoredPaths)
{
	std::set<std::string> safetyExact;
	std::set<std::string> safetyFolded;
	std::vector<std::filesystem::path> safetyNonAscii;
	for (const auto &entry : safetyEntries) {
		if (IsManifestEntry(entry.relativePath))
			continue;
		const std::string name = entry.relativePath.generic_u8string();
		safetyExact.insert(name);
		safetyFolded.insert(AsciiFold(name));
		if (HasNonAscii(name))
			safetyNonAscii.push_back(entry.relativePath);
	}

	for (const auto &relative : restoredPaths) {
		if (IsManifestEntry(relative) || !IsSafeRelativePath(relative))
			continue;

		const std::string name = relative.generic_u8string();
		if (safetyExact.count(name) != 0 || safetyFolded.count(AsciiFold(name)) != 0)
			continue;

		const std::filesystem::path full = targetDir / relative;
		std::error_code ec;
		if (std::filesystem::symlink_status(full, ec).type() != std::filesystem::file_type::regular || ec)
			continue;

		// Non-ASCII names can differ in case in ways AsciiFold can't see; ask
		// the filesystem whether this is really one of the safety files.
		bool sameAsSafetyFile = false;
		if (HasNonAscii(name)) {
			for (const auto &safetyPath : safetyNonAscii) {
				std::error_code eqEc;
				if (std::filesystem::equivalent(full, targetDir / safetyPath, eqEc) || eqEc) {
					sameAsSafetyFile = true;
					break;
				}
			}
		}
		if (sameAsSafetyFile)
			continue;

		std::filesystem::remove(full, ec);
		if (ec)
			continue;

		for (auto parent = full.parent_path(); !parent.empty() && parent != targetDir; parent = parent.parent_path()) {
			std::error_code dirEc;
			if (!std::filesystem::is_directory(parent, dirEc) || !std::filesystem::is_empty(parent, dirEc) || dirEc)
				break;
			std::filesystem::remove(parent, dirEc);
			if (dirEc)
				break;
		}
	}
}

// Best-effort restore of targetDir from a safety backup archive, used when a
// restore fails partway through: files that existed before are put back from
// the safety backup, and files the failed restore added (restoredPaths minus
// what the safety backup holds) are deleted, so the directory ends up as it
// was. Failures on individual files are not retried here (the safety backup
// was just written by us, so a locked file is unlikely) and are ignored
// rather than escalated -- the goal is to recover as much as possible, not to
// fail a second time.
void RollbackFromSafetyBackup(const std::filesystem::path &safetyBackupPath, const std::filesystem::path &targetDir,
			      const std::vector<std::filesystem::path> &restoredPaths,
			      const std::vector<PluginRoot> &pluginRoots = {})
{
	ZipReader reader(safetyBackupPath);
	std::string error;
	if (!reader.Open(error))
		return;

	const auto entries = reader.ListEntries();
	for (const auto &entry : entries) {
		if (IsManifestEntry(entry.relativePath))
			continue;

		if (IsOwnPluginPath(entry.relativePath))
			continue; // never touched by the restore, see CopyWithRetries

		std::string extractError;
		std::filesystem::path destination = targetDir / entry.relativePath;
		bool isPlugin = IsPluginPath(entry.relativePath);
		if (IsPluginRootEntry(entry.relativePath)) {
			// A file from a plugin folder outside targetDir. Without a matching
			// folder here (the pending-restore marker does not carry them) it is
			// left alone rather than written under targetDir.
			const PluginRoot *root = FindPluginRoot(pluginRoots, entry.relativePath);
			if (!root)
				continue;
			const auto inside = StripFirstComponent(entry.relativePath);
			if (IsOwnPluginInRoot(inside))
				continue;
			destination = root->dir / inside;
			isPlugin = true;
		}
		if (isPlugin) {
			// Never overwrite a possibly loaded plugin in place.
			const auto temp = WithSuffix(destination, kRollbackTempSuffix);
			if (reader.ExtractEntryToFile(entry.relativePath.generic_u8string(), temp, extractError))
				ReplaceFileSafely(temp, destination, extractError);
			std::error_code removeEc;
			std::filesystem::remove(temp, removeEc);
			continue;
		}
		reader.ExtractEntryToFile(entry.relativePath.generic_u8string(), destination, extractError);
	}

	RemoveFilesAddedByRestore(entries, targetDir, restoredPaths);
}

// Keeps only the maxCount most recent safety backups in safetyBackupDir,
// deleting older ones. Relies on the fixed-width "obs-backup_before-restore_
// YYYY-MM-DD_HHMM[_N].zip" naming scheme sorting lexicographically the same
// as chronologically.
void RotateSafetyBackups(const std::filesystem::path &safetyBackupDir, int maxCount)
{
	if (maxCount <= 0)
		return;

	std::error_code ec;
	std::vector<std::filesystem::path> backups;
	for (const auto &entry : std::filesystem::directory_iterator(safetyBackupDir, ec)) {
		if (ec)
			break;

		const std::string name = entry.path().filename().string();
		if (entry.is_regular_file() && name.rfind(kSafetyBackupPrefix, 0) == 0)
			backups.push_back(entry.path());
	}
	if (ec || backups.size() <= static_cast<std::size_t>(maxCount))
		return;

	std::sort(backups.begin(), backups.end());

	const std::size_t excess = backups.size() - static_cast<std::size_t>(maxCount);
	for (std::size_t i = 0; i < excess; ++i) {
		std::error_code removeEc;
		std::filesystem::remove(backups[i], removeEc);
	}
}

// Shared by PerformRestore and PerformStagedRestore: snapshots targetDir into
// safetyBackupDir before anything under targetDir is touched, and rotates old
// safety backups. Returns the new safety backup's path, or empty on failure
// (with errorMessage set).
std::filesystem::path CreateSafetyBackup(const RestoreOptions &options, std::string &errorMessage)
{
	BackupOptions safetyOptions;
	safetyOptions.destinationDir = options.safetyBackupDir;
	safetyOptions.pluginVersion = options.pluginVersion;
	safetyOptions.obsVersion = options.obsVersion;
	safetyOptions.sourceOs = options.sourceOs;
	safetyOptions.sourceOsVersion = options.sourceOsVersion;
	safetyOptions.archiveBaseNamePrefix = kSafetyBackupPrefix;
	safetyOptions.appendObsVersionToFileName = false;

	auto collectedCurrentConfig = CollectFiles(options.targetDir);
	for (const auto &root : options.pluginRoots)
		CollectPluginRoot(root, collectedCurrentConfig);
	const auto safetyOutcome = BackupManager::CreateBackup(collectedCurrentConfig, safetyOptions, options.onSafetyBackupProgress);
	if (!safetyOutcome.success) {
		errorMessage = "failed to create safety backup before restoring: " + safetyOutcome.errorMessage;
		return {};
	}

	RotateSafetyBackups(options.safetyBackupDir, options.maxSafetyBackups);
	return safetyOutcome.archivePath;
}

// Trivial key=value text serialization for PendingRestoreMarker -- same
// rationale as Manifest.h's hand-rolled JSON: the field set is small and
// fixed, not worth a JSON dependency for.
constexpr const char *kStagingDirKey = "stagingDir";
constexpr const char *kTargetDirKey = "targetDir";
constexpr const char *kSafetyBackupPathKey = "safetyBackupPath";
constexpr const char *kFileWriteMaxAttemptsKey = "fileWriteMaxAttempts";
constexpr const char *kFileWriteRetryDelayMsKey = "fileWriteRetryDelayMs";

ArchiveValidationResult ValidateZip(const std::filesystem::path &archivePath)
{
	ArchiveValidationResult result;

	ZipReader reader(archivePath);
	std::string error;
	if (!reader.Open(error)) {
		result.errorMessage = "failed to open archive: " + error;
		result.errorKind = ErrorKind::InvalidArchive;
		return result;
	}

	std::string manifestJson;
	if (!reader.ReadEntryToString(kManifestEntryName, manifestJson, error)) {
		result.errorMessage = "archive does not contain a readable manifest.json: " + error;
		result.errorKind = ErrorKind::InvalidArchive;
		return result;
	}

	if (!ParseManifestJson(manifestJson, result.manifest, error)) {
		result.errorMessage = "manifest.json is corrupt: " + error;
		result.errorKind = ErrorKind::InvalidArchive;
		return result;
	}

	if (result.manifest.backupFormatVersion != kCurrentBackupFormatVersion) {
		result.errorMessage = "unsupported backup format version " +
				       std::to_string(result.manifest.backupFormatVersion) + " (this version of the plugin " +
				       "supports version " + std::to_string(kCurrentBackupFormatVersion) + ")";
		result.errorKind = ErrorKind::UnsupportedVersion;
		return result;
	}

	result.valid = true;
	return result;
}

// The archive to actually read: the file itself for a plain ZIP, or -- for an
// encrypted container -- a temporary decrypted ZIP that is deleted when this
// object goes away, on every exit path.
struct PreparedArchive {
	std::filesystem::path zipPath;
	ScopedFileDeleter temporaryZip;
};

// Resolves archivePath into a readable ZIP. On failure returns false with
// `failure` filled in (invalid, or passwordRequired for an encrypted archive
// with no password).
bool PrepareArchive(const std::filesystem::path &archivePath, const std::string &password,
		    const std::filesystem::path &tempDir, const ContainerProgressCallback &onDecryptProgress,
		    PreparedArchive &prepared, ArchiveValidationResult &failure)
{
	if (!IsEncryptedArchive(archivePath)) {
		prepared.zipPath = archivePath;
		return true;
	}

	ContainerHeader header;
	const ContainerResult headerResult = ReadContainerHeader(archivePath, header);
	if (!headerResult.success) {
		failure.errorMessage = headerResult.errorMessage;
		failure.errorKind = headerResult.errorKind;
		return false;
	}

	if (password.empty()) {
		failure.passwordRequired = true;
		failure.errorMessage = "archive is encrypted and needs a password";
		return false;
	}

	std::error_code dirEc;
	std::filesystem::create_directories(tempDir, dirEc);
	const std::filesystem::path zipPath = MakeRandomTempPath(tempDir, kDecryptedTempPrefix, kDecryptedTempSuffix);
	prepared.temporaryZip.Reset(zipPath); // registered first: DecryptFile also cleans up, this is the backstop

	const ContainerResult decrypted = DecryptFile(archivePath, zipPath, password, onDecryptProgress);
	if (!decrypted.success) {
		failure.errorMessage = "failed to decrypt archive: " + decrypted.errorMessage;
		failure.errorKind = decrypted.errorKind;
		return false;
	}

	prepared.zipPath = zipPath;
	return true;
}

} // namespace

ArchiveValidationResult RestoreManager::ValidateArchive(const std::filesystem::path &archivePath,
							 const std::string &password,
							 const std::filesystem::path &tempDir,
							 const ContainerProgressCallback &onDecryptProgress)
{
	ArchiveValidationResult failure;
	PreparedArchive prepared;
	const std::filesystem::path dir = tempDir.empty() ? std::filesystem::temp_directory_path() : tempDir;
	if (!PrepareArchive(archivePath, password, dir, onDecryptProgress, prepared, failure))
		return failure;

	return ValidateZip(prepared.zipPath);
}

void RestoreManager::RemoveStaleTemporaryFiles(const std::filesystem::path &dir)
{
	std::error_code ec;
	for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
		if (ec)
			break;
		const std::string name = entry.path().filename().string();
		if (entry.is_regular_file() && name.rfind(kDecryptedTempPrefix, 0) == 0 &&
		    name.size() >= std::string(kDecryptedTempSuffix).size() &&
		    name.compare(name.size() - std::string(kDecryptedTempSuffix).size(), std::string::npos,
				 kDecryptedTempSuffix) == 0) {
			std::error_code removeEc;
			std::filesystem::remove(entry.path(), removeEc);
		}
	}
}

void RestoreManager::RemovePluginReplacementLeftovers(const std::filesystem::path &targetDir)
{
	std::error_code ec;
	const auto pluginsDir = targetDir / kPluginsDirName;
	if (!std::filesystem::is_directory(pluginsDir, ec))
		return;

	std::filesystem::recursive_directory_iterator it(
		pluginsDir, std::filesystem::directory_options::skip_permission_denied, ec);
	const std::filesystem::recursive_directory_iterator end;
	std::vector<std::filesystem::path> leftovers;
	for (; !ec && it != end; it.increment(ec)) {
		std::error_code fileEc;
		if (!it->is_regular_file(fileEc) || fileEc)
			continue;
		const std::string name = it->path().filename().string();
		for (const char *suffix : {kReplacementOldSuffix, kReplacementNewSuffix, kRollbackTempSuffix}) {
			const std::string s = suffix;
			if (name.size() > s.size() && name.compare(name.size() - s.size(), std::string::npos, s) == 0) {
				leftovers.push_back(it->path());
				break;
			}
		}
	}
	for (const auto &path : leftovers) {
		std::error_code removeEc;
		std::filesystem::remove(path, removeEc);
	}
}

RestoreOutcome RestoreManager::PerformRestore(const RestoreOptions &options, const RestoreProgressCallback &onProgress)
{
	RestoreOutcome outcome;

	ArchiveValidationResult validation;
	PreparedArchive prepared;
	if (PrepareArchive(options.archivePath, options.password, options.safetyBackupDir, options.onDecryptProgress,
			    prepared, validation))
		validation = ValidateZip(prepared.zipPath);
	if (!validation.valid) {
		outcome.errorMessage = validation.errorMessage;
		outcome.errorKind = validation.errorKind;
		return outcome;
	}

	// 1. Safety backup of the current configuration, before anything is
	// touched, so a failed restore can be rolled back from it.
	std::string safetyError;
	outcome.safetyBackupPath = CreateSafetyBackup(options, safetyError);
	if (outcome.safetyBackupPath.empty()) {
		outcome.errorMessage = safetyError;
		outcome.errorKind = ErrorKind::SafetyBackupFailed;
		return outcome;
	}

	// 2. Extract the archive over targetDir.
	ZipReader reader(prepared.zipPath);
	std::string error;
	if (!reader.Open(error)) {
		outcome.errorMessage = "failed to re-open archive for extraction: " + error;
		outcome.errorKind = ErrorKind::RestoreExtractFailed;
		RollbackFromSafetyBackup(outcome.safetyBackupPath, options.targetDir, {});
		outcome.rolledBack = true;
		return outcome;
	}

	std::vector<ZipReader::Entry> entries;
	for (auto &entry : reader.ListEntries()) {
		// Plugin folders outside targetDir are only handled by the staged restore.
		if (!IsManifestEntry(entry.relativePath) && !IsPluginRootEntry(entry.relativePath))
			entries.push_back(std::move(entry));
	}

	const std::size_t total = entries.size();
	for (std::size_t i = 0; i < total; ++i) {
		const auto &entry = entries[i];

		if (onProgress)
			onProgress(i + 1, total, entry.relativePath);

		std::string extractError;
		if (!ExtractWithRetries(reader, entry, options.targetDir, options.fileWriteMaxAttempts,
					 options.fileWriteRetryDelayMs, extractError)) {
			outcome.errorMessage = "failed to restore \"" + entry.relativePath.generic_string() +
						"\": " + extractError;
			outcome.errorKind = ErrorKind::RestoreExtractFailed;
			std::vector<std::filesystem::path> restoredPaths;
			for (const auto &restored : entries)
				restoredPaths.push_back(restored.relativePath);
			RollbackFromSafetyBackup(outcome.safetyBackupPath, options.targetDir, restoredPaths);
			outcome.rolledBack = true;
			return outcome;
		}
	}

	outcome.success = true;
	return outcome;
}

StagedRestoreOutcome RestoreManager::PerformStagedRestore(const RestoreOptions &options,
							   const std::filesystem::path &stagingDir,
							   const RestoreProgressCallback &onProgress)
{
	StagedRestoreOutcome outcome;

	ArchiveValidationResult validation;
	PreparedArchive prepared;
	if (PrepareArchive(options.archivePath, options.password, options.safetyBackupDir, options.onDecryptProgress,
			    prepared, validation))
		validation = ValidateZip(prepared.zipPath);
	if (!validation.valid) {
		outcome.errorMessage = validation.errorMessage;
		outcome.errorKind = validation.errorKind;
		return outcome;
	}

	// 1. Safety backup of the *real* current configuration -- targetDir, not
	// stagingDir -- before anything under targetDir is touched (it still
	// isn't, by this whole function: extraction below goes into stagingDir).
	std::string safetyError;
	outcome.safetyBackupPath = CreateSafetyBackup(options, safetyError);
	if (outcome.safetyBackupPath.empty()) {
		outcome.errorMessage = safetyError;
		outcome.errorKind = ErrorKind::SafetyBackupFailed;
		return outcome;
	}

	// 2. Extract the archive into stagingDir instead of targetDir.
	std::error_code dirEc;
	std::filesystem::remove_all(stagingDir, dirEc);
	std::filesystem::create_directories(stagingDir, dirEc);
	// Staged files may hold stream keys in the clear until the next OBS start;
	// keep them to the current user.
	RestrictDirectoryToOwner(stagingDir);

	ZipReader reader(prepared.zipPath);
	std::string error;
	if (!reader.Open(error)) {
		outcome.errorMessage = "failed to re-open archive for extraction: " + error;
		outcome.errorKind = ErrorKind::RestoreExtractFailed;
		return outcome;
	}

	std::vector<ZipReader::Entry> entries;
	for (auto &entry : reader.ListEntries()) {
		if (!IsManifestEntry(entry.relativePath))
			entries.push_back(std::move(entry));
	}

	// Plugin files are put in place right here, not at the next start: OBS
	// lists the plugins to load before this plugin runs at startup, so files
	// restored later would only be loaded on the launch after that, i.e. the
	// user would have to restart OBS twice. ReplaceFileSafely never modifies a
	// file OBS may have loaded, so doing it while OBS runs is safe. Settings
	// still wait for the restart (see PerformStagedRestore's doc comment).
	std::vector<std::filesystem::path> appliedPluginPaths;
	bool placedInPluginRoots = false; // any file written into a PluginRoot folder

	const std::size_t total = entries.size();
	for (std::size_t i = 0; i < total; ++i) {
		const auto &entry = entries[i];

		if (onProgress)
			onProgress(i + 1, total, entry.relativePath);

		// Plugins in a folder outside targetDir (Windows: %ProgramData%\obs-
		// studio\plugins). Best effort: Windows may refuse the write without
		// administrator rights, and that must not fail the whole restore.
		if (IsPluginRootEntry(entry.relativePath)) {
			const PluginRoot *root = FindPluginRoot(options.pluginRoots, entry.relativePath);
			if (!root)
				continue; // no such folder on this machine (e.g. a backup from another OS)
			const auto inside = StripFirstComponent(entry.relativePath);
			if (IsOwnPluginInRoot(inside))
				continue;

			const auto destination = root->dir / inside;
			std::string placeError;
			bool placed = ExtractWithRetries(reader, entry, stagingDir, options.fileWriteMaxAttempts,
							  options.fileWriteRetryDelayMs, placeError);
			if (placed) {
				placed = PlaceFileWithRetries(stagingDir / entry.relativePath, destination,
							      options.fileWriteMaxAttempts, options.fileWriteRetryDelayMs,
							      placeError);
				if (placed)
					placedInPluginRoots = true;
			}
			std::error_code removeEc;
			std::filesystem::remove(stagingDir / entry.relativePath, removeEc);

			if (!placed) {
				if (outcome.pluginFilesFailed == 0)
					outcome.pluginFailureMessage = destination.string() + ": " + placeError;
				++outcome.pluginFilesFailed;
			}
			continue;
		}

		if (IsOwnPluginPath(entry.relativePath))
			continue; // the running copy of this plugin is never replaced

		std::string extractError;
		bool ok = ExtractWithRetries(reader, entry, stagingDir, options.fileWriteMaxAttempts,
					      options.fileWriteRetryDelayMs, extractError);
		if (ok && IsPluginPath(entry.relativePath)) {
			ok = PlaceFileWithRetries(stagingDir / entry.relativePath, options.targetDir / entry.relativePath,
						  options.fileWriteMaxAttempts, options.fileWriteRetryDelayMs, extractError);
			if (ok)
				appliedPluginPaths.push_back(entry.relativePath);
			std::error_code removeEc;
			std::filesystem::remove(stagingDir / entry.relativePath, removeEc); // in place, nothing to commit later
		}

		if (!ok) {
			// Settings were not touched. Plugin files that were already put in
			// place are put back from the safety backup, so the outcome is
			// all-or-nothing.
			if (!appliedPluginPaths.empty() || placedInPluginRoots) {
				RollbackFromSafetyBackup(outcome.safetyBackupPath, options.targetDir, appliedPluginPaths,
							  options.pluginRoots);
				outcome.rolledBack = true;
			}
			std::error_code cleanupEc;
			std::filesystem::remove_all(stagingDir, cleanupEc);
			outcome.errorMessage =
				"failed to stage \"" + entry.relativePath.generic_string() + "\": " + extractError;
			outcome.errorKind = ErrorKind::RestoreExtractFailed;
			return outcome;
		}
	}

	outcome.stagingDir = stagingDir;
	outcome.success = true;
	return outcome;
}

CommitOutcome RestoreManager::CommitStagedRestore(const std::filesystem::path &stagingDir,
						   const std::filesystem::path &targetDir,
						   const std::filesystem::path &safetyBackupPath, int fileWriteMaxAttempts,
						   int fileWriteRetryDelayMs, const RestoreProgressCallback &onProgress)
{
	CommitOutcome outcome;

	const auto staged = CollectFiles(stagingDir);
	const std::size_t total = staged.files.size();

	for (std::size_t i = 0; i < total; ++i) {
		const auto &file = staged.files[i];

		if (onProgress)
			onProgress(i + 1, total, file.relativePath);

		std::string copyError;
		if (!CopyWithRetries(file.absolutePath, targetDir, file.relativePath, fileWriteMaxAttempts,
				      fileWriteRetryDelayMs, copyError)) {
			outcome.errorMessage = "failed to apply staged restore for \"" + file.relativePath.generic_string() +
						"\": " + copyError;
			outcome.errorKind = ErrorKind::RestoreApplyFailed;
			if (!safetyBackupPath.empty()) {
				std::vector<std::filesystem::path> restoredPaths;
				for (const auto &stagedFile : staged.files)
					restoredPaths.push_back(stagedFile.relativePath);
				RollbackFromSafetyBackup(safetyBackupPath, targetDir, restoredPaths);
				outcome.rolledBack = true;
			}
			std::error_code cleanupEc;
			std::filesystem::remove_all(stagingDir, cleanupEc);
			return outcome;
		}
	}

	std::error_code cleanupEc;
	std::filesystem::remove_all(stagingDir, cleanupEc);

	outcome.success = true;
	return outcome;
}

namespace {

std::string EscapeMarkerValue(const std::string &value)
{
	// Paths can't contain newlines on any of our target platforms, so the
	// only thing a key=value line format needs to guard against is a value
	// that happens to be empty -- nothing to escape either way. Kept as a
	// named step so a future field with riskier content has an obvious place
	// to add real escaping.
	return value;
}

} // namespace

bool RestoreManager::WritePendingRestoreMarker(const std::filesystem::path &markerPath, const PendingRestoreMarker &marker,
						std::string &errorMessage)
{
	std::error_code dirEc;
	std::filesystem::create_directories(markerPath.parent_path(), dirEc);

	std::ofstream out(markerPath, std::ios::binary | std::ios::trunc);
	if (!out.is_open()) {
		errorMessage = "failed to open pending-restore marker for writing: " + markerPath.string();
		return false;
	}

	out << kStagingDirKey << '=' << EscapeMarkerValue(marker.stagingDir.string()) << '\n';
	out << kTargetDirKey << '=' << EscapeMarkerValue(marker.targetDir.string()) << '\n';
	out << kSafetyBackupPathKey << '=' << EscapeMarkerValue(marker.safetyBackupPath.string()) << '\n';
	out << kFileWriteMaxAttemptsKey << '=' << marker.fileWriteMaxAttempts << '\n';
	out << kFileWriteRetryDelayMsKey << '=' << marker.fileWriteRetryDelayMs << '\n';

	if (!out.good()) {
		errorMessage = "failed to write pending-restore marker: " + markerPath.string();
		return false;
	}
	return true;
}

bool RestoreManager::ReadPendingRestoreMarker(const std::filesystem::path &markerPath, PendingRestoreMarker &outMarker,
					       std::string &errorMessage)
{
	std::ifstream in(markerPath, std::ios::binary);
	if (!in.is_open()) {
		errorMessage = "no pending-restore marker at: " + markerPath.string();
		return false;
	}

	PendingRestoreMarker marker;
	std::string line;
	while (std::getline(in, line)) {
		const auto separator = line.find('=');
		if (separator == std::string::npos)
			continue;

		const std::string key = line.substr(0, separator);
		const std::string value = line.substr(separator + 1);

		if (key == kStagingDirKey)
			marker.stagingDir = value;
		else if (key == kTargetDirKey)
			marker.targetDir = value;
		else if (key == kSafetyBackupPathKey)
			marker.safetyBackupPath = value;
		else if (key == kFileWriteMaxAttemptsKey)
			marker.fileWriteMaxAttempts = std::atoi(value.c_str());
		else if (key == kFileWriteRetryDelayMsKey)
			marker.fileWriteRetryDelayMs = std::atoi(value.c_str());
	}

	if (marker.stagingDir.empty() || marker.targetDir.empty()) {
		errorMessage = "pending-restore marker is missing required fields: " + markerPath.string();
		return false;
	}

	outMarker = marker;
	return true;
}

void RestoreManager::RemovePendingRestoreMarker(const std::filesystem::path &markerPath)
{
	std::error_code ec;
	std::filesystem::remove(markerPath, ec);
}

} // namespace obs_backuper
