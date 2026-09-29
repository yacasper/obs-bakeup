// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include <catch2/catch_test_macros.hpp>

#include "core/RestoreResult.h"

#include <filesystem>
#include <fstream>

using namespace obs_backuper;
namespace fs = std::filesystem;

namespace {

struct TempDir {
	fs::path root = fs::temp_directory_path() / "obs-backuper-restoreresult-tests";
	TempDir()
	{
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

TEST_CASE("A stored success round-trips", "[RestoreResult]")
{
	TempDir dir;
	const auto path = dir.root / "nested" / "result.ini";

	REQUIRE(WriteRestoreResult(path, {true, false, ErrorKind::None, ""}));

	StoredRestoreResult read;
	read.errorMessage = "stale";
	REQUIRE(ReadRestoreResult(path, read));
	CHECK(read.success);
	CHECK_FALSE(read.rolledBack);
	CHECK(read.errorKind == ErrorKind::None);
	CHECK(read.errorMessage.empty());
}

TEST_CASE("A stored failure keeps its kind, rollback flag and multi-line message", "[RestoreResult]")
{
	TempDir dir;
	const auto path = dir.root / "result.ini";
	const std::string message = "failed to apply \"C:\\obs\\a.json\"\nsecond line\r\nthird = line";

	REQUIRE(WriteRestoreResult(path, {false, true, ErrorKind::RestoreApplyFailed, message}));

	StoredRestoreResult read;
	REQUIRE(ReadRestoreResult(path, read));
	CHECK_FALSE(read.success);
	CHECK(read.rolledBack);
	CHECK(read.errorKind == ErrorKind::RestoreApplyFailed);
	CHECK(read.errorMessage == message);
}

TEST_CASE("ReadRestoreResult rejects a missing or malformed file", "[RestoreResult]")
{
	TempDir dir;
	StoredRestoreResult read;
	CHECK_FALSE(ReadRestoreResult(dir.root / "missing.ini", read));

	std::ofstream(dir.root / "junk.ini") << "hello\nnot=a result\n";
	CHECK_FALSE(ReadRestoreResult(dir.root / "junk.ini", read));
}

TEST_CASE("RemoveRestoreResult deletes the file and tolerates a missing one", "[RestoreResult]")
{
	TempDir dir;
	const auto path = dir.root / "result.ini";
	REQUIRE(WriteRestoreResult(path, {true, false, ErrorKind::None, ""}));

	RemoveRestoreResult(path);
	CHECK_FALSE(fs::exists(path));
	RemoveRestoreResult(path);
}
