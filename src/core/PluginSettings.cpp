// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "PluginSettings.h"
#include "PathUtf8.h"

#include <fstream>
#include <system_error>

namespace obs_backuper {

namespace {

constexpr const char *kDismissedKey = "sensitiveWarningDismissed";
constexpr const char *kLastBackupKey = "lastBackupIso8601";
constexpr const char *kLastBackupPathKey = "lastBackupPath";
constexpr const char *kEncryptBackupsKey = "encryptBackups";

// Values are single-line; strip anything that would break the format.
std::string SingleLine(const std::string &value)
{
	std::string out;
	for (char c : value) {
		if (c != '\n' && c != '\r')
			out.push_back(c);
	}
	return out;
}

} // namespace

PluginSettings PluginSettings::Load(const std::filesystem::path &path)
{
	PluginSettings settings;

	std::ifstream in(path, std::ios::binary);
	if (!in.is_open())
		return settings;

	std::string line;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();

		const auto eq = line.find('=');
		if (eq == std::string::npos)
			continue;

		const std::string key = line.substr(0, eq);
		const std::string value = line.substr(eq + 1);

		if (key == kDismissedKey)
			settings.sensitiveWarningDismissed = (value == "1");
		else if (key == kLastBackupKey)
			settings.lastBackupIso8601 = value;
		else if (key == kLastBackupPathKey)
			settings.lastBackupPath = value;
		else if (key == kEncryptBackupsKey)
			settings.encryptBackups = (value == "1");
	}
	return settings;
}

bool PluginSettings::Save(const std::filesystem::path &path, std::string &errorMessage) const
{
	std::error_code ec;
	if (path.has_parent_path())
		std::filesystem::create_directories(path.parent_path(), ec);

	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out.is_open()) {
		errorMessage = "failed to open plugin settings for writing: " + PathToUtf8(path);
		return false;
	}

	out << kDismissedKey << '=' << (sensitiveWarningDismissed ? "1" : "0") << '\n';
	out << kLastBackupKey << '=' << SingleLine(lastBackupIso8601) << '\n';
	out << kLastBackupPathKey << '=' << SingleLine(lastBackupPath) << '\n';
	out << kEncryptBackupsKey << '=' << (encryptBackups ? "1" : "0") << '\n';
	out.flush();

	if (!out.good()) {
		errorMessage = "failed to write plugin settings: " + PathToUtf8(path);
		return false;
	}
	return true;
}

} // namespace obs_backuper
