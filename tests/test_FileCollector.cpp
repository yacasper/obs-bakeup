// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "core/FileCollector.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

using namespace obs_backuper;

namespace {

// Creates a temporary directory mimicking the ~/Library/Application
// Support/obs-studio layout and
// removes it when the test finishes — no dependency on a real OBS install.
class ObsDirFixture {
public:
	ObsDirFixture() : root(std::filesystem::temp_directory_path() / "obs-backuper-tests" / UniqueName())
	{
		std::filesystem::create_directories(root);

		WriteFile(root / "global.ini", "size=1");
		WriteFile(root / "user.ini", "size=2");

		WriteFile(root / "basic" / "profiles" / "Record" / "basic.ini", "profile");
		WriteFile(root / "basic" / "profiles" / "Youtube_1080p" / "service.json", "{}");
		WriteFile(root / "basic" / "scenes" / "Record.json", "{}");

		WriteFile(root / "plugin_config" / "obs-websocket" / "config.json", "{}");
		// Installed plugins are backed up whole, together with their settings.
		WriteFile(root / "plugins" / "some-plugin.plugin" / "Contents" / "Info.plist", "binary-plugin");
		WriteFile(root / "plugins" / "some-plugin.plugin" / "Contents" / "MacOS" / "some-plugin", "machine-code");
		// plugin_config/obs-browser is intentionally copied whole, including its
		// cache: recoverability takes priority over size.
		WriteFile(root / "plugin_config" / "obs-browser" / "Cache" / "data_0", "binarycache");

		// Sections that must NOT end up in the backup by default.
		WriteFile(root / "logs" / "2026-09-28.txt", "log line");
		WriteFile(root / "crashes" / "crash.dmp", "crash");
		WriteFile(root / "plugin_manager" / "state.json", "{}");
		WriteFile(root / "profiler_data" / "session.bin", "profiler");
		WriteFile(root / ".DS_Store", "junk");
		// Finder metadata inside included folders is skipped too.
		WriteFile(root / "plugins" / ".DS_Store", "junk");
		WriteFile(root / "plugins" / "some-plugin.plugin" / ".DS_Store", "junk");
		WriteFile(root / "basic" / "profiles" / ".DS_Store", "junk");
		WriteFile(root / "plugin_config" / "obs-websocket" / ".DS_Store", "junk");
	}

	~ObsDirFixture() { std::filesystem::remove_all(root.parent_path()); }

	const std::filesystem::path root;

private:
	static std::string UniqueName()
	{
		static int counter = 0;
		return "fixture-" + std::to_string(++counter);
	}

	static void WriteFile(const std::filesystem::path &path, std::string_view content)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream file(path, std::ios::binary);
		file << content;
	}
};

bool ContainsRelativePath(const CollectionResult &result, const std::filesystem::path &relative)
{
	for (const auto &file : result.files) {
		if (file.relativePath == relative)
			return true;
	}
	return false;
}

} // namespace

TEST_CASE("CollectFiles includes only the allow-listed top-level sections", "[FileCollector]")
{
	ObsDirFixture fixture;

	const auto result = CollectFiles(fixture.root);

	CHECK(ContainsRelativePath(result, "global.ini"));
	CHECK(ContainsRelativePath(result, "user.ini"));
	CHECK(ContainsRelativePath(result, std::filesystem::path("basic") / "profiles" / "Record" / "basic.ini"));
	CHECK(ContainsRelativePath(result, std::filesystem::path("basic") / "scenes" / "Record.json"));
	CHECK(ContainsRelativePath(result,
				    std::filesystem::path("plugin_config") / "obs-websocket" / "config.json"));

	SECTION("plugin_config is copied whole, including browser cache — restorability over size")
	{
		CHECK(ContainsRelativePath(
			result, std::filesystem::path("plugin_config") / "obs-browser" / "Cache" / "data_0"));
	}

	SECTION("installed plugins are included whole, not just their settings")
	{
		CHECK(ContainsRelativePath(
			result, std::filesystem::path("plugins") / "some-plugin.plugin" / "Contents" / "Info.plist"));
		CHECK(ContainsRelativePath(result, std::filesystem::path("plugins") / "some-plugin.plugin" / "Contents" /
							     "MacOS" / "some-plugin"));
	}

	SECTION(".DS_Store files are never collected, at any depth")
	{
		for (const auto &file : result.files)
			CHECK(file.relativePath.filename() != ".DS_Store");
		CHECK_FALSE(ContainsRelativePath(result, std::filesystem::path("plugins") / ".DS_Store"));
		CHECK_FALSE(ContainsRelativePath(
			result, std::filesystem::path("plugins") / "some-plugin.plugin" / ".DS_Store"));

		// ...while their real neighbours still are.
		CHECK(ContainsRelativePath(
			result, std::filesystem::path("plugins") / "some-plugin.plugin" / "Contents" / "Info.plist"));
		CHECK(ContainsRelativePath(result, std::filesystem::path("basic") / "scenes" / "Record.json"));
	}

	SECTION("diagnostic and internal OBS state are excluded by default")
	{
		CHECK_FALSE(ContainsRelativePath(result, std::filesystem::path("logs") / "2026-09-28.txt"));
		CHECK_FALSE(ContainsRelativePath(result, std::filesystem::path("crashes") / "crash.dmp"));
		CHECK_FALSE(ContainsRelativePath(result, std::filesystem::path("plugin_manager") / "state.json"));
		CHECK_FALSE(ContainsRelativePath(result, std::filesystem::path("profiler_data") / "session.bin"));
		CHECK_FALSE(ContainsRelativePath(result, ".DS_Store"));
	}
}

