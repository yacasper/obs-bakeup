// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "core/RestoreManager.h"

#include "core/BackupManager.h"
#include "core/FileCollector.h"
#include "core/ZipArchive.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

using namespace obs_backuper;

namespace {

class TempDirFixture {
public:
	TempDirFixture() : root(std::filesystem::temp_directory_path() / "obs-backuper-restoretests" / UniqueName())
	{
		std::filesystem::create_directories(root);
	}

	~TempDirFixture() { std::filesystem::remove_all(root.parent_path()); }

	const std::filesystem::path root;

private:
	static std::string UniqueName()
	{
		static int counter = 0;
		return "fixture-" + std::to_string(++counter);
	}
};

std::filesystem::path WriteFile(const std::filesystem::path &path, std::string_view content)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream file(path, std::ios::binary);
	file << content;
	return path;
}

std::string ReadFile(const std::filesystem::path &path)
{
	std::ifstream file(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

// Builds a valid backup archive (global.ini + basic/scenes/Record.json) the
// way BackupManager::CreateBackup would, for use as PerformRestore/
// ValidateArchive input.
std::filesystem::path BuildValidArchive(const std::filesystem::path &destDir, const std::filesystem::path &sourceDir,
					 std::string_view globalIniContent = "size=1\n")
{
	std::filesystem::create_directories(destDir);

	CollectionResult collected;
	const auto file1 = WriteFile(sourceDir / "global.ini", globalIniContent);
	const auto file2 = WriteFile(sourceDir / "basic" / "scenes" / "Record.json", R"({"scene":"original"})");
	collected.files = {
		{"global.ini", file1, std::filesystem::file_size(file1)},
		{std::filesystem::path("basic") / "scenes" / "Record.json", file2, std::filesystem::file_size(file2)},
	};
	collected.totalSizeBytes = collected.files[0].sizeBytes + collected.files[1].sizeBytes;

	BackupOptions options;
	options.destinationDir = destDir;
	options.pluginVersion = "1.0.0";
	options.obsVersion = "30.2.0";
	options.sourceOs = "macos";
	options.sourceOsVersion = "14.0";

	const auto outcome = BackupManager::CreateBackup(collected, options);
	REQUIRE(outcome.success);
	return outcome.archivePath;
}

RestoreOptions MakeRestoreOptions(const std::filesystem::path &archivePath, const std::filesystem::path &targetDir,
				   const std::filesystem::path &safetyDir)
{
	RestoreOptions options;
	options.archivePath = archivePath;
	options.targetDir = targetDir;
	options.safetyBackupDir = safetyDir;
	options.pluginVersion = "1.0.0";
	options.obsVersion = "30.2.0";
	options.sourceOs = "macos";
	options.sourceOsVersion = "14.0";
	options.fileWriteMaxAttempts = 2;
	options.fileWriteRetryDelayMs = 1;
	return options;
}

} // namespace

TEST_CASE("ValidateArchive accepts a well-formed archive", "[RestoreManager]")
{
	TempDirFixture fixture;
	const auto archivePath = BuildValidArchive(fixture.root / "backups", fixture.root / "source");

	const auto result = RestoreManager::ValidateArchive(archivePath);

	REQUIRE(result.valid);
	CHECK(result.errorMessage.empty());
	CHECK(result.manifest.pluginVersion == "1.0.0");
	CHECK(result.manifest.obsVersion == "30.2.0");
	CHECK(result.manifest.sourceOs == "macos");
	CHECK(result.manifest.backupFormatVersion == kCurrentBackupFormatVersion);
}

TEST_CASE("ValidateArchive rejects a file that does not exist", "[RestoreManager]")
{
	TempDirFixture fixture;

	const auto result = RestoreManager::ValidateArchive(fixture.root / "does-not-exist.zip");

	CHECK_FALSE(result.valid);
	CHECK_FALSE(result.errorMessage.empty());
	CHECK(result.errorKind == ErrorKind::InvalidArchive);
}

TEST_CASE("ValidateArchive rejects an archive with no manifest.json", "[RestoreManager]")
{
	TempDirFixture fixture;
	const auto zipPath = fixture.root / "no-manifest.zip";

	ZipArchive archive(zipPath);
	std::string error;
	REQUIRE(archive.Open(error));
	REQUIRE(archive.AddData("size=1\n", "global.ini", error));
	REQUIRE(archive.Close(error));

	const auto result = RestoreManager::ValidateArchive(zipPath);

	CHECK_FALSE(result.valid);
	CHECK(result.errorMessage.find("manifest.json") != std::string::npos);
	CHECK(result.errorKind == ErrorKind::InvalidArchive);
}

TEST_CASE("ValidateArchive rejects a manifest.json without a backup_format_version", "[RestoreManager]")
{
	TempDirFixture fixture;
	const auto zipPath = fixture.root / "corrupt.zip";

	ZipArchive archive(zipPath);
	std::string error;
	REQUIRE(archive.Open(error));
	REQUIRE(archive.AddData("{\"plugin_version\": \"1.0.0\"}", "manifest.json", error));
	REQUIRE(archive.Close(error));

	const auto result = RestoreManager::ValidateArchive(zipPath);

	CHECK_FALSE(result.valid);
	CHECK_FALSE(result.errorMessage.empty());
	CHECK(result.errorKind == ErrorKind::InvalidArchive);
}

TEST_CASE("ValidateArchive rejects an unsupported backup_format_version", "[RestoreManager]")
{
	TempDirFixture fixture;
	const auto zipPath = fixture.root / "future-version.zip";

	ZipArchive archive(zipPath);
	std::string error;
	REQUIRE(archive.Open(error));
	REQUIRE(archive.AddData("{\"backup_format_version\": 999}", "manifest.json", error));
	REQUIRE(archive.Close(error));

	const auto result = RestoreManager::ValidateArchive(zipPath);

	CHECK_FALSE(result.valid);
	CHECK(result.errorMessage.find("999") != std::string::npos);
	CHECK(result.errorKind == ErrorKind::UnsupportedVersion);
}

TEST_CASE("PerformRestore overwrites the target directory and creates a safety backup", "[RestoreManager]")
{
	TempDirFixture fixture;
	const auto archivePath = BuildValidArchive(fixture.root / "backups", fixture.root / "source", "size=NEW\n");

	const auto targetDir = fixture.root / "obs-studio";
	WriteFile(targetDir / "global.ini", "size=OLD\n");
	WriteFile(targetDir / "basic" / "scenes" / "Record.json", R"({"scene":"old"})");

	const auto safetyDir = fixture.root / "obs-backuper-safety";

	std::vector<std::pair<std::size_t, std::size_t>> progressCalls;
	const auto outcome = RestoreManager::PerformRestore(
		MakeRestoreOptions(archivePath, targetDir, safetyDir),
		[&progressCalls](std::size_t current, std::size_t total, const std::filesystem::path &) {
			progressCalls.emplace_back(current, total);
		});

	REQUIRE(outcome.success);
	CHECK_FALSE(outcome.rolledBack);
	CHECK(outcome.errorMessage.empty());
	REQUIRE(std::filesystem::exists(outcome.safetyBackupPath));

	CHECK(ReadFile(targetDir / "global.ini") == "size=NEW\n");
	CHECK(ReadFile(targetDir / "basic" / "scenes" / "Record.json") == R"({"scene":"original"})");

	REQUIRE(progressCalls.size() == 2);
	CHECK(progressCalls[0] == std::make_pair(std::size_t{1}, std::size_t{2}));
	CHECK(progressCalls[1] == std::make_pair(std::size_t{2}, std::size_t{2}));

	// The safety backup captured the pre-restore ("OLD") state.
	const auto safetyValidation = RestoreManager::ValidateArchive(outcome.safetyBackupPath);
	REQUIRE(safetyValidation.valid);
}

TEST_CASE("PerformRestore fails cleanly on an invalid archive without touching the target directory", "[RestoreManager]")
{
	TempDirFixture fixture;
	const auto targetDir = fixture.root / "obs-studio";
	WriteFile(targetDir / "global.ini", "size=OLD\n");

	const auto outcome =
		RestoreManager::PerformRestore(MakeRestoreOptions(fixture.root / "missing.zip", targetDir,
								    fixture.root / "obs-backuper-safety"));

	CHECK_FALSE(outcome.success);
	CHECK_FALSE(outcome.rolledBack);
	CHECK_FALSE(outcome.errorMessage.empty());
	CHECK(outcome.errorKind == ErrorKind::InvalidArchive);
	CHECK(outcome.safetyBackupPath.empty());
	CHECK(ReadFile(targetDir / "global.ini") == "size=OLD\n");
}

TEST_CASE("PerformRestore rotates old safety backups beyond maxSafetyBackups", "[RestoreManager]")
{
	TempDirFixture fixture;
	const auto targetDir = fixture.root / "obs-studio";
	WriteFile(targetDir / "global.ini", "size=1\n");
	const auto safetyDir = fixture.root / "obs-backuper-safety";

	for (int i = 0; i < 3; ++i) {
		const auto archivePath = BuildValidArchive(fixture.root / "backups", fixture.root / "source");
		auto options = MakeRestoreOptions(archivePath, targetDir, safetyDir);
		options.maxSafetyBackups = 2;

		const auto outcome = RestoreManager::PerformRestore(options);
		REQUIRE(outcome.success);
	}

	std::size_t safetyBackupCount = 0;
	for (const auto &entry : std::filesystem::directory_iterator(safetyDir)) {
		if (entry.is_regular_file())
			++safetyBackupCount;
	}
	CHECK(safetyBackupCount == 2);
}

#ifndef _WIN32
TEST_CASE("PerformRestore rolls back to the pre-restore state when extraction fails partway through", "[RestoreManager]")
{
	TempDirFixture fixture;
	const auto archivePath = BuildValidArchive(fixture.root / "backups", fixture.root / "source", "size=NEW\n");

	const auto targetDir = fixture.root / "obs-studio";
	WriteFile(targetDir / "global.ini", "size=OLD\n");
	WriteFile(targetDir / "basic" / "scenes" / "Record.json", R"({"scene":"old"})");

	// Make Record.json read-only so overwriting it fails, simulating a
	// locked/inaccessible file mid-restore. (Removing write permission on the
	// parent directory instead would not do it -- overwriting the *content*
	// of an existing file only checks the file's own permission bits, not the
	// directory's.)
	const auto lockedFile = targetDir / "basic" / "scenes" / "Record.json";
	std::filesystem::permissions(lockedFile, std::filesystem::perms::owner_read, std::filesystem::perm_options::replace);

	const auto outcome =
		RestoreManager::PerformRestore(MakeRestoreOptions(archivePath, targetDir, fixture.root / "obs-backuper-safety"));

	std::filesystem::permissions(lockedFile, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);

	CHECK_FALSE(outcome.success);
	CHECK(outcome.rolledBack);
	CHECK_FALSE(outcome.errorMessage.empty());
	CHECK(outcome.errorKind == ErrorKind::RestoreExtractFailed);
	REQUIRE(std::filesystem::exists(outcome.safetyBackupPath));

	// global.ini (writable) was rolled back to its pre-restore content even
	// though it was already overwritten before the failure on Record.json.
	CHECK(ReadFile(targetDir / "global.ini") == "size=OLD\n");
}
#endif

TEST_CASE("PerformStagedRestore extracts into stagingDir and leaves targetDir untouched", "[RestoreManager][staged]")
{
	TempDirFixture fixture;
	const auto archivePath = BuildValidArchive(fixture.root / "backups", fixture.root / "source", "size=NEW\n");

	const auto targetDir = fixture.root / "obs-studio";
	WriteFile(targetDir / "global.ini", "size=OLD\n");
	WriteFile(targetDir / "basic" / "scenes" / "Record.json", R"({"scene":"old"})");

	const auto stagingDir = fixture.root / "staging";
	const auto safetyDir = fixture.root / "obs-backuper-safety";

	std::vector<std::pair<std::size_t, std::size_t>> progressCalls;
	const auto outcome = RestoreManager::PerformStagedRestore(
		MakeRestoreOptions(archivePath, targetDir, safetyDir), stagingDir,
		[&progressCalls](std::size_t current, std::size_t total, const std::filesystem::path &) {
			progressCalls.emplace_back(current, total);
		});

	REQUIRE(outcome.success);
	CHECK(outcome.errorMessage.empty());
	CHECK(outcome.stagingDir == stagingDir);
	REQUIRE(std::filesystem::exists(outcome.safetyBackupPath));
	REQUIRE(progressCalls.size() == 2);

	// targetDir is completely untouched -- still the pre-restore ("OLD") state.
	CHECK(ReadFile(targetDir / "global.ini") == "size=OLD\n");
	CHECK(ReadFile(targetDir / "basic" / "scenes" / "Record.json") == R"({"scene":"old"})");

	// stagingDir holds the restored ("NEW") content instead.
	CHECK(ReadFile(stagingDir / "global.ini") == "size=NEW\n");
	CHECK(ReadFile(stagingDir / "basic" / "scenes" / "Record.json") == R"({"scene":"original"})");

	// The safety backup still captured targetDir's pre-restore state.
	const auto safetyValidation = RestoreManager::ValidateArchive(outcome.safetyBackupPath);
	REQUIRE(safetyValidation.valid);
}

TEST_CASE("PerformStagedRestore fails cleanly on an invalid archive without creating a safety backup", "[RestoreManager][staged]")
{
	TempDirFixture fixture;
	const auto targetDir = fixture.root / "obs-studio";
	WriteFile(targetDir / "global.ini", "size=OLD\n");

	const auto outcome = RestoreManager::PerformStagedRestore(
		MakeRestoreOptions(fixture.root / "missing.zip", targetDir, fixture.root / "obs-backuper-safety"),
		fixture.root / "staging");

	CHECK_FALSE(outcome.success);
	CHECK_FALSE(outcome.errorMessage.empty());
	CHECK(outcome.errorKind == ErrorKind::InvalidArchive);
	CHECK(outcome.safetyBackupPath.empty());
	CHECK(ReadFile(targetDir / "global.ini") == "size=OLD\n");
}

TEST_CASE("CommitStagedRestore copies stagingDir onto targetDir and removes stagingDir", "[RestoreManager][staged]")
{
	TempDirFixture fixture;
	const auto stagingDir = fixture.root / "staging";
	WriteFile(stagingDir / "global.ini", "size=NEW\n");
	WriteFile(stagingDir / "basic" / "scenes" / "Record.json", R"({"scene":"new"})");

	const auto targetDir = fixture.root / "obs-studio";
	WriteFile(targetDir / "global.ini", "size=OLD\n");

	const auto commit = RestoreManager::CommitStagedRestore(stagingDir, targetDir, /*safetyBackupPath=*/{},
								 /*fileWriteMaxAttempts=*/2, /*fileWriteRetryDelayMs=*/1);

	REQUIRE(commit.success);
	CHECK_FALSE(commit.rolledBack);
	CHECK(commit.errorMessage.empty());

	CHECK(ReadFile(targetDir / "global.ini") == "size=NEW\n");
	CHECK(ReadFile(targetDir / "basic" / "scenes" / "Record.json") == R"({"scene":"new"})");
	CHECK_FALSE(std::filesystem::exists(stagingDir));
}

#ifndef _WIN32
TEST_CASE("CommitStagedRestore rolls back from the safety backup when a copy fails partway through",
	  "[RestoreManager][staged]")
{
	TempDirFixture fixture;
	const auto archivePath = BuildValidArchive(fixture.root / "backups", fixture.root / "safety-source", "size=OLD\n");

	const auto stagingDir = fixture.root / "staging";
	WriteFile(stagingDir / "global.ini", "size=NEW\n");
	WriteFile(stagingDir / "basic" / "scenes" / "Record.json", R"({"scene":"new"})");

	const auto targetDir = fixture.root / "obs-studio";
	WriteFile(targetDir / "global.ini", "size=OLD\n");
	const auto lockedFile = WriteFile(targetDir / "basic" / "scenes" / "Record.json", R"({"scene":"old"})");
	std::filesystem::permissions(lockedFile, std::filesystem::perms::owner_read, std::filesystem::perm_options::replace);

	const auto commit = RestoreManager::CommitStagedRestore(stagingDir, targetDir, archivePath,
								 /*fileWriteMaxAttempts=*/2, /*fileWriteRetryDelayMs=*/1);

	std::filesystem::permissions(lockedFile, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);

	CHECK_FALSE(commit.success);
	CHECK(commit.rolledBack);
	CHECK_FALSE(commit.errorMessage.empty());
	CHECK(commit.errorKind == ErrorKind::RestoreApplyFailed);
	CHECK_FALSE(std::filesystem::exists(stagingDir));

	// global.ini (writable, copied before Record.json failed) was rolled back
	// to the safety archive's content.
	CHECK(ReadFile(targetDir / "global.ini") == "size=OLD\n");
}
#endif

#ifndef _WIN32
TEST_CASE("CommitStagedRestore's rollback deletes files the restore added, and only those", "[RestoreManager][staged]")
{
	namespace fs = std::filesystem;
	TempDirFixture fixture;
	const auto safetyArchive = BuildValidArchive(fixture.root / "backups", fixture.root / "safety-source", "size=OLD\n");

	const auto stagingDir = fixture.root / "staging";
	WriteFile(stagingDir / "global.ini", "size=NEW\n");
	WriteFile(stagingDir / "basic" / "scenes" / "Record.json", R"({"scene":"new"})");
	WriteFile(stagingDir / "new-top.ini", "added\n");
	WriteFile(stagingDir / "new-dir" / "sub" / "a.json", "added\n");
	WriteFile(stagingDir / "basic" / "scenes" / "New Scene.json", "added\n");

	const auto targetDir = fixture.root / "obs-studio";
	WriteFile(targetDir / "global.ini", "size=OLD\n");
	WriteFile(targetDir / "basic" / "scenes" / "Record.json", R"({"scene":"old"})");
	WriteFile(targetDir / "logs" / "keep.log", "not part of any backup\n"); // never touched by the restore
	WriteFile(targetDir / "user-note.txt", "not part of any backup\n");

	// Make the LAST file of the run fail, whichever it is, so every earlier
	// file (including the added ones) has really been copied by then: an
	// existing file is made read-only, a new one is blocked by a directory.
	fs::path failingFile;
	fs::path lockedFile;
	const auto commit = RestoreManager::CommitStagedRestore(
		stagingDir, targetDir, safetyArchive, /*fileWriteMaxAttempts=*/1, /*fileWriteRetryDelayMs=*/0,
		[&](std::size_t current, std::size_t total, const fs::path &file) {
			if (current != total)
				return;
			failingFile = file;
			const auto full = targetDir / file;
			if (fs::exists(full)) {
				lockedFile = full;
				fs::permissions(full, fs::perms::owner_read, fs::perm_options::replace);
			} else {
				fs::create_directories(full);
			}
		});
	if (!lockedFile.empty())
		fs::permissions(lockedFile, fs::perms::owner_all, fs::perm_options::replace);

	REQUIRE_FALSE(commit.success);
	REQUIRE(commit.rolledBack);

	// Pre-existing files are back to the safety backup's content...
	CHECK(ReadFile(targetDir / "global.ini") == "size=OLD\n");
	CHECK(ReadFile(targetDir / "basic" / "scenes" / "Record.json") == R"({"scene":"original"})");
	// ...files the restore never touched are still there...
	CHECK(ReadFile(targetDir / "logs" / "keep.log") == "not part of any backup\n");
	CHECK(ReadFile(targetDir / "user-note.txt") == "not part of any backup\n");
	CHECK(fs::is_directory(targetDir / "basic" / "scenes"));

	// ...and the files it added are gone.
	CHECK_FALSE(fs::is_regular_file(targetDir / "new-top.ini"));
	CHECK_FALSE(fs::is_regular_file(targetDir / "new-dir" / "sub" / "a.json"));
	CHECK_FALSE(fs::is_regular_file(targetDir / "basic" / "scenes" / "New Scene.json"));

	// Directories emptied by the cleanup go too (unless the blocker itself
	// happens to be a directory placed in one of them).
	if (failingFile != fs::path("new-dir") / "sub" / "a.json")
		CHECK_FALSE(fs::exists(targetDir / "new-dir"));
}

TEST_CASE("Rollback keeps a file that is the safety backup's file under a differently-cased name",
	  "[RestoreManager][staged]")
{
	// On a case-insensitive filesystem (Windows, default macOS) the restored
	// "Basic/Scenes/record.json" IS the safety backup's "basic/scenes/Record.json"; deleting
	// it as "added by the restore" would destroy the file the rollback just put back. The
	// probe below only means something on such a filesystem; on a case-sensitive one the
	// differently-cased path is a separate file and simply gets removed.
	namespace fs = std::filesystem;
	TempDirFixture fixture;
	const auto safetyArchive = BuildValidArchive(fixture.root / "backups", fixture.root / "safety-source", "size=OLD\n");

	const auto targetDir = fixture.root / "obs-studio";
	WriteFile(targetDir / "global.ini", "size=OLD\n");
	WriteFile(targetDir / "basic" / "scenes" / "Record.json", R"({"scene":"old"})");
	const bool caseInsensitive = fs::exists(targetDir / "GLOBAL.INI");

	const auto stagingDir = fixture.root / "staging";
	WriteFile(stagingDir / "GLOBAL.INI", "size=NEW\n"); // same file as global.ini on a case-insensitive fs
	WriteFile(stagingDir / "basic" / "scenes" / "Record.json", R"({"scene":"new"})");

	const auto lockedFile = targetDir / "basic" / "scenes" / "Record.json";
	const auto commit = RestoreManager::CommitStagedRestore(
		stagingDir, targetDir, safetyArchive, 1, 0, [&](std::size_t current, std::size_t total, const fs::path &file) {
			// Fail on Record.json regardless of order: lock it up front.
			if (current == 1 || current == total)
				fs::permissions(lockedFile, fs::perms::owner_read, fs::perm_options::replace);
			(void)file;
		});
	fs::permissions(lockedFile, fs::perms::owner_all, fs::perm_options::replace);

	REQUIRE_FALSE(commit.success);
	REQUIRE(commit.rolledBack);
	CHECK(ReadFile(targetDir / "global.ini") == "size=OLD\n");
	if (caseInsensitive)
		CHECK(fs::is_regular_file(targetDir / "global.ini"));
}
#endif

TEST_CASE("WritePendingRestoreMarker/ReadPendingRestoreMarker round-trip", "[RestoreManager][marker]")
{
	TempDirFixture fixture;
	const auto markerPath = fixture.root / "nested" / "pending-restore.marker";

	PendingRestoreMarker marker;
	marker.stagingDir = fixture.root / "staging";
	marker.targetDir = fixture.root / "obs-studio";
	marker.safetyBackupPath = fixture.root / "obs-backuper-safety" / "obs-backup_before-restore_x.zip";
	marker.fileWriteMaxAttempts = 7;
	marker.fileWriteRetryDelayMs = 321;

	std::string error;
	REQUIRE(RestoreManager::WritePendingRestoreMarker(markerPath, marker, error));
	CHECK(error.empty());

	PendingRestoreMarker parsed;
	REQUIRE(RestoreManager::ReadPendingRestoreMarker(markerPath, parsed, error));
	CHECK(parsed.stagingDir == marker.stagingDir);
	CHECK(parsed.targetDir == marker.targetDir);
	CHECK(parsed.safetyBackupPath == marker.safetyBackupPath);
	CHECK(parsed.fileWriteMaxAttempts == 7);
	CHECK(parsed.fileWriteRetryDelayMs == 321);
}

TEST_CASE("ReadPendingRestoreMarker fails when no marker file exists", "[RestoreManager][marker]")
{
	TempDirFixture fixture;
	PendingRestoreMarker marker;
	std::string error;

	CHECK_FALSE(RestoreManager::ReadPendingRestoreMarker(fixture.root / "does-not-exist.marker", marker, error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("RemovePendingRestoreMarker deletes the marker file if present, and is a no-op otherwise",
	  "[RestoreManager][marker]")
{
	TempDirFixture fixture;
	const auto markerPath = fixture.root / "pending-restore.marker";
	WriteFile(markerPath, "stagingDir=/tmp/x\ntargetDir=/tmp/y\n");

	RestoreManager::RemovePendingRestoreMarker(markerPath);
	CHECK_FALSE(std::filesystem::exists(markerPath));

	// Calling it again with nothing there must not throw/crash.
	RestoreManager::RemovePendingRestoreMarker(markerPath);
}

// ---------------------------------------------------------------------------
// Installed plugins (plugins/<name>...) are executable code that OBS may have
// loaded already, so a restore must never overwrite them in place.
// ---------------------------------------------------------------------------

TEST_CASE("CommitStagedRestore installs new and changed plugin files", "[RestoreManager][plugins]")
{
	TempDirFixture fixture;
	const auto stagingDir = fixture.root / "staging";
	const auto targetDir = fixture.root / "obs-studio";

	WriteFile(stagingDir / "plugins" / "new.plugin" / "Contents" / "MacOS" / "new", "NEW-BINARY");
	WriteFile(stagingDir / "plugins" / "old.plugin" / "Contents" / "MacOS" / "old", "UPDATED-BINARY");
	WriteFile(targetDir / "plugins" / "old.plugin" / "Contents" / "MacOS" / "old", "OLD-BINARY");

	const auto commit = RestoreManager::CommitStagedRestore(stagingDir, targetDir, {}, 2, 1);

	REQUIRE(commit.success);
	CHECK(ReadFile(targetDir / "plugins" / "new.plugin" / "Contents" / "MacOS" / "new") == "NEW-BINARY");
	CHECK(ReadFile(targetDir / "plugins" / "old.plugin" / "Contents" / "MacOS" / "old") == "UPDATED-BINARY");

	// No temporary files are left behind next to the plugin.
	for (const auto &entry : std::filesystem::recursive_directory_iterator(targetDir / "plugins"))
		CHECK(entry.path().filename().string().find(".bakeup-") == std::string::npos);
}

TEST_CASE("CommitStagedRestore leaves an identical plugin file untouched", "[RestoreManager][plugins]")
{
	TempDirFixture fixture;
	const auto stagingDir = fixture.root / "staging";
	const auto targetDir = fixture.root / "obs-studio";

	WriteFile(stagingDir / "plugins" / "p.plugin" / "lib", "SAME-CONTENT");
	const auto existing = WriteFile(targetDir / "plugins" / "p.plugin" / "lib", "SAME-CONTENT");
	const auto longAgo = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24 * 365);
	std::filesystem::last_write_time(existing, longAgo);
	const auto before = std::filesystem::last_write_time(existing);

	const auto commit = RestoreManager::CommitStagedRestore(stagingDir, targetDir, {}, 2, 1);

	REQUIRE(commit.success);
	CHECK(ReadFile(existing) == "SAME-CONTENT");
	// Not rewritten: a loaded library must not be touched when nothing changed.
	// (Compared outside CHECK: Catch2 cannot print a file_time_type on libc++.)
	const bool timestampUnchanged = std::filesystem::last_write_time(existing) == before;
	CHECK(timestampUnchanged);
}

TEST_CASE("CommitStagedRestore never touches this plugin's own files", "[RestoreManager][plugins]")
{
	TempDirFixture fixture;
	const auto stagingDir = fixture.root / "staging";
	const auto targetDir = fixture.root / "obs-studio";

	// macOS bundle and Windows folder spellings.
	WriteFile(stagingDir / "plugins" / "obs-backuper.plugin" / "Contents" / "MacOS" / "obs-backuper", "OLDER-BUILD");
	WriteFile(stagingDir / "plugins" / "obs-backuper" / "bin" / "64bit" / "obs-backuper.dll", "OLDER-BUILD");
	WriteFile(stagingDir / "plugins" / "other.plugin" / "lib", "OTHER-NEW");
	const auto ownMac = WriteFile(targetDir / "plugins" / "obs-backuper.plugin" / "Contents" / "MacOS" / "obs-backuper",
				      "RUNNING-BUILD");
	const auto ownWin =
		WriteFile(targetDir / "plugins" / "obs-backuper" / "bin" / "64bit" / "obs-backuper.dll", "RUNNING-BUILD");

	const auto commit = RestoreManager::CommitStagedRestore(stagingDir, targetDir, {}, 2, 1);

	REQUIRE(commit.success);
	CHECK(ReadFile(ownMac) == "RUNNING-BUILD");
	CHECK(ReadFile(ownWin) == "RUNNING-BUILD");
	CHECK(ReadFile(targetDir / "plugins" / "other.plugin" / "lib") == "OTHER-NEW");
}

TEST_CASE("Only files inside plugins/ get plugin treatment", "[RestoreManager][plugins]")
{
	TempDirFixture fixture;
	const auto stagingDir = fixture.root / "staging";
	const auto targetDir = fixture.root / "obs-studio";

	// The plugin's own settings folder is not its "plugins/" folder: ordinary file.
	WriteFile(stagingDir / "plugin_config" / "obs-backuper" / "settings.json", "NEW");
	WriteFile(targetDir / "plugin_config" / "obs-backuper" / "settings.json", "OLD");

	const auto commit = RestoreManager::CommitStagedRestore(stagingDir, targetDir, {}, 2, 1);

	REQUIRE(commit.success);
	CHECK(ReadFile(targetDir / "plugin_config" / "obs-backuper" / "settings.json") == "NEW");
}

#ifndef _WIN32
TEST_CASE("Replacing a plugin file leaves a copy that is already open untouched", "[RestoreManager][plugins]")
{
	TempDirFixture fixture;
	const auto stagingDir = fixture.root / "staging";
	const auto targetDir = fixture.root / "obs-studio";

	WriteFile(stagingDir / "plugins" / "p.plugin" / "lib", "NEW-BINARY-CONTENT");
	const auto lib = WriteFile(targetDir / "plugins" / "p.plugin" / "lib", "OLD-BINARY-CONTENT");

	// Stands in for OBS having the library loaded (mapped): the restore must
	// not modify the file this handle refers to.
	std::ifstream loaded(lib, std::ios::binary);
	REQUIRE(loaded.is_open());

	const auto commit = RestoreManager::CommitStagedRestore(stagingDir, targetDir, {}, 2, 1);
	REQUIRE(commit.success);

	CHECK(ReadFile(lib) == "NEW-BINARY-CONTENT");
	const std::string stillOld((std::istreambuf_iterator<char>(loaded)), std::istreambuf_iterator<char>());
	CHECK(stillOld == "OLD-BINARY-CONTENT");
}

TEST_CASE("A failed restore puts plugin files back without leaving temporary files", "[RestoreManager][plugins]")
{
	TempDirFixture fixture;

	// The safety backup holds the plugin as it was before the restore.
	CollectionResult safety;
	const auto pluginFile = WriteFile(fixture.root / "before" / "plugins" / "p.plugin" / "lib", "ORIGINAL");
	const auto sceneFile = WriteFile(fixture.root / "before" / "basic" / "scenes" / "Record.json", R"({"scene":"old"})");
	safety.files = {
		{std::filesystem::path("plugins") / "p.plugin" / "lib", pluginFile, std::filesystem::file_size(pluginFile)},
		{std::filesystem::path("basic") / "scenes" / "Record.json", sceneFile, std::filesystem::file_size(sceneFile)},
	};
	safety.totalSizeBytes = safety.files[0].sizeBytes + safety.files[1].sizeBytes;
	BackupOptions backupOptions;
	backupOptions.destinationDir = fixture.root / "safety";
	backupOptions.pluginVersion = "1.0.0";
	backupOptions.obsVersion = "32.2.2";
	backupOptions.sourceOs = "macos";
	const auto safetyOutcome = BackupManager::CreateBackup(safety, backupOptions);
	REQUIRE(safetyOutcome.success);

	const auto stagingDir = fixture.root / "staging";
	WriteFile(stagingDir / "plugins" / "p.plugin" / "lib", "FROM-BACKUP");
	WriteFile(stagingDir / "basic" / "scenes" / "Record.json", R"({"scene":"new"})");

	const auto targetDir = fixture.root / "obs-studio";
	const auto lib = WriteFile(targetDir / "plugins" / "p.plugin" / "lib", "ORIGINAL");
	const auto lockedFile = WriteFile(targetDir / "basic" / "scenes" / "Record.json", R"({"scene":"old"})");
	std::filesystem::permissions(lockedFile, std::filesystem::perms::owner_read, std::filesystem::perm_options::replace);

	const auto commit = RestoreManager::CommitStagedRestore(stagingDir, targetDir, safetyOutcome.archivePath, 2, 1);

	std::filesystem::permissions(lockedFile, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);

	CHECK_FALSE(commit.success);
	CHECK(commit.rolledBack);
	CHECK(ReadFile(lib) == "ORIGINAL");
	for (const auto &entry : std::filesystem::recursive_directory_iterator(targetDir / "plugins"))
		CHECK(entry.path().filename().string().find(".bakeup-") == std::string::npos);
}
#endif

TEST_CASE("RemovePluginReplacementLeftovers deletes only the temporary plugin files", "[RestoreManager][plugins]")
{
	TempDirFixture fixture;
	const auto targetDir = fixture.root / "obs-studio";

	const auto keepLib = WriteFile(targetDir / "plugins" / "p" / "lib.dll", "keep");
	const auto keepOther = WriteFile(targetDir / "plugin_config" / "p" / "cfg.bakeup-old", "outside plugins/: keep");
	const auto oldFile = WriteFile(targetDir / "plugins" / "p" / "lib.dll.bakeup-old", "x");
	const auto newFile = WriteFile(targetDir / "plugins" / "p" / "bin" / "a.dll.bakeup-new", "x");
	const auto rollbackFile = WriteFile(targetDir / "plugins" / "q" / "b.so.bakeup-rollback", "x");

	RestoreManager::RemovePluginReplacementLeftovers(targetDir);

	CHECK(std::filesystem::exists(keepLib));
	CHECK(std::filesystem::exists(keepOther));
	CHECK_FALSE(std::filesystem::exists(oldFile));
	CHECK_FALSE(std::filesystem::exists(newFile));
	CHECK_FALSE(std::filesystem::exists(rollbackFile));
}

TEST_CASE("RemovePluginReplacementLeftovers is a no-op without a plugins folder", "[RestoreManager][plugins]")
{
	TempDirFixture fixture;
	RestoreManager::RemovePluginReplacementLeftovers(fixture.root / "does-not-exist");
	RestoreManager::RemovePluginReplacementLeftovers(fixture.root);
	SUCCEED();
}

TEST_CASE("Safety backups keep their sortable name and do not carry the OBS version", "[RestoreManager][plugins]")
{
	TempDirFixture fixture;
	const auto archivePath = BuildValidArchive(fixture.root / "backups", fixture.root / "source");
	const auto targetDir = fixture.root / "obs-studio";
	WriteFile(targetDir / "global.ini", "size=OLD\n");

	auto options = MakeRestoreOptions(archivePath, targetDir, fixture.root / "safety");
	const auto outcome = RestoreManager::PerformRestore(options);
	REQUIRE(outcome.success);

	const std::string name = outcome.safetyBackupPath.filename().string();
	CHECK(name.rfind("obs-backup_before-restore_", 0) == 0);
	CHECK(name.find("OBS-") == std::string::npos);
}

TEST_CASE("CommitStagedRestore counts the plugin files it actually wrote", "[RestoreManager][plugins]")
{
	TempDirFixture fixture;
	const auto stagingDir = fixture.root / "staging";
	const auto targetDir = fixture.root / "obs-studio";

	WriteFile(stagingDir / "plugins" / "new.plugin" / "lib", "NEW");
	WriteFile(stagingDir / "plugins" / "changed.plugin" / "lib", "AFTER");
	WriteFile(stagingDir / "plugins" / "same.plugin" / "lib", "SAME");
	WriteFile(stagingDir / "plugins" / "obs-backuper.plugin" / "lib", "OWN-OLDER");
	WriteFile(stagingDir / "global.ini", "size=1\n");
	WriteFile(targetDir / "plugins" / "changed.plugin" / "lib", "BEFORE");
	WriteFile(targetDir / "plugins" / "same.plugin" / "lib", "SAME");
	WriteFile(targetDir / "plugins" / "obs-backuper.plugin" / "lib", "OWN-RUNNING");

	const auto commit = RestoreManager::CommitStagedRestore(stagingDir, targetDir, {}, 2, 1);

	REQUIRE(commit.success);
	// added + replaced only: not the identical one, not this plugin's own, not a setting.
	CHECK(commit.pluginFilesChanged == 2);
}

TEST_CASE("A restore that touches no plugin files reports none", "[RestoreManager][plugins]")
{
	TempDirFixture fixture;
	const auto stagingDir = fixture.root / "staging";
	const auto targetDir = fixture.root / "obs-studio";
	WriteFile(stagingDir / "global.ini", "size=1\n");
	WriteFile(stagingDir / "plugins" / "same.plugin" / "lib", "SAME");
	WriteFile(targetDir / "plugins" / "same.plugin" / "lib", "SAME");

	const auto commit = RestoreManager::CommitStagedRestore(stagingDir, targetDir, {}, 2, 1);

	REQUIRE(commit.success);
	CHECK(commit.pluginFilesChanged == 0);
}

#ifndef _WIN32
TEST_CASE("A restored plugin keeps its execute permission", "[RestoreManager][plugins]")
{
	TempDirFixture fixture;
	const auto exec = std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
			  std::filesystem::perms::others_exec;

	// Source archive holds an executable plugin binary.
	CollectionResult collected;
	const auto binary = WriteFile(fixture.root / "src" / "plugins" / "p.plugin" / "Contents" / "MacOS" / "p", "BIN");
	std::filesystem::permissions(binary, static_cast<std::filesystem::perms>(0755),
				      std::filesystem::perm_options::replace);
	collected.files = {{std::filesystem::path("plugins") / "p.plugin" / "Contents" / "MacOS" / "p", binary,
			    std::filesystem::file_size(binary)}};
	collected.totalSizeBytes = collected.files[0].sizeBytes;
	BackupOptions backupOptions;
	backupOptions.destinationDir = fixture.root / "backups";
	backupOptions.pluginVersion = "1.0.0";
	backupOptions.obsVersion = "32.2.2";
	backupOptions.sourceOs = "macos";
	const auto backup = BackupManager::CreateBackup(collected, backupOptions);
	REQUIRE(backup.success);

	const auto targetDir = fixture.root / "obs-studio";
	const auto restored = targetDir / "plugins" / "p.plugin" / "Contents" / "MacOS" / "p";

	SECTION("a newly installed plugin")
	{
		auto options = MakeRestoreOptions(backup.archivePath, targetDir, fixture.root / "safety");
		const auto staging = fixture.root / "staging";
		const auto staged = RestoreManager::PerformStagedRestore(options, staging);
		REQUIRE(staged.success);
		REQUIRE(RestoreManager::CommitStagedRestore(staging, targetDir, staged.safetyBackupPath, 2, 1).success);

		CHECK(ReadFile(restored) == "BIN");
		CHECK((std::filesystem::status(restored).permissions() & exec) == exec);
	}

	SECTION("an identical file that lost its execute permission gets it back without being rewritten")
	{
		WriteFile(restored, "BIN");
		std::filesystem::permissions(restored, static_cast<std::filesystem::perms>(0644),
					      std::filesystem::perm_options::replace);

		auto options = MakeRestoreOptions(backup.archivePath, targetDir, fixture.root / "safety");
		const auto staging = fixture.root / "staging";
		const auto staged = RestoreManager::PerformStagedRestore(options, staging);
		REQUIRE(staged.success);
		const auto commit = RestoreManager::CommitStagedRestore(staging, targetDir, staged.safetyBackupPath, 2, 1);
		REQUIRE(commit.success);

		CHECK((std::filesystem::status(restored).permissions() & exec) == exec);
		CHECK(commit.pluginFilesChanged == 0);
	}
}
#endif
