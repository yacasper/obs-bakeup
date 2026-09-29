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

// Whether a value from a restored user.ini / global.ini should be copied into
// the running OBS's config. [General] holds install-specific bookkeeping
// (version, install id, first-run flags) and [Basic] the profile and scene
// collection, which are switched by PlanSelectionSwitch instead.
bool ShouldMergeIniEntry(const IniEntry &entry);

enum class CollectionAction {
	None,
	Switch, // the restored collection is not the current one: load it
	Reload, // it is the current one, but was loaded on the wrong canvas: step away and back
};

struct SelectionSwitchPlan {
	bool switchProfile = false;
	std::string profile;
	CollectionAction collectionAction = CollectionAction::None;
	std::string collection;
	std::string bounceCollection; // for Reload: the collection to step through
};

// What to switch to after a restore that could only be applied once OBS was
// already running. The lists are the names OBS knows; a restored name OBS does
// not list (it built its lists before the restore) is left alone.
// `haveSceneSnapshot` says whether the restored scene files are at hand to be
// written back, which a Reload needs.
SelectionSwitchPlan PlanSelectionSwitch(const ActiveSelection &restored, const std::string &currentProfile,
					const std::vector<std::string> &knownProfiles, const std::string &currentCollection,
					const std::vector<std::string> &knownCollections, bool haveSceneSnapshot);

// The entries of <obsDataDir>/<fileName>; nothing if it cannot be read.
std::vector<IniEntry> ReadIniEntries(const std::filesystem::path &obsDataDir, const std::string &fileName);

} // namespace obs_backuper
