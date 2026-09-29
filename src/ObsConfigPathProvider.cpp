// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "ObsConfigPathProvider.h"
#include "core/PathUtf8.h"

#include <util/platform.h>

#include <QCoreApplication>

namespace obs_backuper {

namespace {

constexpr Platform kCurrentPlatform =
#if defined(_WIN32)
	Platform::Windows;
#elif defined(__APPLE__)
	Platform::MacOS;
#else
#error "Unsupported platform"
#endif

} // namespace

namespace {

std::filesystem::path ExecutableDir()
{
	return PathFromUtf8(QCoreApplication::applicationDirPath().toStdString());
}

// Portable mode exists on Windows only (official macOS builds don't enable
// it), and os_get_config_path() knows nothing about it, so replicate OBS's
// own check.
bool RunningPortable()
{
	if (kCurrentPlatform != Platform::Windows)
		return false;

	bool portableFlagGiven = false;
	for (const QString &argument : QCoreApplication::arguments()) {
		if (argument == "--portable" || argument == "-p")
			portableFlagGiven = true;
	}
	return IsPortableMode(kCurrentPlatform, ExecutableDir(), portableFlagGiven);
}

} // namespace

std::filesystem::path GetObsDataDir()
{
	char buffer[4096] = {};
	std::optional<std::filesystem::path> obsConfigPath;

	// os_get_config_path() returns the length of the path (or -1 on failure),
	// not zero on success.
	if (os_get_config_path(buffer, sizeof(buffer), "obs-studio") > 0 && buffer[0] != '\0')
		obsConfigPath = PathFromUtf8(buffer); // OBS returns UTF-8

	return ResolveObsDataDir(kCurrentPlatform, obsConfigPath, ExecutableDir(), RunningPortable());
}

std::vector<PluginRoot> GetExtraPluginRoots()
{
	std::vector<PluginRoot> roots;

#if defined(_WIN32)
	std::optional<std::filesystem::path> programDataPlugins;
	char buffer[4096] = {};
	if (os_get_program_data_path(buffer, sizeof(buffer), "obs-studio/plugins") > 0 && buffer[0] != '\0')
		programDataPlugins = PathFromUtf8(buffer);

	roots = ResolveWindowsPluginRoots(ObsBasePathFromExecutableDir(kCurrentPlatform, ExecutableDir()),
					  RunningPortable(), programDataPlugins);
#elif defined(__APPLE__)
	std::optional<std::filesystem::path> systemPlugins;
	char buffer[4096] = {};
	if (os_get_program_data_path(buffer, sizeof(buffer), "obs-studio/plugins") > 0 && buffer[0] != '\0')
		systemPlugins = PathFromUtf8(buffer);

	roots = ResolveMacPluginRoots(systemPlugins);
#endif

	return roots;
}

std::filesystem::path GetSafetyBackupDir()
{
	return GetObsDataDir().parent_path() / "obs-backuper-safety";
}

std::filesystem::path GetPluginSettingsPath()
{
	return GetObsDataDir().parent_path() / "obs-backuper-settings.ini";
}

} // namespace obs_backuper
