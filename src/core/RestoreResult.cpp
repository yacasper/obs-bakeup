// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "RestoreResult.h"

#include <cstdlib>
#include <fstream>
#include <system_error>

namespace obs_backuper {

namespace {

// One line per value: backslash, CR and LF are escaped.
std::string Escape(const std::string &value)
{
	std::string out;
	for (const char c : value) {
		if (c == '\\')
			out += "\\\\";
		else if (c == '\n')
			out += "\\n";
		else if (c == '\r')
			out += "\\r";
		else
			out += c;
	}
	return out;
}

std::string Unescape(const std::string &value)
{
	std::string out;
	for (std::size_t i = 0; i < value.size(); ++i) {
		if (value[i] == '\\' && i + 1 < value.size()) {
			const char next = value[++i];
			out += next == 'n' ? '\n' : next == 'r' ? '\r' : next;
		} else {
			out += value[i];
		}
	}
	return out;
}

} // namespace

bool WriteRestoreResult(const std::filesystem::path &path, const StoredRestoreResult &result)
{
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);

	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out.is_open())
		return false;
	out << "success=" << (result.success ? 1 : 0) << '\n';
	out << "rolledBack=" << (result.rolledBack ? 1 : 0) << '\n';
	out << "errorKind=" << static_cast<int>(result.errorKind) << '\n';
	out << "errorMessage=" << Escape(result.errorMessage) << '\n';
	return out.good();
}

bool ReadRestoreResult(const std::filesystem::path &path, StoredRestoreResult &result)
{
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open())
		return false;

	StoredRestoreResult read;
	bool sawSuccess = false;
	std::string line;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		const auto separator = line.find('=');
		if (separator == std::string::npos)
			continue;
		const std::string key = line.substr(0, separator);
		const std::string value = line.substr(separator + 1);

		if (key == "success") {
			read.success = value == "1";
			sawSuccess = true;
		} else if (key == "rolledBack") {
			read.rolledBack = value == "1";
		} else if (key == "errorKind") {
			read.errorKind = static_cast<ErrorKind>(std::atoi(value.c_str()));
		} else if (key == "errorMessage") {
			read.errorMessage = Unescape(value);
		}
	}
	if (!sawSuccess)
		return false;
	result = read;
	return true;
}

void RemoveRestoreResult(const std::filesystem::path &path)
{
	std::error_code ec;
	std::filesystem::remove(path, ec);
}

} // namespace obs_backuper
