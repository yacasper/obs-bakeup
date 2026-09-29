// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include "ErrorKind.h"

#include <filesystem>
#include <string>

namespace obs_backuper {

// How applying a staged restore turned out. The restore is applied while OBS
// shuts down, when there is no window left to tell the user, so the outcome is
// stored in a small file and shown by the next OBS start.
struct StoredRestoreResult {
	bool success = false;
	bool rolledBack = false;
	ErrorKind errorKind = ErrorKind::None;
	std::string errorMessage;
};

// Returns false if the file cannot be written.
bool WriteRestoreResult(const std::filesystem::path &path, const StoredRestoreResult &result);

// Returns false if there is no readable, well-formed result file.
bool ReadRestoreResult(const std::filesystem::path &path, StoredRestoreResult &result);

// Deletes the file if present.
void RemoveRestoreResult(const std::filesystem::path &path);

} // namespace obs_backuper
