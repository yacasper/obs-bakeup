// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <cstddef>
#include <string>

namespace obs_backuper {

// Password quality feedback for the "protect with password" dialog. Advisory
// only: TooShort blocks creating a backup (the minimum is a hard rule), Weak
// merely triggers a soft warning the user may ignore.
enum class PasswordStrength {
	TooShort, // fewer than kMinPasswordLength characters
	Weak,     // long enough, but trivially guessable
	Fair,
	Good,
};

constexpr std::size_t kMinPasswordLength = 8;

// Number of Unicode characters (code points) in a UTF-8 string -- what the
// user perceives as the length, not the byte count (Cyrillic / emoji passwords
// are multi-byte).
std::size_t Utf8Length(const std::string &utf8);

PasswordStrength EvaluatePasswordStrength(const std::string &utf8Password);

} // namespace obs_backuper