TEST_CASE("CollectFiles computes the correct total size", "[FileCollector]")
{
	ObsDirFixture fixture;

	const auto result = CollectFiles(fixture.root);

	std::uintmax_t expectedTotal = 0;
	for (const auto &file : result.files)
		expectedTotal += std::filesystem::file_size(file.absolutePath);

	REQUIRE(result.totalSizeBytes == expectedTotal);
	REQUIRE(result.totalSizeBytes > 0);
}

TEST_CASE("CollectFiles returns an empty result for a non-existent directory", "[FileCollector]")
{
	const auto result = CollectFiles("/this/path/does/not/exist-obs-backuper-test");

	REQUIRE(result.files.empty());
	REQUIRE(result.totalSizeBytes == 0);
}

// ---------------------------------------------------------------------------
// Plugin folders outside the obs-studio directory (Windows: %ProgramData%).
// ---------------------------------------------------------------------------

namespace {

void WritePluginFile(const std::filesystem::path &path, const std::string &content)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary) << content;
}

class PluginRootFixture {
public:
	PluginRootFixture() : root(std::filesystem::temp_directory_path() / "obs-backuper-pluginroot-tests" / UniqueName())
	{
		std::filesystem::create_directories(root);
		WritePluginFile(root / "foo" / "foo.dll", "dll");
		WritePluginFile(root / "foo" / "data" / "locale" / "en-US.ini", "x=1");
		WritePluginFile(root / "bar" / "bin" / "64bit" / "bar.dll", "bar-dll");
		WritePluginFile(root / ".DS_Store", "junk");
	}
	~PluginRootFixture() { std::filesystem::remove_all(root.parent_path()); }

	const std::filesystem::path root;

private:
	static std::string UniqueName()
	{
		static int counter = 0;
		return "fixture-" + std::to_string(++counter);
	}
};

} // namespace

TEST_CASE("CollectPluginRoot names files under the archive prefix", "[FileCollector][pluginroot]")
{
	PluginRootFixture fixture;
	CollectionResult result;

	CollectPluginRoot({kSystemPluginsPrefix, fixture.root}, result);

	CHECK(result.files.size() == 3);
	CHECK(ContainsRelativePath(result, std::filesystem::path("system-plugins") / "foo" / "foo.dll"));
	CHECK(ContainsRelativePath(result, std::filesystem::path("system-plugins") / "foo" / "data" / "locale" / "en-US.ini"));
	CHECK(ContainsRelativePath(result, std::filesystem::path("system-plugins") / "bar" / "bin" / "64bit" / "bar.dll"));
	CHECK_FALSE(ContainsRelativePath(result, std::filesystem::path("system-plugins") / ".DS_Store"));

	// Absolute paths still point at the real files, and the total adds up.
	std::uintmax_t expected = 0;
	for (const auto &file : result.files) {
		CHECK(std::filesystem::exists(file.absolutePath));
		expected += std::filesystem::file_size(file.absolutePath);
	}
	CHECK(result.totalSizeBytes == expected);
}

TEST_CASE("CollectPluginRoot adds to what is already collected", "[FileCollector][pluginroot]")
{
	ObsDirFixture obs;
	PluginRootFixture fixture;

	auto result = CollectFiles(obs.root);
	const auto filesBefore = result.files.size();
	const auto sizeBefore = result.totalSizeBytes;

	CollectPluginRoot({kPortablePluginsPrefix, fixture.root}, result);

	CHECK(result.files.size() == filesBefore + 3);
	CHECK(result.totalSizeBytes > sizeBefore);
	CHECK(ContainsRelativePath(result, "global.ini")); // the settings are still there
}

TEST_CASE("CollectPluginRoot ignores a missing folder or an empty prefix", "[FileCollector][pluginroot]")
{
	PluginRootFixture fixture;
	CollectionResult result;

	CollectPluginRoot({kSystemPluginsPrefix, fixture.root / "does-not-exist"}, result);
	CollectPluginRoot({"", fixture.root}, result);
	CollectPluginRoot({kSystemPluginsPrefix, fixture.root / "foo" / "foo.dll"}, result); // a file, not a folder

	CHECK(result.files.empty());
	CHECK(result.totalSizeBytes == 0);
}

TEST_CASE("IsPluginRootEntry recognises only the plugin-folder prefixes", "[FileCollector][pluginroot]")
{
	CHECK(IsPluginRootEntry(std::filesystem::path("system-plugins") / "foo" / "foo.dll"));
	CHECK(IsPluginRootEntry(std::filesystem::path("portable-plugins") / "foo" / "foo.dll"));
	CHECK_FALSE(IsPluginRootEntry(std::filesystem::path("plugins") / "foo.plugin" / "lib"));
	CHECK_FALSE(IsPluginRootEntry(std::filesystem::path("system-plugins-old") / "x"));
	CHECK_FALSE(IsPluginRootEntry("global.ini"));
	CHECK_FALSE(IsPluginRootEntry(""));
}
