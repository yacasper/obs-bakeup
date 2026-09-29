// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

// The Windows workflow that was verified by hand, end to end and without OBS:
// back up a normal install, restore it into a fresh portable one, twice, as OBS
// shutting down would. Paths use the Windows layout under a temporary folder.
#include <catch2/catch_test_macros.hpp>

#include "core/ActiveSelection.h"
#include "core/BackupManager.h"
#include "core/ConfigPaths.h"
#include "core/FileCollector.h"
#include "core/PathUtf8.h"
#include "core/RestoreFlow.h"
#include "core/RestoreManager.h"
#include "core/SceneCollectionSnapshot.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace obs_backuper;
namespace fs = std::filesystem;

namespace {

// "Сute_Cats_Animated" (Cyrillic "С") and "профиль" -- names a Western Windows
// code page cannot hold.
const std::string kCyrillicCollection = "\xD0\xA1ute_Cats_Animated";
const std::string kCyrillicProfile = "\xD0\xBF\xD1\x80\xD0\xBE\xD1\x84\xD0\xB8\xD0\xBB\xD1\x8C";

const std::string kSceneJson = "{\"resolution\":{\"x\":2560,\"y\":1440},\"crop_right\":1098}";
const std::string kUserIni = "[General]\nInstallGUID=SOURCE\n\n[Basic]\nProfile=Stream\nSceneCollection=StreamSceneCollection\n\n"
			     "[BasicWindow]\nExtraBrowserDocks=[{\"title\":\"Chat\",\"url\":\"http://x/?a=b\"}]\nDockState=ABC\n";

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

bool ArchiveHas(const CollectionResult &collected, const fs::path &relativePath)
{
	return std::any_of(collected.files.begin(), collected.files.end(),
			   [&](const auto &f) { return f.relativePath == relativePath; });
}

struct Machine {
	fs::path root;
	fs::path obsBase() const { return root / "obs-studio-install"; }   // folder holding bin, obs-plugins, data
	fs::path config() const { return root / "AppData" / "obs-studio"; } // %APPDATA%\obs-studio or <base>\config\obs-studio
	fs::path programData() const { return root / "ProgramData" / "obs-studio" / "plugins"; }
	fs::path safety() const { return config().parent_path() / "obs-backuper-safety"; }
};

struct ScenarioFixture {
	fs::path root = fs::temp_directory_path() / "obs-backuper-windows-scenario";
	Machine source;
	Machine target;

	ScenarioFixture()
	{
		fs::remove_all(root);
		source.root = root / "source";
		target.root = root / "target";
		fs::create_directories(source.root);
		fs::create_directories(target.root);
		BuildSourceInstall();
	}
	~ScenarioFixture()
	{
		std::error_code ec;
		fs::remove_all(root, ec);
	}

	void BuildSourceInstall()
	{
		const auto cfg = source.config();
		Write(cfg / "user.ini", kUserIni);
		Write(cfg / "global.ini", "[General]\nInstallGUID=SOURCE\n");
		Write(cfg / "basic" / "profiles" / "Stream" / "basic.ini", "[Video]\nBaseCX=2560\nBaseCY=1440\n");
		Write(cfg / "basic" / "profiles" / "Stream" / "service.json", "{\"type\":\"rtmp_common\"}");
		Write(cfg / "basic" / "profiles" / fs::u8path(kCyrillicProfile) / "basic.ini", "[Video]\nBaseCX=1920\n");
		Write(cfg / "basic" / "scenes" / "StreamSceneCollection.json", kSceneJson);
		Write(cfg / "basic" / "scenes" / "StreamSceneCollection.json.bak", "old");
		Write(cfg / "basic" / "scenes" / fs::u8path(kCyrillicCollection + ".json"), "{\"cyrillic\":true}");
		Write(cfg / "plugin_config" / "aitum-multistream" / "config.json", "{\"aitum\":1}");
		Write(cfg / "plugin_config" / "obs-browser" / "Cookies", "LOGINS");
		Write(cfg / "plugin_config" / "obs-browser" / "Cache" / "Cache_Data" / "f_000001", "CACHE");
		Write(cfg / "plugin_config" / "obs-browser" / "first_party_sets.db", "LOCKED-DB");
		Write(cfg / "logs" / "2026-01-01.txt", "log");

		Write(source.programData() / "obs-multi-rtmp" / "bin" / "64bit" / "obs-multi-rtmp.dll", "MULTI");
		Write(source.obsBase() / "obs-plugins" / "64bit" / "aitum-multistream.dll", "AITUM");
		Write(source.obsBase() / "obs-plugins" / "64bit" / "obs-ffmpeg.dll", "OBS-OWN-FFMPEG");
		Write(source.obsBase() / "obs-plugins" / "64bit" / "libcef.dll", "OBS-OWN-CEF");
		Write(source.obsBase() / "obs-plugins" / "64bit" / "obs-bakeup.dll", "RUNNING-PLUGIN");
		Write(source.obsBase() / "data" / "obs-plugins" / "aitum-multistream" / "locale" / "en-US.ini", "aitum=1");
		Write(source.obsBase() / "data" / "obs-plugins" / "obs-ffmpeg" / "x.ini", "OBS-OWN");
		Write(source.obsBase() / "data" / "obs-plugins" / "obs-bakeup" / "locale" / "en-US.ini", "RUNNING-LOCALE");
	}

