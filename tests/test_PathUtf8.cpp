// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include <catch2/catch_test_macros.hpp>

#include "core/BackupManager.h"
#include "core/PathUtf8.h"
#include "core/RestoreManager.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace obs_backuper;
namespace fs = std::filesystem;

namespace {

// "Привет" / "профиль" -- names a Western Windows code page cannot represent,
// which used to make path::string() throw.
const std::string kCyrillicFile = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82.txt";
const std::string kCyrillicDir = "\xD0\xBF\xD1\x80\xD0\xBE\xD1\x84\xD0\xB8\xD0\xBB\xD1\x8C";

struct TempDir {
	fs::path root;
	TempDir()
	{
		root = fs::temp_directory_path() / PathFromUtf8("obs-backuper-pathutf8-" + kCyrillicDir);
		fs::remove_all(root);
		fs::create_directories(root);
	}
	~TempDir()
	{
		std::error_code ec;
		fs::remove_all(root, ec);
	}
};

} // namespace

TEST_CASE("PathToUtf8 and PathFromUtf8 round-trip non-ASCII names", "[PathUtf8]")
{
	const fs::path path = PathFromUtf8(kCyrillicDir + "/" + kCyrillicFile);

	CHECK(GenericPathToUtf8(path) == kCyrillicDir + "/" + kCyrillicFile);
	CHECK(PathToUtf8(path.filename()) == kCyrillicFile);
	CHECK(PathFromUtf8(PathToUtf8(path)) == path);
}

TEST_CASE("PathToUtf8 leaves ASCII names alone and handles an empty path", "[PathUtf8]")
{
	CHECK(GenericPathToUtf8(fs::path("a") / "b" / "c.ini") == "a/b/c.ini");
	CHECK(PathToUtf8(fs::path()).empty());
	CHECK(PathFromUtf8("").empty());
}

TEST_CASE("A pending-restore marker keeps non-ASCII folder names intact", "[PathUtf8][RestoreManager]")
{
	TempDir dir;
	PendingRestoreMarker marker;
	marker.stagingDir = dir.root / PathFromUtf8(kCyrillicDir) / "staging";
	marker.targetDir = dir.root / PathFromUtf8(kCyrillicDir) / PathFromUtf8(kCyrillicFile);
	marker.safetyBackupPath = dir.root / "safety.zip";
	const fs::path markerPath = dir.root / "pending-restore.marker";

	std::string error;
	REQUIRE(RestoreManager::WritePendingRestoreMarker(markerPath, marker, error));

	PendingRestoreMarker read;
	REQUIRE(RestoreManager::ReadPendingRestoreMarker(markerPath, read, error));
	CHECK(read.stagingDir == marker.stagingDir);
	CHECK(read.targetDir == marker.targetDir);
	CHECK(read.safetyBackupPath == marker.safetyBackupPath);
}

TEST_CASE("A backup to a folder with a non-ASCII name and file names works", "[PathUtf8][BackupManager]")
{
	TempDir dir;
	const fs::path source = dir.root / "source";
	fs::create_directories(source / PathFromUtf8(kCyrillicDir));
	std::ofstream(source / PathFromUtf8(kCyrillicDir) / PathFromUtf8(kCyrillicFile)) << "data";

	const fs::path destination = dir.root / PathFromUtf8(kCyrillicDir);
	fs::create_directories(destination);
	const fs::path first = BackupManager::ResolveUniqueBackupPath(destination, "obs-backup_x.zip");
	std::ofstream(first) << "taken";

	// The collision suffix must be added without going through a narrow string.
	const fs::path second = BackupManager::ResolveUniqueBackupPath(destination, "obs-backup_x.zip");
	CHECK(second != first);
	CHECK(PathToUtf8(second.filename()) == "obs-backup_x_1.zip");
}
