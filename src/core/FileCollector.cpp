// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "FileCollector.h"

#include <algorithm>
#include <system_error>

namespace obs_backuper {

const std::vector<std::string> kDefaultIncludedTopLevelEntries = {
	"global.ini", "user.ini", "user.ini.bak", "basic", "plugin_config", "plugins", "themes",
};

namespace {

bool IsIncluded(const std::filesystem::path &entryPath, const std::vector<std::string> &includedTopLevelEntries)
{
	const std::string name = entryPath.filename().string();
	return std::find(includedTopLevelEntries.begin(), includedTopLevelEntries.end(), name) !=
	       includedTopLevelEntries.end();
}

void AddFile(const std::filesystem::path &rootDir, const std::filesystem::path &filePath, std::uintmax_t sizeBytes,
	     CollectionResult &result)
{
	result.files.push_back({std::filesystem::relative(filePath, rootDir), filePath, sizeBytes});
	result.totalSizeBytes += sizeBytes;
}

void CollectFromEntry(const std::filesystem::path &rootDir, const std::filesystem::path &entryPath,
		       CollectionResult &result)
{
	std::error_code ec;

	if (std::filesystem::is_regular_file(entryPath, ec) && !ec) {
		const auto size = std::filesystem::file_size(entryPath, ec);
		if (!ec)
			AddFile(rootDir, entryPath, size, result);
		return;
	}

	if (ec || !std::filesystem::is_directory(entryPath, ec) || ec)
		return;

	std::filesystem::recursive_directory_iterator it(
		entryPath, std::filesystem::directory_options::skip_permission_denied, ec);
	const std::filesystem::recursive_directory_iterator end;

	for (; !ec && it != end; it.increment(ec)) {
		std::error_code fileEc;
		if (!it->is_regular_file(fileEc) || fileEc)
			continue;

		const auto size = it->file_size(fileEc);
		if (fileEc)
			continue;

		AddFile(rootDir, it->path(), size, result);
	}
}

} // namespace

CollectionResult CollectFiles(const std::filesystem::path &rootDir,
			       const std::vector<std::string> &includedTopLevelEntries)
{
	CollectionResult result;

	std::error_code ec;
	if (!std::filesystem::exists(rootDir, ec) || ec)
		return result;

	for (const auto &entry : std::filesystem::directory_iterator(rootDir, ec)) {
		if (ec)
			break;

		if (!IsIncluded(entry.path(), includedTopLevelEntries))
			continue;

		CollectFromEntry(rootDir, entry.path(), result);
	}

	return result;
}

} // namespace obs_backuper
