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
		// plugin_config/obs-browser is intentionally copied whole, including its
		// cache: recoverability takes priority over size.
		WriteFile(root / "plugin_config" / "obs-browser" / "Cache" / "data_0", "binarycache");

		// Sections that must NOT end up in the backup by default.
		WriteFile(root / "logs" / "2026-09-28.txt", "log line");
		WriteFile(root / "crashes" / "crash.dmp", "crash");
		WriteFile(root / "plugins" / "some-plugin.plugin" / "Contents" / "Info.plist", "binary-plugin");
		WriteFile(root / "profiler_data" / "session.bin", "profiler");
		WriteFile(root / ".DS_Store", "junk");
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

	SECTION("diagnostic and internal OBS state are excluded by default")
	{
		CHECK_FALSE(ContainsRelativePath(result, std::filesystem::path("logs") / "2026-09-28.txt"));
		CHECK_FALSE(ContainsRelativePath(result, std::filesystem::path("crashes") / "crash.dmp"));
		CHECK_FALSE(ContainsRelativePath(
			result, std::filesystem::path("plugins") / "some-plugin.plugin" / "Contents" / "Info.plist"));
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
