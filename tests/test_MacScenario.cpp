// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

// The macOS plugin locations, without OBS: the per-user plugins inside the
// obs-studio settings folder and the ones installed for all users.
#include <catch2/catch_test_macros.hpp>

#include "core/BackupManager.h"
#include "core/ConfigPaths.h"
#include "core/FileCollector.h"
#include "core/RestoreFlow.h"
#include "core/RestoreManager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace obs_backuper;
namespace fs = std::filesystem;

namespace {

void Write(const fs::path &path, const std::string &text)
{
	fs::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary) << text;
}

std::string Read(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	std::ostringstream buffer;
	buffer << in.rdbuf();
	return buffer.str();
}

struct MacFixture {
	fs::path root = fs::temp_directory_path() / "obs-backuper-mac-scenario";
	fs::path sourceConfig = root / "source" / "Users" / "me" / "Library" / "Application Support" / "obs-studio";
	fs::path sourceSystem = root / "source" / "Library" / "Application Support" / "obs-studio" / "plugins";
	fs::path targetConfig = root / "target" / "Users" / "me" / "Library" / "Application Support" / "obs-studio";
	fs::path targetSystem = root / "target" / "Library" / "Application Support" / "obs-studio" / "plugins";

	MacFixture()
	{
		fs::remove_all(root);
		Write(sourceConfig / "user.ini", "[Basic]\nProfile=Stream\nSceneCollection=Live\n");
		Write(sourceConfig / "basic" / "scenes" / "Live.json", "{\"scenes\":1}");
		Write(sourceConfig / "plugins" / "user-plugin.plugin" / "Contents" / "MacOS" / "user-plugin", "USER-BINARY");
		Write(sourceSystem / "all-users.plugin" / "Contents" / "MacOS" / "all-users", "SYSTEM-BINARY");
		Write(sourceSystem / "all-users.plugin" / "Contents" / "Resources" / "locale" / "en-US.ini", "a=1");
		Write(sourceSystem / ".DS_Store", "junk");
		Write(sourceSystem / "obs-bakeup.plugin" / "Contents" / "MacOS" / "obs-bakeup", "SOURCE-OWN");
	}
	~MacFixture()
	{
		std::error_code ec;
		fs::remove_all(root, ec);
	}
};

} // namespace

TEST_CASE("The macOS system plugin folder is a plugin root of its own", "[MacScenario]")
{
	const auto roots = ResolveMacPluginRoots(fs::path("/Library/Application Support/obs-studio/plugins"));

	REQUIRE(roots.size() == 1);
	CHECK(roots[0].archivePrefix == "mac-system-plugins");
	CHECK(roots[0].dir == fs::path("/Library/Application Support/obs-studio/plugins"));
	CHECK(roots[0].excludedStems.empty());
	CHECK(IsPluginRootEntry(fs::path("mac-system-plugins") / "x.plugin" / "Contents" / "Info.plist"));

	// It must not share a prefix with the Windows folders, or a backup from one
	// system would be restored into the folder of the other.
	CHECK(std::string(kMacSystemPluginsPrefix) != kSystemPluginsPrefix);
	CHECK(std::string(kMacSystemPluginsPrefix) != kPortablePluginsPrefix);
}

TEST_CASE("No macOS system plugin folder gives no root", "[MacScenario]")
{
	CHECK(ResolveMacPluginRoots(std::nullopt).empty());
	CHECK(ResolveMacPluginRoots(fs::path()).empty());
}

TEST_CASE("Both macOS plugin locations go into a backup", "[MacScenario]")
{
	MacFixture fixture;
	auto collected = CollectFiles(fixture.sourceConfig);
	for (const auto &root : ResolveMacPluginRoots(fixture.sourceSystem))
		CollectPluginRoot(root, collected);

	const auto has = [&](const fs::path &p) {
		return std::any_of(collected.files.begin(), collected.files.end(),
				   [&](const auto &f) { return f.relativePath == p; });
	};

	CHECK(has(fs::path("plugins") / "user-plugin.plugin" / "Contents" / "MacOS" / "user-plugin"));
	CHECK(has(fs::path("mac-system-plugins") / "all-users.plugin" / "Contents" / "MacOS" / "all-users"));
	CHECK(has(fs::path("mac-system-plugins") / "all-users.plugin" / "Contents" / "Resources" / "locale" / "en-US.ini"));
	CHECK_FALSE(has(fs::path("mac-system-plugins") / ".DS_Store"));
}

