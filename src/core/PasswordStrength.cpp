// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "PasswordStrength.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace obs_backuper {

namespace {

// A tiny denylist of the most common passwords that meet the length minimum.
const char *const kCommonPasswords[] = {"password", "password1", "password123", "12345678", "123456789",
					 "1234567890", "qwertyui", "qwerty123", "qwertyuiop", "iloveyou",
					 "11111111", "00000000", "abcdefgh", "abc12345", "letmein1",
					 "admin123", "welcome1", "passw0rd", "obsstudio", "streamer",
					 "йцукенгш", "пароль12", "пароль123"};

std::string LowerAscii(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

} // namespace

std::size_t Utf8Length(const std::string &utf8)
{
	std::size_t count = 0;
	for (const unsigned char c : utf8) {
		if ((c & 0xC0) != 0x80) // not a continuation byte
			++count;
	}
	return count;
}

PasswordStrength EvaluatePasswordStrength(const std::string &utf8Password)
{
	const std::size_t length = Utf8Length(utf8Password);
	if (length < kMinPasswordLength)
		return PasswordStrength::TooShort;

	const std::string lowered = LowerAscii(utf8Password);
	for (const char *common : kCommonPasswords) {
		if (lowered == common)
			return PasswordStrength::Weak;
	}

	// Distinct bytes approximate distinct characters closely enough for a
	// soft hint ("aaaaaaaa", "abababab").
	const std::set<unsigned char> distinct(utf8Password.begin(), utf8Password.end());
	if (distinct.size() <= 3)
		return PasswordStrength::Weak;

	bool lower = false, upper = false, digit = false, other = false;
	for (const unsigned char c : utf8Password) {
		if (c >= 0x80)
			other = true;
		else if (std::islower(c))
			lower = true;
		else if (std::isupper(c))
			upper = true;
		else if (std::isdigit(c))
			digit = true;
		else
			other = true;
	}
	const int classes = int(lower) + int(upper) + int(digit) + int(other);

	if (length >= 16 || (length >= 12 && classes >= 3))
		return PasswordStrength::Good;
	if (classes >= 2 && length >= 10)
		return PasswordStrength::Fair;
	if (classes >= 3)
		return PasswordStrength::Fair;
	return PasswordStrength::Weak;
}

} // namespace obs_backuper
