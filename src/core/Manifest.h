// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <string>
#include <vector>

namespace obs_backuper {

// The backup_format_version this build writes and the highest version it
// knows how to restore (bumped when the archive layout changes incompatibly).
constexpr int kCurrentBackupFormatVersion = 1;

// Data for manifest.json at the archive root (plugin/OBS/OS versions, creation time and
// the included/excluded sections; used to validate an archive and inform the
// user before restoring). Gathered by the caller (BackupWorker, which has
// access to OBS/platform APIs) and handed to a pure, testable serializer.
struct ManifestInfo {
	int backupFormatVersion = kCurrentBackupFormatVersion;
	std::string pluginVersion;
	std::string createdAtIso8601;
	std::string sourceOs;	      // "windows" | "macos"
	std::string sourceOsVersion; // may be empty if unavailable
	std::string obsVersion;
	std::vector<std::string> includedSections;
	std::vector<std::string> excludedSections;
};

// Serializes ManifestInfo into the manifest.json text. Does not depend on any
// third-party JSON library — the field set is simple and fixed.
std::string BuildManifestJson(const ManifestInfo &info);

// Parses a manifest.json produced by BuildManifestJson back into a
// ManifestInfo (used by RestoreManager to validate/display an archive before
// restoring it). Does not depend on any third-party JSON library — since the
// format is our own fixed, flat schema, a small hand-rolled scanner is enough
// and can fail gracefully on a corrupt/foreign file instead of crashing.
// Returns false (with errorMessage set) if the text isn't valid JSON in the
// expected shape, or is missing backup_format_version.
bool ParseManifestJson(const std::string &json, ManifestInfo &outInfo, std::string &errorMessage);

} // namespace obs_backuper
