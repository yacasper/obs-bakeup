// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <filesystem>
#include <string>

namespace obs_backuper {

// Small persistent plugin state (Stage 5): whether the user dismissed the
// "archive contains sensitive data" warning, and when the last backup was
// created. Stored as a plain key=value text file, same rationale as
// RestoreManager's pending-restore marker. Lives outside the obs-studio data
// dir on purpose, so restoring an old backup never rolls it back.
struct PluginSettings {
	bool sensitiveWarningDismissed = false;
	std::string lastBackupIso8601; // UTC, "%Y-%m-%dT%H:%M:%SZ"; empty = never
	std::string lastBackupPath;

	// Stage 7: last state of the "Protect with password" checkbox. Only the
	// checkbox is remembered -- never the password itself.
	bool encryptBackups = false;

	// Missing file, unreadable file and unknown/garbled lines all yield
	// defaults for the affected fields -- settings must never block a backup.
	static PluginSettings Load(const std::filesystem::path &path);

	bool Save(const std::filesystem::path &path, std::string &errorMessage) const;
};

} // namespace obs_backuper
