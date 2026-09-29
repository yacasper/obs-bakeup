// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include <catch2/catch_test_macros.hpp>

#include "core/RestoreFlow.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace obs_backuper;
namespace fs = std::filesystem;

namespace {

struct FlowFixture {
	fs::path root = fs::temp_directory_path() / "obs-backuper-restoreflow-tests";
	fs::path safetyDir = root / "obs-backuper-safety";
	fs::path obsDir = root / "obs-studio";
	fs::path stagingDir = safetyDir / "staging";

	FlowFixture()
	{
		fs::remove_all(root);
		fs::create_directories(safetyDir);
		fs::create_directories(obsDir);
	}
	~FlowFixture()
	{
		std::error_code ec;
		fs::remove_all(root, ec);
	}

	static fs::path Write(const fs::path &path, const std::string &text)
	{
		fs::create_directories(path.parent_path());
		std::ofstream(path, std::ios::binary) << text;
		return path;
	}
	static std::string Read(const fs::path &path)
	{
		std::ifstream in(path, std::ios::binary);
		std::ostringstream buffer;
		buffer << in.rdbuf();
		return buffer.str();
	}

	// Stages a restore the way the dialog does: files in stagingDir plus a marker.
	void Stage()
	{
		PendingRestoreMarker marker;
		marker.stagingDir = stagingDir;
		marker.targetDir = obsDir;
		marker.fileWriteMaxAttempts = 2;
		marker.fileWriteRetryDelayMs = 1;
		std::string error;
		REQUIRE(RestoreManager::WritePendingRestoreMarker(PendingRestoreMarkerPath(safetyDir), marker, error));
	}
};

} // namespace

TEST_CASE("The restore flow keeps its files in the safety-backup folder", "[RestoreFlow]")
{
	CHECK(PendingRestoreMarkerPath("/x/safety") == fs::path("/x/safety") / "pending-restore.marker");
	CHECK(StoredRestoreResultPath("/x/safety") == fs::path("/x/safety") / "restore-result.ini");
}

TEST_CASE("ApplyPendingRestore does nothing when no restore is pending", "[RestoreFlow]")
{
	FlowFixture fixture;
	FlowFixture::Write(fixture.obsDir / "user.ini", "[Basic]\nProfile=Keep\n");

	const auto applied = ApplyPendingRestore(fixture.safetyDir, fixture.obsDir);

	CHECK_FALSE(applied.wasPending);
	CHECK(FlowFixture::Read(fixture.obsDir / "user.ini") == "[Basic]\nProfile=Keep\n");
	CHECK_FALSE(fs::exists(StoredRestoreResultPath(fixture.safetyDir)));
}

TEST_CASE("ApplyPendingRestore commits the staged files and removes the marker", "[RestoreFlow]")
{
	FlowFixture fixture;
	FlowFixture::Write(fixture.obsDir / "user.ini", "[Basic]\nProfile=Old\n");
	FlowFixture::Write(fixture.stagingDir / "user.ini", "[Basic]\nProfile=Stream\n");
	FlowFixture::Write(fixture.stagingDir / "basic" / "profiles" / "Stream" / "basic.ini", "[Video]\nBaseCX=2560\n");
	fixture.Stage();

	const auto applied = ApplyPendingRestore(fixture.safetyDir, fixture.obsDir);

	REQUIRE(applied.wasPending);
	CHECK(applied.commit.success);
	CHECK(applied.marker.targetDir == fixture.obsDir);
	CHECK(FlowFixture::Read(fixture.obsDir / "user.ini") == "[Basic]\nProfile=Stream\n");
	CHECK(FlowFixture::Read(fixture.obsDir / "basic" / "profiles" / "Stream" / "basic.ini") == "[Video]\nBaseCX=2560\n");
	CHECK_FALSE(fs::exists(PendingRestoreMarkerPath(fixture.safetyDir)));
	CHECK_FALSE(fs::exists(fixture.stagingDir));
	// The plain variant leaves no stored result.
	CHECK_FALSE(fs::exists(StoredRestoreResultPath(fixture.safetyDir)));
}

