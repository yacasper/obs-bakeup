// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "core/BackupManager.h"

#include <catch2/catch_test_macros.hpp>
#include <miniz.h>
#include <miniz_zip.h>

#include <fstream>
#include <regex>

using namespace obs_backuper;

namespace {

class TempDirFixture {
public:
	TempDirFixture() : root(std::filesystem::temp_directory_path() / "obs-backuper-backuptests" / UniqueName())
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

mz_uint CountZipEntries(const std::filesystem::path &zipPath)
{
	mz_zip_archive reader{};
	REQUIRE(mz_zip_reader_init_file(&reader, zipPath.string().c_str(), 0));
	const mz_uint count = mz_zip_reader_get_num_files(&reader);
	mz_zip_reader_end(&reader);
	return count;
}

std::string ReadZipEntry(const std::filesystem::path &zipPath, const std::string &entryName)
{
	mz_zip_archive reader{};
	REQUIRE(mz_zip_reader_init_file(&reader, zipPath.string().c_str(), 0));

	size_t size = 0;
	void *data = mz_zip_reader_extract_file_to_heap(&reader, entryName.c_str(), &size, 0);
	REQUIRE(data != nullptr);

	std::string result(static_cast<const char *>(data), size);
	mz_free(data);
	mz_zip_reader_end(&reader);
	return result;
}

BackupOptions MakeOptions(const std::filesystem::path &destinationDir)
{
	BackupOptions options;
	options.destinationDir = destinationDir;
	options.pluginVersion = "1.0.0";
	options.obsVersion = "30.2.0";
	options.sourceOs = "macos";
	options.sourceOsVersion = "14.0";
	return options;
}

} // namespace

TEST_CASE("GenerateBackupBaseFileName produces the expected pattern", "[BackupManager]")
{
	const std::string name = BackupManager::GenerateBackupBaseFileName(std::chrono::system_clock::now());

	static const std::regex pattern(R"(^obs-backup_\d{4}-\d{2}-\d{2}_\d{4}\.zip$)");
	CHECK(std::regex_match(name, pattern));
}

TEST_CASE("ResolveUniqueBackupPath returns the base name when nothing collides", "[BackupManager]")
{
	TempDirFixture fixture;

	const auto result = BackupManager::ResolveUniqueBackupPath(fixture.root, "obs-backup_2026-09-28_2230.zip");

	CHECK(result == fixture.root / "obs-backup_2026-09-28_2230.zip");
}

TEST_CASE("ResolveUniqueBackupPath increments a numeric suffix on collision", "[BackupManager]")
{
	TempDirFixture fixture;
	WriteFile(fixture.root / "obs-backup_2026-09-28_2230.zip", "existing");

	const auto firstCollision = BackupManager::ResolveUniqueBackupPath(fixture.root, "obs-backup_2026-09-28_2230.zip");
	CHECK(firstCollision == fixture.root / "obs-backup_2026-09-28_2230_1.zip");

	WriteFile(firstCollision, "existing too");
	const auto secondCollision =
		BackupManager::ResolveUniqueBackupPath(fixture.root, "obs-backup_2026-09-28_2230.zip");
	CHECK(secondCollision == fixture.root / "obs-backup_2026-09-28_2230_2.zip");
}

TEST_CASE("CreateBackup archives all collected files plus a manifest.json", "[BackupManager]")
{
	TempDirFixture fixture;
	const auto sourceDir = fixture.root / "source";
	const auto destDir = fixture.root / "dest";
	std::filesystem::create_directories(destDir);

	CollectionResult collected;
	const auto file1 = WriteFile(sourceDir / "global.ini", "size=1\n");
	const auto file2 = WriteFile(sourceDir / "basic" / "scenes" / "Record.json", "{}");
	collected.files = {
		{"global.ini", file1, std::filesystem::file_size(file1)},
		{std::filesystem::path("basic") / "scenes" / "Record.json", file2, std::filesystem::file_size(file2)},
	};
	collected.totalSizeBytes = collected.files[0].sizeBytes + collected.files[1].sizeBytes;

	std::vector<std::pair<std::size_t, std::size_t>> progressCalls;
	const auto outcome = BackupManager::CreateBackup(
		collected, MakeOptions(destDir),
		[&progressCalls](std::size_t current, std::size_t total, const std::filesystem::path &) {
			progressCalls.emplace_back(current, total);
		});

	REQUIRE(outcome.success);
	CHECK(outcome.warnings.empty());
	CHECK(outcome.errorMessage.empty());
	REQUIRE(std::filesystem::exists(outcome.archivePath));
	CHECK(outcome.archiveSizeBytes == std::filesystem::file_size(outcome.archivePath));

	CHECK(CountZipEntries(outcome.archivePath) == 3); // 2 files + manifest.json
	CHECK(ReadZipEntry(outcome.archivePath, "global.ini") == "size=1\n");
	CHECK(ReadZipEntry(outcome.archivePath, "basic/scenes/Record.json") == "{}");

	const std::string manifest = ReadZipEntry(outcome.archivePath, "manifest.json");
	CHECK(manifest.find("\"plugin_version\": \"1.0.0\"") != std::string::npos);
	CHECK(manifest.find("\"obs_version\": \"30.2.0\"") != std::string::npos);

	REQUIRE(progressCalls.size() == 2);
	CHECK(progressCalls[0] == std::make_pair(std::size_t{1}, std::size_t{2}));
	CHECK(progressCalls[1] == std::make_pair(std::size_t{2}, std::size_t{2}));
}

TEST_CASE("CreateBackup skips an unreadable file as a warning instead of aborting", "[BackupManager]")
{
	TempDirFixture fixture;
	const auto sourceDir = fixture.root / "source";
	const auto destDir = fixture.root / "dest";

	CollectionResult collected;
	const auto goodFile = WriteFile(sourceDir / "global.ini", "size=1\n");
	const auto missingFile = sourceDir / "missing.ini";
	collected.files = {
		{"global.ini", goodFile, std::filesystem::file_size(goodFile)},
		{"missing.ini", missingFile, 123},
	};
	collected.totalSizeBytes = collected.files[0].sizeBytes + collected.files[1].sizeBytes;

	const auto outcome = BackupManager::CreateBackup(collected, MakeOptions(destDir));

	REQUIRE(outcome.success);
	REQUIRE(outcome.warnings.size() == 1);
	CHECK(outcome.warnings[0].relativePath == "missing.ini");
	CHECK_FALSE(outcome.warnings[0].message.empty());

	CHECK(CountZipEntries(outcome.archivePath) == 2); // global.ini + manifest.json, missing.ini skipped
	CHECK(ReadZipEntry(outcome.archivePath, "global.ini") == "size=1\n");
}

TEST_CASE("CreateBackup fails cleanly when there is not enough free disk space", "[BackupManager]")
{
	TempDirFixture fixture;
	const auto sourceDir = fixture.root / "source";
	const auto destDir = fixture.root / "dest";

	CollectionResult collected;
	const auto file = WriteFile(sourceDir / "global.ini", "size=1\n");
	collected.files = {{"global.ini", file, std::filesystem::file_size(file)}};
	// An absurdly large requirement that cannot possibly fit on any disk.
	collected.totalSizeBytes = std::numeric_limits<std::uintmax_t>::max() / 2;

	const auto outcome = BackupManager::CreateBackup(collected, MakeOptions(destDir));

	CHECK_FALSE(outcome.success);
	CHECK(outcome.errorMessage.find("disk space") != std::string::npos);
	CHECK(outcome.errorKind == ErrorKind::NotEnoughDiskSpace);
	CHECK_FALSE(std::filesystem::exists(outcome.archivePath));
}

TEST_CASE("CreateBackup creates the destination directory if it does not exist yet", "[BackupManager]")
{
	TempDirFixture fixture;
	const auto sourceDir = fixture.root / "source";
	const auto destDir = fixture.root / "dest" / "nested" / "does-not-exist-yet";

	CollectionResult collected;
	const auto file = WriteFile(sourceDir / "global.ini", "size=1\n");
	collected.files = {{"global.ini", file, std::filesystem::file_size(file)}};
	collected.totalSizeBytes = collected.files[0].sizeBytes;

	const auto outcome = BackupManager::CreateBackup(collected, MakeOptions(destDir));

	REQUIRE(outcome.success);
	CHECK(outcome.errorKind == ErrorKind::None);
	CHECK(outcome.archivePath.parent_path() == destDir);
}

#ifndef _WIN32
TEST_CASE("CreateBackup fails cleanly when the destination directory is not writable", "[BackupManager]")
{
	TempDirFixture fixture;
	const auto sourceDir = fixture.root / "source";
	const auto destDir = fixture.root / "dest";
	std::filesystem::create_directories(destDir);
	std::filesystem::permissions(destDir, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
				      std::filesystem::perm_options::replace);

	CollectionResult collected;
	const auto file = WriteFile(sourceDir / "global.ini", "size=1\n");
	collected.files = {{"global.ini", file, std::filesystem::file_size(file)}};
	collected.totalSizeBytes = collected.files[0].sizeBytes;

	const auto outcome = BackupManager::CreateBackup(collected, MakeOptions(destDir));

	std::filesystem::permissions(destDir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);

	CHECK_FALSE(outcome.success);
	CHECK_FALSE(outcome.errorMessage.empty());
	CHECK(outcome.errorKind == ErrorKind::DestinationNotWritable);
}
#endif
