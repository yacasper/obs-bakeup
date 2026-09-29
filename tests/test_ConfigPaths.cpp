// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "core/ConfigPaths.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <fstream>

using namespace obs_backuper;

TEST_CASE("ResolveObsDataDir in portable mode uses <base>/config/obs-studio, ignoring the OBS-reported path",
	  "[ConfigPaths]")
{
	const std::optional<std::filesystem::path> obsConfigPath =
		std::filesystem::path("C:/Users/someone/AppData/Roaming/obs-studio");

	SECTION("Windows: the base folder is two levels above bin/64bit")
	{
		const std::filesystem::path executableDir = "C:/obs-portable/bin/64bit";
		const auto result = ResolveObsDataDir(Platform::Windows, obsConfigPath, executableDir, /*portableMode=*/true);
		REQUIRE(result == std::filesystem::path("C:/obs-portable/config/obs-studio"));
	}

	SECTION("macOS/Linux: the base folder is one level up")
	{
		const std::filesystem::path executableDir = "/opt/obs/bin";
		const auto result = ResolveObsDataDir(Platform::MacOS, obsConfigPath, executableDir, /*portableMode=*/true);
		REQUIRE(result == std::filesystem::path("/opt/obs/config/obs-studio"));
	}

	SECTION("without an executable directory portable mode cannot be resolved and is ignored")
	{
		const auto result = ResolveObsDataDir(Platform::Windows, obsConfigPath, {}, /*portableMode=*/true);
		REQUIRE(result == *obsConfigPath);
	}
}

TEST_CASE("ResolveObsDataDir uses the OBS-reported path when there is no portable marker", "[ConfigPaths]")
{
	const std::filesystem::path executableDir = "/Applications/OBS.app/Contents/MacOS";
	const std::optional<std::filesystem::path> obsConfigPath =
		std::filesystem::path("/Users/someone/Library/Application Support/obs-studio");

	const auto result = ResolveObsDataDir(Platform::MacOS, obsConfigPath, executableDir, /*portableMode=*/false);

	REQUIRE(result == *obsConfigPath);
}

TEST_CASE("ResolveObsDataDir falls back to the platform default when OBS API is unavailable", "[ConfigPaths]")
{
	const std::optional<std::filesystem::path> noObsConfigPath;

	SECTION("Windows fallback uses %APPDATA%")
	{
#ifdef _WIN32
		const auto result = ResolveObsDataDir(Platform::Windows, noObsConfigPath, {}, false);
		REQUIRE(result.string().find("obs-studio") != std::string::npos);
#endif
	}

	SECTION("macOS fallback uses ~/Library/Application Support")
	{
		const auto result = ResolveObsDataDir(Platform::MacOS, noObsConfigPath, {}, false);
		if (std::getenv("HOME") != nullptr) {
			REQUIRE(result == std::filesystem::path(std::getenv("HOME")) / "Library" / "Application Support" /
						   "obs-studio");
		}
	}
}

namespace {

// A throwaway OBS folder laid out like a Windows portable install:
// <root>/bin/64bit holds the executable, <root> holds the marker file.
class PortableLayoutFixture {
public:
	PortableLayoutFixture() : root(std::filesystem::temp_directory_path() / "obs-backuper-configpaths" / Unique())
	{
		std::filesystem::create_directories(root / "bin" / "64bit");
	}
	~PortableLayoutFixture() { std::filesystem::remove_all(root.parent_path()); }

	void Touch(const std::filesystem::path &path) const
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream(path).put('x');
	}

	const std::filesystem::path root;
	std::filesystem::path ExecutableDir() const { return root / "bin" / "64bit"; }

private:
	static std::string Unique()
	{
		static int counter = 0;
		return "fixture-" + std::to_string(++counter);
	}
};

} // namespace

TEST_CASE("IsPortableMode is off for a plain installation", "[ConfigPaths][portable]")
{
	PortableLayoutFixture fixture;
	CHECK_FALSE(IsPortableMode(Platform::Windows, fixture.ExecutableDir(), /*portableFlagGiven=*/false));
}

TEST_CASE("IsPortableMode finds OBS's marker files in the base folder, like OBS does", "[ConfigPaths][portable]")
{
	for (const char *name : {"portable_mode", "obs_portable_mode", "portable_mode.txt", "obs_portable_mode.txt"}) {
		PortableLayoutFixture fixture;
		fixture.Touch(fixture.root / name);
		INFO(name);
		CHECK(IsPortableMode(Platform::Windows, fixture.ExecutableDir(), false));
	}
}

TEST_CASE("IsPortableMode ignores a marker next to the executable, because OBS does", "[ConfigPaths][portable]")
{
	PortableLayoutFixture fixture;
	fixture.Touch(fixture.ExecutableDir() / "portable_mode.txt");
	CHECK_FALSE(IsPortableMode(Platform::Windows, fixture.ExecutableDir(), false));
}

TEST_CASE("IsPortableMode is on when --portable / -p was given", "[ConfigPaths][portable]")
{
	PortableLayoutFixture fixture;
	CHECK(IsPortableMode(Platform::Windows, fixture.ExecutableDir(), /*portableFlagGiven=*/true));
	CHECK(IsPortableMode(Platform::Windows, {}, true)); // the flag needs no directory
}

TEST_CASE("IsPortableMode without an executable directory is off", "[ConfigPaths][portable]")
{
	CHECK_FALSE(IsPortableMode(Platform::Windows, {}, false));
}

TEST_CASE("ObsBasePathFromExecutableDir follows OBS's BASE_PATH per platform", "[ConfigPaths][portable]")
{
	CHECK(ObsBasePathFromExecutableDir(Platform::Windows, "C:/obs/bin/64bit") == std::filesystem::path("C:/obs"));
	CHECK(ObsBasePathFromExecutableDir(Platform::MacOS, "/opt/obs/bin") == std::filesystem::path("/opt/obs"));
	CHECK(ObsBasePathFromExecutableDir(Platform::Windows, {}).empty());
}

TEST_CASE("A portable Windows layout resolves to the folder OBS really uses", "[ConfigPaths][portable]")
{
	PortableLayoutFixture fixture;
	fixture.Touch(fixture.root / "portable_mode.txt");
	const std::optional<std::filesystem::path> reportedByObs = std::filesystem::path("C:/Users/x/AppData/Roaming/obs-studio");

	const bool portable = IsPortableMode(Platform::Windows, fixture.ExecutableDir(), false);
	const auto dataDir = ResolveObsDataDir(Platform::Windows, reportedByObs, fixture.ExecutableDir(), portable);

	CHECK(dataDir == fixture.root / "config" / "obs-studio");
}
