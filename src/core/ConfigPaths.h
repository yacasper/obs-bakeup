// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <filesystem>
#include <optional>

namespace obs_backuper {

enum class Platform { Windows, MacOS };

// Pure, testable logic with no access to the real OBS API — takes
// already-resolved inputs, so it's fully covered by unit tests.
//
// obsConfigPath        — the path OBS returned (os_get_config_path("obs-studio")),
//                         if the call succeeded.
// executableDir        — the directory the OBS executable lives in (for
//                         portable-mode detection).
// portableMarkerExists — true if a portable-mode marker file was found next
//                         to the executable.
//
// Priority: if a portable-mode marker is found, the path next to the
// executable is used (spec 3, "work correctly in... portable [mode]").
// Otherwise, if the OBS API returned a path, that is used. Otherwise, a
// platform fallback (%APPDATA%\obs-studio or ~/Library/Application
// Support/obs-studio).
std::filesystem::path ResolveObsDataDir(Platform platform, const std::optional<std::filesystem::path> &obsConfigPath,
					 const std::filesystem::path &executableDir, bool portableMarkerExists);

} // namespace obs_backuper
