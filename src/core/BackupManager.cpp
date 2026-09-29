// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "BackupManager.h"

#include "SecureFile.h"
#include "ZipArchive.h"

#include <array>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <system_error>

namespace obs_backuper {

const std::vector<std::string> kDefaultExcludedTopLevelSections = {"logs", "crashes"};

namespace {

std::tm ToLocalTime(std::chrono::system_clock::time_point tp)
{
	const std::time_t time = std::chrono::system_clock::to_time_t(tp);
	std::tm result{};
#if defined(_WIN32)
	localtime_s(&result, &time);
#else
	localtime_r(&time, &result);
#endif
	return result;
}

std::string FormatTwoDigits(int value)
{
	std::array<char, 4> buf{};
	std::snprintf(buf.data(), buf.size(), "%02d", value);
	return std::string(buf.data());
}

bool DirectoryIsWritable(const std::filesystem::path &dir, std::string &errorMessage)
{
	std::error_code ec;
	if (!std::filesystem::exists(dir, ec) || ec) {
		if (!std::filesystem::create_directories(dir, ec) || ec) {
			errorMessage = "destination directory does not exist and could not be created: " + dir.string();
			return false;
		}
	}

	const auto probePath = dir / ".obs-backuper-write-check.tmp";
	std::ofstream probe(probePath, std::ios::binary);
	if (!probe.is_open()) {
		errorMessage = "no write permission for destination directory: " + dir.string();
		return false;
	}
	probe.close();
	std::filesystem::remove(probePath, ec);
	return true;
}

bool HasEnoughDiskSpace(const std::filesystem::path &dir, std::uintmax_t requiredBytes, std::string &errorMessage)
{
	std::error_code ec;
	const auto spaceInfo = std::filesystem::space(dir, ec);
	if (ec) {
		// Could not determine free disk space -- do not block the backup
		// because of a diagnostic check that itself failed.
		return true;
	}

	if (spaceInfo.available < requiredBytes) {
		errorMessage = "not enough free disk space in destination directory";
		return false;
	}
	return true;
}

// Keeps letters, digits, '.', '+' and '-'; everything else becomes '-'.
std::string SanitizeForFileName(const std::string &value)
{
	std::string out;
	for (const char c : value) {
		const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '.' ||
				 c == '+' || c == '-';
		out.push_back(ok ? c : '-');
	}
	return out;
}

} // namespace

std::string BackupManager::GenerateBackupBaseFileName(std::chrono::system_clock::time_point now, const std::string &prefix,
							  const std::string &extension, const std::string &obsVersion)
{
	const std::tm local = ToLocalTime(now);

	std::string name = prefix + "_" + std::to_string(local.tm_year + 1900) + "-" + FormatTwoDigits(local.tm_mon + 1) +
			   "-" + FormatTwoDigits(local.tm_mday) + "_" + FormatTwoDigits(local.tm_hour) +
			   FormatTwoDigits(local.tm_min);
	if (!obsVersion.empty())
		name += "_OBS-" + SanitizeForFileName(obsVersion);
	return name + extension;
}

std::filesystem::path BackupManager::ResolveUniqueBackupPath(const std::filesystem::path &destinationDir,
							       const std::string &baseFileName)
{
	const std::filesystem::path candidate = destinationDir / baseFileName;

	std::error_code ec;
	if (!std::filesystem::exists(candidate, ec))
		return candidate;

	const std::filesystem::path stem = candidate.stem();
	const std::filesystem::path extension = candidate.extension();

	for (int suffix = 1;; ++suffix) {
		const std::filesystem::path attempt =
			destinationDir / (stem.string() + "_" + std::to_string(suffix) + extension.string());
		if (!std::filesystem::exists(attempt, ec))
			return attempt;
	}
}

BackupOutcome BackupManager::CreateBackup(const CollectionResult &collected, const BackupOptions &options,
					   const BackupProgressCallback &onProgress)
{
	BackupOutcome outcome;

	std::string checkError;
	if (!DirectoryIsWritable(options.destinationDir, checkError)) {
		outcome.errorMessage = checkError;
		outcome.errorKind = ErrorKind::DestinationNotWritable;
		return outcome;
	}

	const bool encrypt = !options.password.empty();

	// An encrypted backup briefly needs the plain ZIP and the ciphertext side
	// by side.
	if (!HasEnoughDiskSpace(options.destinationDir, collected.totalSizeBytes * (encrypt ? 2 : 1), checkError)) {
		outcome.errorMessage = checkError;
		outcome.errorKind = ErrorKind::NotEnoughDiskSpace;
		return outcome;
	}

	const auto now = std::chrono::system_clock::now();
	const std::string baseFileName =
		GenerateBackupBaseFileName(now, options.archiveBaseNamePrefix, encrypt ? ".obsbak" : ".zip",
				   options.appendObsVersionToFileName ? options.obsVersion : std::string());
	const std::filesystem::path archivePath = ResolveUniqueBackupPath(options.destinationDir, baseFileName);

	// Where the ZIP itself is built: the final path for a plain backup; for an
	// encrypted one, an unpredictable temporary file in the destination folder,
	// created owner-only up front and deleted on every exit path below.
	std::filesystem::path zipPath = archivePath;
	ScopedFileDeleter temporaryZip;
	if (encrypt) {
		zipPath = MakeRandomTempPath(options.destinationDir, ".obs-backuper-plain-", ".tmp");
		std::FILE *created = OpenForWrite(zipPath, /*ownerOnly=*/true, /*exclusive=*/true);
		if (created == nullptr) {
			outcome.errorMessage = "failed to create temporary archive in: " + options.destinationDir.string();
			outcome.errorKind = ErrorKind::DestinationNotWritable;
			return outcome;
		}
		std::fclose(created);
		temporaryZip.Reset(zipPath);
	}

	ZipArchive archive(zipPath);
	std::string zipError;
	if (!archive.Open(zipError)) {
		outcome.errorMessage = "failed to create archive: " + zipError;
		outcome.errorKind = ErrorKind::ArchiveWriteFailed;
		return outcome;
	}

	const std::size_t total = collected.files.size();
	for (std::size_t i = 0; i < total; ++i) {
		const auto &file = collected.files[i];

		if (onProgress)
			onProgress(i + 1, total, file.relativePath);

		std::string addError;
		if (!archive.AddFile(file.absolutePath, file.relativePath, addError))
			outcome.warnings.push_back({file.relativePath, addError});
	}

	ManifestInfo manifestInfo;
	manifestInfo.pluginVersion = options.pluginVersion;
	manifestInfo.sourceOs = options.sourceOs;
	manifestInfo.sourceOsVersion = options.sourceOsVersion;
	manifestInfo.obsVersion = options.obsVersion;
	manifestInfo.includedSections = kDefaultIncludedTopLevelEntries;
	manifestInfo.excludedSections = kDefaultExcludedTopLevelSections;

	{
		std::array<char, 32> timeBuf{};
		const std::time_t nowTimeT = std::chrono::system_clock::to_time_t(now);
		std::tm utc{};
#if defined(_WIN32)
		gmtime_s(&utc, &nowTimeT);
#else
		gmtime_r(&nowTimeT, &utc);
#endif
		std::strftime(timeBuf.data(), timeBuf.size(), "%Y-%m-%dT%H:%M:%SZ", &utc);
		manifestInfo.createdAtIso8601 = timeBuf.data();
	}

	const std::string manifestJson = BuildManifestJson(manifestInfo);
	std::string manifestError;
	if (!archive.AddData(manifestJson, "manifest.json", manifestError)) {
		archive.Close(zipError);
		outcome.errorMessage = "failed to write manifest.json: " + manifestError;
		outcome.errorKind = ErrorKind::ArchiveWriteFailed;
		return outcome;
	}

	if (!archive.Close(zipError)) {
		outcome.errorMessage = "failed to finalize archive: " + zipError;
		outcome.errorKind = ErrorKind::ArchiveWriteFailed;
		return outcome;
	}

	if (encrypt) {
		const ContainerResult encrypted = EncryptFile(zipPath, archivePath, options.password,
							       options.onEncryptProgress, options.encryptionParams);
		if (!encrypted.success) {
			outcome.errorMessage = "failed to encrypt archive: " + encrypted.errorMessage;
			outcome.errorKind = encrypted.errorKind;
			return outcome;
		}
	}

	std::error_code sizeEc;
	outcome.archiveSizeBytes = std::filesystem::file_size(archivePath, sizeEc);
	outcome.archivePath = archivePath;
	outcome.success = true;
	return outcome;
}

} // namespace obs_backuper
