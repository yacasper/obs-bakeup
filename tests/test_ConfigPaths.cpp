// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "core/ConfigPaths.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

using namespace obs_backuper;

TEST_CASE("ResolveObsDataDir prefers the portable-mode marker over everything else", "[ConfigPaths]")
{
	const std::filesystem::path executableDir = "/Applications/OBS.app/Contents/MacOS";
	const std::optional<std::filesystem::path> obsConfigPath = std::filesystem::path("/Users/someone/Library/Application Support/obs-studio");

	const auto result = ResolveObsDataDir(Platform::MacOS, obsConfigPath, executableDir, /*portableMarkerExists=*/true);

	REQUIRE(result == executableDir / "config" / "obs-studio");
}

TEST_CASE("ResolveObsDataDir uses the OBS-reported path when there is no portable marker", "[ConfigPaths]")
{
	const std::filesystem::path executableDir = "/Applications/OBS.app/Contents/MacOS";
	const std::optional<std::filesystem::path> obsConfigPath =
		std::filesystem::path("/Users/someone/Library/Application Support/obs-studio");

	const auto result = ResolveObsDataDir(Platform::MacOS, obsConfigPath, executableDir, /*portableMarkerExists=*/false);

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