	BackupOutcome Backup(const std::string &password)
	{
		auto collected = CollectFiles(source.config());
		const auto roots = ResolveWindowsPluginRoots(source.obsBase(), /*portableMode=*/false, source.programData());
		BackupOptions options;
		options.destinationDir = root / "backups";
		options.pluginVersion = "0.1.0";
		options.obsVersion = "32.2.2";
		options.sourceOs = "windows";
		options.password = password;
		options.encryptionParams.kdf = {64, 1, 1};
		options.encryptionParams.chunkSize = 4096;
		for (const auto &pluginRoot : roots) {
			CollectPluginRoot(pluginRoot, collected);
			options.extraIncludedSections.push_back(pluginRoot.archivePrefix);
		}
		lastCollected = collected;
		return BackupManager::CreateBackup(collected, options);
	}

	// Stages the restore while "OBS is running", then applies it as OBS shuts down.
	AppliedRestore RestoreInto(const Machine &machine, const fs::path &archive, const std::string &password,
				   bool portable)
	{
		RestoreOptions options;
		options.archivePath = archive;
		options.targetDir = machine.config();
		options.safetyBackupDir = machine.safety();
		options.pluginRoots = ResolveWindowsPluginRoots(machine.obsBase(), portable, machine.programData());
		options.pluginVersion = "0.1.0";
		options.obsVersion = "32.2.2";
		options.sourceOs = "windows";
		options.password = password;
		options.fileWriteMaxAttempts = 2;
		options.fileWriteRetryDelayMs = 1;

		const auto staged = RestoreManager::PerformStagedRestore(options, machine.safety() / "staging");
		REQUIRE(staged.success);
		lastStaged = staged;

		PendingRestoreMarker marker;
		marker.stagingDir = staged.stagingDir;
		marker.targetDir = machine.config();
		marker.safetyBackupPath = staged.safetyBackupPath;
		marker.fileWriteMaxAttempts = 2;
		marker.fileWriteRetryDelayMs = 1;
		std::string error;
		REQUIRE(RestoreManager::WritePendingRestoreMarker(PendingRestoreMarkerPath(machine.safety()), marker, error));

		return ApplyPendingRestoreAndStoreResult(machine.safety(), machine.config());
	}

