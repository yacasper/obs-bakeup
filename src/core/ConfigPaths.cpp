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

std::filesystem::path ObsBasePathFromExecutableDir(Platform platform, const std::filesystem::path &executableDir)
{
	if (executableDir.empty())
		return {};

	auto base = (platform == Platform::Windows ? executableDir / ".." / ".." : executableDir / "..").lexically_normal();
	if (!base.has_filename())
		base = base.parent_path(); // "C:/obs/" -> "C:/obs"
	return base;
}

bool IsPortableMode(Platform platform, const std::filesystem::path &executableDir, bool portableFlagGiven)
{
	if (portableFlagGiven)
		return true;

	const auto base = ObsBasePathFromExecutableDir(platform, executableDir);
	if (base.empty())
		return false;

	for (const char *marker : {"portable_mode", "obs_portable_mode", "portable_mode.txt", "obs_portable_mode.txt"}) {
		std::error_code ec;
		if (std::filesystem::exists(base / marker, ec) && !ec)
			return true;
	}
	return false;
}

std::filesystem::path ResolveObsDataDir(Platform platform, const std::optional<std::filesystem::path> &obsConfigPath,
					 const std::filesystem::path &executableDir, bool portableMode)
{
	if (portableMode && !executableDir.empty())
		return ObsBasePathFromExecutableDir(platform, executableDir) / "config" / "obs-studio";

	if (obsConfigPath && !obsConfigPath->empty())
		return *obsConfigPath;

	return FallbackDataDir(platform);
}

std::vector<PluginRoot> ResolveWindowsPluginRoots(const std::filesystem::path &obsBaseDir, bool portableMode,
						   const std::optional<std::filesystem::path> &programDataPluginsDir)
{
	std::vector<PluginRoot> roots;

	if (portableMode) {
		if (!obsBaseDir.empty())
			roots.push_back({kPortablePluginsPrefix, obsBaseDir / "plugins", {}});
	} else if (programDataPluginsDir && !programDataPluginsDir->empty()) {
		roots.push_back({kSystemPluginsPrefix, *programDataPluginsDir, {}});
	}

	if (!obsBaseDir.empty()) {
		roots.push_back({kProgramPluginsBinPrefix, obsBaseDir / "obs-plugins" / "64bit", ObsShippedPluginStems()});
		roots.push_back({kProgramPluginsDataPrefix, obsBaseDir / "data" / "obs-plugins", ObsShippedPluginStems()});
	}
	return roots;
}

std::vector<PluginRoot> ResolveMacPluginRoots(const std::optional<std::filesystem::path> &systemPluginsDir)
{
	std::vector<PluginRoot> roots;
	if (systemPluginsDir && !systemPluginsDir->empty())
		roots.push_back({kMacSystemPluginsPrefix, *systemPluginsDir, {}});
	return roots;
}

} // namespace obs_backuper
