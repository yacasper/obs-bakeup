// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace obs_backuper {

struct SceneCollectionFile {
	std::string fileName; // e.g. "My Scenes.json" (UTF-8), inside <obsDataDir>/basic/scenes
	std::string bytes;
};

// The scene collection files (*.json; not the .bak/.v1 copies) in
// <obsDataDir>/basic/scenes, byte for byte.
//
// Right after a restore OBS may load a collection while it still runs on the
// profile it started with, at another canvas size. It then rescales things such
// as crops and saves the result over the restored file. Taking this snapshot
// straight after the restore lets the plugin put the files back as they were in
// the backup once the right profile is active. Nothing is returned when the
// files together exceed maxTotalBytes.
std::vector<SceneCollectionFile> SnapshotSceneCollections(const std::filesystem::path &obsDataDir,
							    std::uintmax_t maxTotalBytes = 256ull * 1024 * 1024);

// Writes the snapshot back into <obsDataDir>/basic/scenes and returns how many
// files were written.
int WriteSceneCollections(const std::filesystem::path &obsDataDir, const std::vector<SceneCollectionFile> &files);

} // namespace obs_backuper
