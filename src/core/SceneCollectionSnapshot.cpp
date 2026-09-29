// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "SceneCollectionSnapshot.h"

#include "PathUtf8.h"

#include <fstream>
#include <sstream>
#include <system_error>

namespace obs_backuper {

namespace {

std::filesystem::path ScenesDir(const std::filesystem::path &obsDataDir)
{
	return obsDataDir / "basic" / "scenes";
}

bool HasJsonExtension(const std::filesystem::path &path)
{
	std::string extension = PathToUtf8(path.extension());
	for (char &c : extension) {
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}
	return extension == ".json";
}

} // namespace

std::vector<SceneCollectionFile> SnapshotSceneCollections(const std::filesystem::path &obsDataDir,
							    std::uintmax_t maxTotalBytes)
{
	std::vector<SceneCollectionFile> files;
	std::error_code ec;
	std::uintmax_t total = 0;

	std::filesystem::directory_iterator it(ScenesDir(obsDataDir), ec);
	const std::filesystem::directory_iterator end;
	for (; !ec && it != end; it.increment(ec)) {
		std::error_code fileEc;
		if (!it->is_regular_file(fileEc) || fileEc || !HasJsonExtension(it->path()))
			continue;

		const auto size = it->file_size(fileEc);
		if (fileEc)
			continue;
		total += size;
		if (total > maxTotalBytes)
			return {};

		std::ifstream in(it->path(), std::ios::binary);
		if (!in.is_open())
			continue;
		std::ostringstream buffer;
		buffer << in.rdbuf();
		files.push_back({PathToUtf8(it->path().filename()), buffer.str()});
	}
	return files;
}

int WriteSceneCollections(const std::filesystem::path &obsDataDir, const std::vector<SceneCollectionFile> &files)
{
	std::error_code ec;
	std::filesystem::create_directories(ScenesDir(obsDataDir), ec);

	int written = 0;
	for (const auto &file : files) {
		std::ofstream out(ScenesDir(obsDataDir) / PathFromUtf8(file.fileName), std::ios::binary | std::ios::trunc);
		if (!out.is_open())
			continue;
		out.write(file.bytes.data(), static_cast<std::streamsize>(file.bytes.size()));
		if (out.good())
			++written;
	}
	return written;
}

} // namespace obs_backuper