TEST_CASE("The shutdown path stores the outcome, and the next start takes it exactly once", "[RestoreFlow]")
{
	FlowFixture fixture;
	FlowFixture::Write(fixture.stagingDir / "global.ini", "[General]\nA=1\n");
	fixture.Stage();

	const auto applied = ApplyPendingRestoreAndStoreResult(fixture.safetyDir, fixture.obsDir);
	REQUIRE(applied.wasPending);
	REQUIRE(applied.commit.success);
	CHECK(fs::exists(StoredRestoreResultPath(fixture.safetyDir)));

	StoredRestoreResult stored;
	REQUIRE(TakeStoredRestoreResult(fixture.safetyDir, stored));
	CHECK(stored.success);
	CHECK_FALSE(stored.rolledBack);
	CHECK(stored.errorKind == ErrorKind::None);

	CHECK_FALSE(fs::exists(StoredRestoreResultPath(fixture.safetyDir)));
	StoredRestoreResult again;
	CHECK_FALSE(TakeStoredRestoreResult(fixture.safetyDir, again));
}

TEST_CASE("The shutdown path stores nothing when nothing was pending", "[RestoreFlow]")
{
	FlowFixture fixture;

	const auto applied = ApplyPendingRestoreAndStoreResult(fixture.safetyDir, fixture.obsDir);

	CHECK_FALSE(applied.wasPending);
	CHECK_FALSE(fs::exists(StoredRestoreResultPath(fixture.safetyDir)));
}

TEST_CASE("TakeStoredRestoreResult ignores a malformed result file", "[RestoreFlow]")
{
	FlowFixture fixture;
	FlowFixture::Write(StoredRestoreResultPath(fixture.safetyDir), "garbage\n");

	StoredRestoreResult stored;
	CHECK_FALSE(TakeStoredRestoreResult(fixture.safetyDir, stored));
}

TEST_CASE("A restore staged with non-ASCII folder names is applied at shutdown", "[RestoreFlow]")
{
	FlowFixture fixture;
	const std::string name = "\xD0\xBF\xD1\x80\xD0\xBE\xD1\x84\xD0\xB8\xD0\xBB\xD1\x8C"; // Cyrillic "profile"
	const fs::path profileDir = fs::path("basic") / "profiles" / std::filesystem::u8path(name);
	FlowFixture::Write(fixture.stagingDir / profileDir / "basic.ini", "[Video]\nBaseCX=2560\n");
	fixture.Stage();

	const auto applied = ApplyPendingRestoreAndStoreResult(fixture.safetyDir, fixture.obsDir);

	REQUIRE(applied.wasPending);
	CHECK(applied.commit.success);
	CHECK(FlowFixture::Read(fixture.obsDir / profileDir / "basic.ini") == "[Video]\nBaseCX=2560\n");
}

#ifndef _WIN32
TEST_CASE("A failed restore is stored as a failure and the marker is still removed", "[RestoreFlow]")
{
	FlowFixture fixture;
	FlowFixture::Write(fixture.stagingDir / "basic" / "scenes" / "Live.json", "NEW");
	const auto locked = FlowFixture::Write(fixture.obsDir / "basic" / "scenes" / "Live.json", "OLD");
	fs::permissions(locked, fs::perms::owner_read, fs::perm_options::replace);
	fixture.Stage();

	const auto applied = ApplyPendingRestoreAndStoreResult(fixture.safetyDir, fixture.obsDir);

	fs::permissions(locked, fs::perms::owner_all, fs::perm_options::replace);

	REQUIRE(applied.wasPending);
	CHECK_FALSE(applied.commit.success);
	CHECK_FALSE(fs::exists(PendingRestoreMarkerPath(fixture.safetyDir)));

	StoredRestoreResult stored;
	REQUIRE(TakeStoredRestoreResult(fixture.safetyDir, stored));
	CHECK_FALSE(stored.success);
	CHECK(stored.errorKind == ErrorKind::RestoreApplyFailed);
	CHECK(stored.errorMessage.find("Live.json") != std::string::npos);
}
#endif
