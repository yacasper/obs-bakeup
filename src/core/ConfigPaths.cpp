// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "ConfigPaths.h"

#include <cstdlib>

namespace obs_backuper {

namespace {

std::filesystem::path FallbackDataDir(Platform platform)
{
	switch (platform) {
	case Platform::Windows: {
		const char *appData = std::getenv("APPDATA");
		if (appData)
			return std::filesystem::path(appData) / "obs-studio";
		return {};
	}
	case Platform::MacOS: {
		const char *home = std::getenv("HOME");
		if (home)
			return std::filesystem::path(home) / "Library" / "Application Support" / "obs-studio";
		return {};
	}
	}
	return {};
}

} // namespace

std::filesystem::path ResolveObsDataDir(Platform platform, const std::optional<std::filesystem::path> &obsConfigPath,
					 const std::filesystem::path &executableDir, bool portableMarkerExists)
{
	if (portableMarkerExists && !executableDir.empty())
		return executableDir / "config" / "obs-studio";

	if (obsConfigPath && !obsConfigPath->empty())
		return *obsConfigPath;

	return FallbackDataDir(platform);
}

} // namespace obs_backuper
