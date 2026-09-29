// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "ObsConfigPathProvider.h"

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

std::filesystem::path GetObsDataDir()
{
	char buffer[4096] = {};
	std::optional<std::filesystem::path> obsConfigPath;

	if (os_get_config_path(buffer, sizeof(buffer), "obs-studio") == 0 && buffer[0] != '\0')
		obsConfigPath = std::filesystem::path(buffer);

	const auto executableDir =
		std::filesystem::path(QCoreApplication::applicationDirPath().toStdString());
	const bool portableMarkerExists = std::filesystem::exists(executableDir / "portable_mode.txt");

	return ResolveObsDataDir(kCurrentPlatform, obsConfigPath, executableDir, portableMarkerExists);
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
