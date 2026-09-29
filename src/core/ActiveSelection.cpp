// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "ActiveSelection.h"

#include <fstream>
#include <sstream>

namespace obs_backuper {

namespace {

std::string ReadWholeFile(const std::filesystem::path &path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open())
		return {};
	std::ostringstream buffer;
	buffer << in.rdbuf();
	return buffer.str();
}

std::string Trimmed(const std::string &text)
{
	const auto first = text.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
		return {};
	const auto last = text.find_last_not_of(" \t\r\n");
	return text.substr(first, last - first + 1);
}

} // namespace

ActiveSelection ParseActiveSelection(const std::string &iniText)
{
	ActiveSelection selection;
	std::istringstream lines(iniText);
	std::string line;
	bool inBasic = false;
	bool firstLine = true;

	while (std::getline(lines, line)) {
		// A UTF-8 byte order mark can precede the first section header.
		if (firstLine && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
			line.erase(0, 3);
		firstLine = false;

		const std::string trimmed = Trimmed(line);
		if (trimmed.empty())
			continue;
		if (trimmed.front() == '[') {
			inBasic = trimmed == "[Basic]";
			continue;
		}
		if (!inBasic)
			continue;

		const auto separator = trimmed.find('=');
		if (separator == std::string::npos)
			continue;
		const std::string key = trimmed.substr(0, separator);
		const std::string value = Trimmed(trimmed.substr(separator + 1));
		if (key == "Profile")
			selection.profile = value;
		else if (key == "SceneCollection")
			selection.sceneCollection = value;
	}
	return selection;
}

ActiveSelection ReadActiveSelection(const std::filesystem::path &obsDataDir)
{
	ActiveSelection selection = ParseActiveSelection(ReadWholeFile(obsDataDir / "user.ini"));
	if (selection.profile.empty() || selection.sceneCollection.empty()) {
		const ActiveSelection legacy = ParseActiveSelection(ReadWholeFile(obsDataDir / "global.ini"));
		if (selection.profile.empty())
			selection.profile = legacy.profile;
		if (selection.sceneCollection.empty())
			selection.sceneCollection = legacy.sceneCollection;
	}
	return selection;
}

} // namespace obs_backuper
