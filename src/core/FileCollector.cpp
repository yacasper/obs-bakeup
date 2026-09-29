// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "FileCollector.h"
#include "PathUtf8.h"

#include <algorithm>
#include <system_error>

namespace obs_backuper {

const std::vector<std::string> kDefaultIncludedTopLevelEntries = {
	"global.ini", "user.ini", "user.ini.bak", "basic", "plugin_config", "plugins", "themes",
};

namespace {

bool IsIncluded(const std::filesystem::path &entryPath, const std::vector<std::string> &includedTopLevelEntries)
{
	const std::string name = PathToUtf8(entryPath.filename());
	return std::find(includedTopLevelEntries.begin(), includedTopLevelEntries.end(), name) !=
	       includedTopLevelEntries.end();
}

// Finder metadata macOS drops into any folder it has displayed. It carries no
// OBS data, differs from machine to machine, and would end up in the backup
// (and be written back by a restore) inside every included folder.
bool IsJunkFile(const std::filesystem::path &path)
{
	return path.filename() == ".DS_Store";
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
		if (IsJunkFile(entryPath))
			return;
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
		if (!it->is_regular_file(fileEc) || fileEc || IsJunkFile(it->path()))
			continue;

		const auto size = it->file_size(fileEc);
		if (fileEc)
			continue;

		AddFile(rootDir, it->path(), size, result);
	}
}

// True if the first component of `relativePath` is one of root.excludedStems.
bool IsExcludedByStem(const PluginRoot &root, const std::filesystem::path &relativePath)
{
	if (root.excludedStems.empty() || relativePath.empty())
		return false;

	std::string name = relativePath.begin()->generic_u8string();
	name = name.substr(0, name.find('.'));
	for (char &c : name) {
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}
	for (const auto &stem : root.excludedStems) {
		if (name == stem)
			return true;
	}
	return false;
}

} // namespace

bool IsPluginRootEntry(const std::filesystem::path &archiveRelativePath)
{
	const auto first = archiveRelativePath.begin();
	if (first == archiveRelativePath.end())
		return false;
	const std::string name = first->generic_u8string();
	return name == kSystemPluginsPrefix || name == kPortablePluginsPrefix || name == kProgramPluginsBinPrefix ||
	       name == kProgramPluginsDataPrefix;
}

const std::vector<std::string> &ObsShippedPluginStems()
{
	static const std::vector<std::string> stems = {
		// Plugin modules (and the data folders named after them).
		"aja", "aja-output-ui", "decklink", "decklink-captions", "decklink-output-ui", "frontend-tools",
		"image-source", "nv-filters", "obs-browser", "obs-ffmpeg", "obs-filters", "obs-nvenc", "obs-outputs",
		"obs-qsv11", "obs-scripting", "obs-text", "obs-transitions", "obs-vst", "obs-webrtc", "obs-x264",
		"rtmp-services", "text-freetype2", "vlc-video", "win-capture", "win-dshow", "win-wasapi",
		// Helper programs and libraries that come with those modules.
		"cef", "cef-bootstrap", "chrome_100_percent", "chrome_200_percent", "chrome_elf", "d3dcompiler_47",
		"dxcompiler", "dxil", "get-graphics-offsets32", "get-graphics-offsets64", "graphics-hook32",
		"graphics-hook64", "icudtl", "inject-helper32", "inject-helper64", "libcef", "libegl", "libglesv2",
		"locales", "natives_blob", "obs-amf-test", "obs-browser-page", "obs-ffmpeg-mux", "obs-nvenc-test",
		"obs-qsv-test", "resources", "snapshot_blob", "swiftshader", "v8_context_snapshot", "vk_swiftshader",
		"vk_swiftshader_icd", "vulkan-1",
	};
	return stems;
}

void CollectPluginRoot(const PluginRoot &root, CollectionResult &result)
{
	std::error_code ec;
	if (root.archivePrefix.empty() || !std::filesystem::is_directory(root.dir, ec) || ec)
		return;

	CollectionResult inside;
	CollectFromEntry(root.dir, root.dir, inside);
	for (auto &file : inside.files) {
		if (IsExcludedByStem(root, file.relativePath))
			continue;
		file.relativePath = std::filesystem::path(root.archivePrefix) / file.relativePath;
		result.totalSizeBytes += file.sizeBytes;
		result.files.push_back(std::move(file));
	}
}

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
