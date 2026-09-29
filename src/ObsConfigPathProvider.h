// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <vector>

#include "core/ConfigPaths.h"
#include "core/FileCollector.h"

namespace obs_backuper {

// Determines the real OBS data directory on this machine via the OBS API
// (os_get_config_path) and Qt (QCoreApplication::applicationDirPath() — for
// portable-mode detection). Not covered by unit tests directly: all testable
// logic lives in ResolveObsDataDir() (core/ConfigPaths.h).
std::filesystem::path GetObsDataDir();

// Folders OUTSIDE the OBS data dir where third-party plugins live and that go
// into a backup too (see core/FileCollector.h, PluginRoot). Windows only:
// %ProgramData%\obs-studio\plugins normally, <OBS folder>\plugins in portable
// mode. Empty on macOS, where plugins are inside the data dir.
std::vector<PluginRoot> GetExtraPluginRoots();

// "<OBS data dir's parent>/obs-backuper-safety" -- where safety backups, the
// staged-restore directory and the pending-restore marker all live (see
// src/core/RestoreManager.h). Shared between the UI (which writes there) and
// plugin-main.cpp (which reads the marker at obs_module_load()), so both
// sides agree on the path without either hardcoding it.
std::filesystem::path GetSafetyBackupDir();

// Where PluginSettings (core/PluginSettings.h) is persisted: next to, not
// inside, the obs-studio data dir, so restoring a backup never rolls the
// settings back.
std::filesystem::path GetPluginSettingsPath();

} // namespace obs_backuper