	CollectionResult lastCollected;
	StagedRestoreOutcome lastStaged;
};

void CheckRestoredMachine(const Machine &machine, bool portable)
{
	const auto cfg = machine.config();

	// Settings, profiles and scenes, byte for byte, including non-ASCII names.
	CHECK(Read(cfg / "user.ini") == kUserIni);
	CHECK(Read(cfg / "basic" / "profiles" / "Stream" / "basic.ini") == "[Video]\nBaseCX=2560\nBaseCY=1440\n");
	CHECK(Read(cfg / "basic" / "profiles" / "Stream" / "service.json") == "{\"type\":\"rtmp_common\"}");
	CHECK(Read(cfg / "basic" / "profiles" / fs::u8path(kCyrillicProfile) / "basic.ini") == "[Video]\nBaseCX=1920\n");
	CHECK(Read(cfg / "basic" / "scenes" / "StreamSceneCollection.json") == kSceneJson);
	CHECK(Read(cfg / "basic" / "scenes" / fs::u8path(kCyrillicCollection + ".json")) == "{\"cyrillic\":true}");
	CHECK(Read(cfg / "plugin_config" / "aitum-multistream" / "config.json") == "{\"aitum\":1}");

	// The browser: logins yes, caches no.
	CHECK(Read(cfg / "plugin_config" / "obs-browser" / "Cookies") == "LOGINS");
	CHECK_FALSE(fs::exists(cfg / "plugin_config" / "obs-browser" / "Cache"));
	CHECK_FALSE(fs::exists(cfg / "plugin_config" / "obs-browser" / "first_party_sets.db"));
	CHECK_FALSE(fs::exists(cfg / "logs"));

	// Third-party plugins are back, in the per-user folder of THIS install's mode.
	const fs::path perUser = portable ? machine.obsBase() / "plugins" : machine.programData();
	CHECK(Read(perUser / "obs-multi-rtmp" / "bin" / "64bit" / "obs-multi-rtmp.dll") == "MULTI");
	CHECK(Read(machine.obsBase() / "obs-plugins" / "64bit" / "aitum-multistream.dll") == "AITUM");
	CHECK(Read(machine.obsBase() / "data" / "obs-plugins" / "aitum-multistream" / "locale" / "en-US.ini") == "aitum=1");

	// OBS's own plugins were never part of it: whatever this install has of them
	// (or nothing) is not the source's copy.
	const auto ownFfmpeg = machine.obsBase() / "obs-plugins" / "64bit" / "obs-ffmpeg.dll";
	CHECK(Read(ownFfmpeg) != "OBS-OWN-FFMPEG");
	CHECK_FALSE(fs::exists(machine.obsBase() / "obs-plugins" / "64bit" / "libcef.dll"));
	CHECK_FALSE(fs::exists(machine.obsBase() / "data" / "obs-plugins" / "obs-ffmpeg"));

	// The selection OBS will start with is what the source used.
	const auto selection = ReadActiveSelection(cfg);
	CHECK(selection.profile == "Stream");
	CHECK(selection.sceneCollection == "StreamSceneCollection");
}

} // namespace

TEST_CASE("Windows: what a backup of a normal install contains", "[WindowsScenario]")
{
	ScenarioFixture fixture;
	const auto outcome = fixture.Backup("");
	REQUIRE(outcome.success);
	const auto &collected = fixture.lastCollected;

	CHECK(ArchiveHas(collected, "user.ini"));
	CHECK(ArchiveHas(collected, "global.ini"));
	CHECK(ArchiveHas(collected, fs::path("basic") / "scenes" / "StreamSceneCollection.json"));
	CHECK(ArchiveHas(collected, fs::path("plugin_config") / "obs-browser" / "Cookies"));
	CHECK(ArchiveHas(collected, fs::path("system-plugins") / "obs-multi-rtmp" / "bin" / "64bit" / "obs-multi-rtmp.dll"));
	CHECK(ArchiveHas(collected, fs::path("program-plugins-bin") / "aitum-multistream.dll"));
	CHECK(ArchiveHas(collected, fs::path("program-plugins-data") / "aitum-multistream" / "locale" / "en-US.ini"));

	CHECK_FALSE(ArchiveHas(collected, fs::path("logs") / "2026-01-01.txt"));
	CHECK_FALSE(ArchiveHas(collected, fs::path("plugin_config") / "obs-browser" / "Cache" / "Cache_Data" / "f_000001"));
	CHECK_FALSE(ArchiveHas(collected, fs::path("plugin_config") / "obs-browser" / "first_party_sets.db"));
	CHECK_FALSE(ArchiveHas(collected, fs::path("program-plugins-bin") / "obs-ffmpeg.dll"));
	CHECK_FALSE(ArchiveHas(collected, fs::path("program-plugins-bin") / "libcef.dll"));
	CHECK_FALSE(ArchiveHas(collected, fs::path("program-plugins-data") / "obs-ffmpeg" / "x.ini"));
}

