// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <filesystem>
#include <string>

namespace obs_backuper {

// On Windows, path::string() / generic_string() convert to the ANSI code page
// and THROW for a name that code page cannot represent (e.g. a Cyrillic file
// name on a Western-locale system), and path(std::string) reads its argument
// as ANSI, so UTF-8 text from Qt or OBS comes out garbled. Everywhere a path
// crosses to or from text (logs, messages, Qt, OBS, the restore marker) it
// goes through these two functions, which always use UTF-8 and never throw.

// C++17's u8string() is std::string and C++20's is std::u8string; the byte-wise
// copy compiles under both.
inline std::string PathToUtf8(const std::filesystem::path &path)
{
	const auto text = path.u8string();
	return std::string(text.begin(), text.end());
}

// Like PathToUtf8, with "/" as the separator (archive entry names).
inline std::string GenericPathToUtf8(const std::filesystem::path &path)
{
	const auto text = path.generic_u8string();
	return std::string(text.begin(), text.end());
}

inline std::filesystem::path PathFromUtf8(const std::string &text)
{
	return std::filesystem::u8path(text);
}

} // namespace obs_backuper