TEST_CASE("A macOS backup restores both plugin locations and leaves this plugin alone", "[MacScenario]")
{
	MacFixture fixture;
	auto collected = CollectFiles(fixture.sourceConfig);
	BackupOptions backupOptions;
	backupOptions.destinationDir = fixture.root / "backups";
	backupOptions.pluginVersion = "0.1.0";
	backupOptions.obsVersion = "32.2.2";
	backupOptions.sourceOs = "macos";
	const auto roots = ResolveMacPluginRoots(fixture.sourceSystem);
	for (const auto &root : roots) {
		CollectPluginRoot(root, collected);
		backupOptions.extraIncludedSections.push_back(root.archivePrefix);
	}
	const auto backup = BackupManager::CreateBackup(collected, backupOptions);
	REQUIRE(backup.success);

	// The target already runs its own copy of this plugin.
	Write(fixture.targetSystem / "obs-bakeup.plugin" / "Contents" / "MacOS" / "obs-bakeup", "TARGET-OWN");
	Write(fixture.targetConfig / "user.ini", "[Basic]\nProfile=Untitled\n");

	for (int pass = 1; pass <= 2; ++pass) {
		INFO("restore pass " << pass);
		RestoreOptions options;
		options.archivePath = backup.archivePath;
		options.targetDir = fixture.targetConfig;
		options.safetyBackupDir = fixture.targetConfig.parent_path() / "obs-backuper-safety";
		options.pluginRoots = ResolveMacPluginRoots(fixture.targetSystem);
		options.pluginVersion = "0.1.0";
		options.obsVersion = "32.2.2";
		options.sourceOs = "macos";
		options.fileWriteMaxAttempts = 2;
		options.fileWriteRetryDelayMs = 1;

		const auto staged = RestoreManager::PerformStagedRestore(options, options.safetyBackupDir / "staging");
		REQUIRE(staged.success);
		CHECK(staged.pluginFilesFailed == 0);

		PendingRestoreMarker marker;
		marker.stagingDir = staged.stagingDir;
		marker.targetDir = fixture.targetConfig;
		marker.safetyBackupPath = staged.safetyBackupPath;
		marker.fileWriteMaxAttempts = 2;
		marker.fileWriteRetryDelayMs = 1;
		std::string error;
		REQUIRE(RestoreManager::WritePendingRestoreMarker(PendingRestoreMarkerPath(options.safetyBackupDir), marker,
								   error));
		const auto applied = ApplyPendingRestoreAndStoreResult(options.safetyBackupDir, fixture.targetConfig);
		REQUIRE(applied.wasPending);
		CHECK(applied.commit.success);

		CHECK(Read(fixture.targetConfig / "user.ini") == "[Basic]\nProfile=Stream\nSceneCollection=Live\n");
		CHECK(Read(fixture.targetConfig / "basic" / "scenes" / "Live.json") == "{\"scenes\":1}");
		CHECK(Read(fixture.targetConfig / "plugins" / "user-plugin.plugin" / "Contents" / "MacOS" / "user-plugin") ==
		      "USER-BINARY");
		CHECK(Read(fixture.targetSystem / "all-users.plugin" / "Contents" / "MacOS" / "all-users") == "SYSTEM-BINARY");
		CHECK(Read(fixture.targetSystem / "all-users.plugin" / "Contents" / "Resources" / "locale" / "en-US.ini") ==
		      "a=1");
		CHECK(Read(fixture.targetSystem / "obs-bakeup.plugin" / "Contents" / "MacOS" / "obs-bakeup") == "TARGET-OWN");
		CHECK_FALSE(fs::exists(fixture.targetConfig / "mac-system-plugins"));
	}
}

TEST_CASE("A macOS backup restored on a machine without a system plugin folder skips it", "[MacScenario]")
{
	MacFixture fixture;
	auto collected = CollectFiles(fixture.sourceConfig);
	BackupOptions backupOptions;
	backupOptions.destinationDir = fixture.root / "backups";
	backupOptions.sourceOs = "macos";
	for (const auto &root : ResolveMacPluginRoots(fixture.sourceSystem)) {
		CollectPluginRoot(root, collected);
		backupOptions.extraIncludedSections.push_back(root.archivePrefix);
	}
	const auto backup = BackupManager::CreateBackup(collected, backupOptions);
	REQUIRE(backup.success);

	RestoreOptions options;
	options.archivePath = backup.archivePath;
	options.targetDir = fixture.targetConfig;
	options.safetyBackupDir = fixture.targetConfig.parent_path() / "obs-backuper-safety";
	// No plugin roots here, as on a Windows machine.
	options.fileWriteMaxAttempts = 2;
	options.fileWriteRetryDelayMs = 1;
	const auto staged = RestoreManager::PerformStagedRestore(options, options.safetyBackupDir / "staging");

	REQUIRE(staged.success);
	CHECK_FALSE(fs::exists(fixture.targetSystem));
	CHECK_FALSE(fs::exists(fixture.targetConfig / "mac-system-plugins"));
	CHECK_FALSE(fs::exists(options.safetyBackupDir / "staging" / "mac-system-plugins"));
}

#ifndef _WIN32
TEST_CASE("Executable files in the macOS system plugin folder keep their execute permission", "[MacScenario]")
{
	MacFixture fixture;
	const auto binary = fixture.sourceSystem / "all-users.plugin" / "Contents" / "MacOS" / "all-users";
	fs::permissions(binary, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
				       fs::perms::others_read | fs::perms::others_exec,
			fs::perm_options::replace);

	auto collected = CollectFiles(fixture.sourceConfig);
	BackupOptions backupOptions;
	backupOptions.destinationDir = fixture.root / "backups";
	backupOptions.sourceOs = "macos";
	for (const auto &root : ResolveMacPluginRoots(fixture.sourceSystem)) {
		CollectPluginRoot(root, collected);
		backupOptions.extraIncludedSections.push_back(root.archivePrefix);
	}
	const auto backup = BackupManager::CreateBackup(collected, backupOptions);
	REQUIRE(backup.success);

	RestoreOptions options;
	options.archivePath = backup.archivePath;
	options.targetDir = fixture.targetConfig;
	options.safetyBackupDir = fixture.targetConfig.parent_path() / "obs-backuper-safety";
	options.pluginRoots = ResolveMacPluginRoots(fixture.targetSystem);
	options.fileWriteMaxAttempts = 2;
	options.fileWriteRetryDelayMs = 1;
	REQUIRE(RestoreManager::PerformStagedRestore(options, options.safetyBackupDir / "staging").success);

	const auto restored = fixture.targetSystem / "all-users.plugin" / "Contents" / "MacOS" / "all-users";
	REQUIRE(fs::exists(restored));
	const auto perms = fs::status(restored).permissions();
	CHECK((perms & fs::perms::owner_exec) != fs::perms::none);
	CHECK((perms & fs::perms::group_exec) != fs::perms::none);
}
#endif
