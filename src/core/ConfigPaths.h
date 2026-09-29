// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include "FileCollector.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace obs_backuper {

enum class Platform { Windows, MacOS };

// Portable mode, exactly as OBS decides it (frontend/obs-main.cpp): it is on
// when --portable / -p was passed, or when a marker file
// (portable_mode[.txt] / obs_portable_mode[.txt]) exists in OBS's base folder.
// That base folder is NOT the one holding the executable: on Windows OBS runs
// from <root>\bin\64bit and looks for the marker in <root> (OBS's BASE_PATH is
// "../.."); on macOS/Linux it is one level up ("..").
std::filesystem::path ObsBasePathFromExecutableDir(Platform platform, const std::filesystem::path &executableDir);

// executableDir -- the directory the OBS executable lives in.
// portableFlagGiven -- whether OBS was started with --portable / -p.
bool IsPortableMode(Platform platform, const std::filesystem::path &executableDir, bool portableFlagGiven);

// Pure, testable logic with no access to the real OBS API — takes
// already-resolved inputs, so it's fully covered by unit tests.
//
// obsConfigPath -- the path OBS returned (os_get_config_path("obs-studio")),
//                  if the call succeeded. It is NOT portable-aware: OBS's own
//                  portable handling lives in its frontend.
// executableDir -- the directory the OBS executable lives in.
// portableMode  -- whether OBS runs in portable mode (see IsPortableMode).
//
// Priority: in portable mode OBS keeps everything in <base>/config/obs-studio
// (see ObsBasePathFromExecutableDir), so that is used. Otherwise, if the OBS
// API returned a path, that is used. Otherwise, a platform fallback
// (%APPDATA%\obs-studio or ~/Library/Application Support/obs-studio).
std::filesystem::path ResolveObsDataDir(Platform platform, const std::optional<std::filesystem::path> &obsConfigPath,
					 const std::filesystem::path &executableDir, bool portableMode);

// The folders outside the obs-studio settings folder that hold installed
// plugins on Windows (see PluginRoot), in the order they are backed up:
//  - the per-user plugin folder: <obsBaseDir>\plugins in portable mode,
//    otherwise %ProgramData%\obs-studio\plugins (programDataPluginsDir; nothing
//    if Windows could not say where it is);
//  - the classic install folders under obsBaseDir, without what OBS ships itself:
//    obs-plugins\64bit and data\obs-plugins.
// An empty obsBaseDir gives no portable and no program-folder roots.
std::vector<PluginRoot> ResolveWindowsPluginRoots(const std::filesystem::path &obsBaseDir, bool portableMode,
						   const std::optional<std::filesystem::path> &programDataPluginsDir);

} // namespace obs_backuper