TEST_CASE("Windows: a normal install restores into a fresh portable one, twice", "[WindowsScenario]")
{
	ScenarioFixture fixture;
	const auto backup = fixture.Backup("");
	REQUIRE(backup.success);

	// A fresh portable OBS with only its own defaults and the running plugin.
	Write(fixture.target.config() / "user.ini", "[Basic]\nProfile=Untitled\nSceneCollection=Untitled\n");
	Write(fixture.target.config() / "basic" / "profiles" / "Untitled" / "basic.ini", "[Video]\nBaseCX=1920\n");
	Write(fixture.target.config() / "basic" / "scenes" / "Untitled.json", "{\"default\":true}");
	Write(fixture.target.obsBase() / "obs-plugins" / "64bit" / "obs-bakeup.dll", "TARGET-RUNNING-PLUGIN");
	Write(fixture.target.obsBase() / "obs-plugins" / "64bit" / "obs-ffmpeg.dll", "TARGET-OWN-FFMPEG");

	for (int pass = 1; pass <= 2; ++pass) {
		INFO("restore pass " << pass);
		const auto applied = fixture.RestoreInto(fixture.target, backup.archivePath, "", /*portable=*/true);

		REQUIRE(applied.wasPending);
		CHECK(applied.commit.success);
		CHECK_FALSE(applied.commit.rolledBack);
		CheckRestoredMachine(fixture.target, /*portable=*/true);

		// This plugin and OBS's own files on the target were not touched.
		CHECK(Read(fixture.target.obsBase() / "obs-plugins" / "64bit" / "obs-bakeup.dll") == "TARGET-RUNNING-PLUGIN");
		CHECK(Read(fixture.target.obsBase() / "obs-plugins" / "64bit" / "obs-ffmpeg.dll") == "TARGET-OWN-FFMPEG");

		// The next start shows the outcome once, and nothing is left pending.
		StoredRestoreResult stored;
		REQUIRE(TakeStoredRestoreResult(fixture.target.safety(), stored));
		CHECK(stored.success);
		CHECK_FALSE(TakeStoredRestoreResult(fixture.target.safety(), stored));
		CHECK_FALSE(fs::exists(PendingRestoreMarkerPath(fixture.target.safety())));
	}
}

TEST_CASE("Windows: a password-protected backup restores the same way", "[WindowsScenario]")
{
	ScenarioFixture fixture;
	const std::string password = "\xD0\xBF\xD0\xB0\xD1\x80\xD0\xBE\xD0\xBB\xD1\x8C-secret";
	const auto backup = fixture.Backup(password);
	REQUIRE(backup.success);
	CHECK(backup.archivePath.extension() == ".obsbak");

	const auto applied = fixture.RestoreInto(fixture.target, backup.archivePath, password, /*portable=*/false);

	REQUIRE(applied.wasPending);
	CHECK(applied.commit.success);
	CheckRestoredMachine(fixture.target, /*portable=*/false);
}

TEST_CASE("Windows: a backup of a portable install restores into a normal one", "[WindowsScenario]")
{
	ScenarioFixture fixture;
	// Rebuild the source as a portable install: its own plugins folder, no ProgramData.
	fs::remove_all(fixture.source.programData().parent_path().parent_path());
	Write(fixture.source.obsBase() / "plugins" / "obs-multi-rtmp" / "bin" / "64bit" / "obs-multi-rtmp.dll", "MULTI");

	auto collected = CollectFiles(fixture.source.config());
	const auto roots = ResolveWindowsPluginRoots(fixture.source.obsBase(), /*portableMode=*/true, std::nullopt);
	BackupOptions options;
	options.destinationDir = fixture.root / "backups";
	options.pluginVersion = "0.1.0";
	options.obsVersion = "32.2.2";
	options.sourceOs = "windows";
	for (const auto &root : roots) {
		CollectPluginRoot(root, collected);
		options.extraIncludedSections.push_back(root.archivePrefix);
	}
	const auto backup = BackupManager::CreateBackup(collected, options);
	REQUIRE(backup.success);

	const auto applied = fixture.RestoreInto(fixture.target, backup.archivePath, "", /*portable=*/false);

	REQUIRE(applied.wasPending);
	CHECK(applied.commit.success);
	CheckRestoredMachine(fixture.target, /*portable=*/false);
}

TEST_CASE("Windows: the restored scene files can be put back after OBS rewrites them", "[WindowsScenario]")
{
	ScenarioFixture fixture;
	const auto backup = fixture.Backup("");
	REQUIRE(backup.success);
	const auto applied = fixture.RestoreInto(fixture.target, backup.archivePath, "", /*portable=*/true);
	REQUIRE(applied.commit.success);

	const auto snapshot = SnapshotSceneCollections(fixture.target.config());
	REQUIRE(snapshot.size() >= 2);

	// OBS, loading the collection on a canvas of another size, rescales the crop
	// and saves it. Writing the snapshot back undoes that.
	Write(fixture.target.config() / "basic" / "scenes" / "StreamSceneCollection.json",
	      "{\"resolution\":{\"x\":2560,\"y\":1440},\"crop_right\":1464}");
	CHECK(WriteSceneCollections(fixture.target.config(), snapshot) == static_cast<int>(snapshot.size()));
	CHECK(Read(fixture.target.config() / "basic" / "scenes" / "StreamSceneCollection.json") == kSceneJson);
}
