// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace obs_backuper {

// The profile and scene collection OBS was using, as recorded in its config.
// Empty strings mean "not recorded".
struct ActiveSelection {
	std::string profile;
	std::string sceneCollection;
};

// Reads [Basic] Profile / SceneCollection from <obsDataDir>/user.ini (OBS 31+),
// falling back per key to global.ini (older OBS, or a config OBS migrated from
// one). OBS picks the profile before any plugin loads, so after a restore the
// caller uses this to switch to what the restored config says. A missing or
// unreadable file yields empty values.
ActiveSelection ReadActiveSelection(const std::filesystem::path &obsDataDir);

// Same, from the text of one ini file.
ActiveSelection ParseActiveSelection(const std::string &iniText);

// One "key=value" line of an ini file, with the [section] it sits in.
struct IniEntry {
	std::string section;
	std::string key;
	std::string value;
};

// Every key=value line of an ini text, in file order. Values are kept exactly
// as written (a JSON value may contain "=" and quotes), minus the line end.
std::vector<IniEntry> ParseIniEntries(const std::string &iniText);

// The entries of <obsDataDir>/<fileName>; nothing if it cannot be read.
std::vector<IniEntry> ReadIniEntries(const std::filesystem::path &obsDataDir, const std::string &fileName);

} // namespace obs_backuper
