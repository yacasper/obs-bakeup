// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

// Pins the parts of the Windows build that were verified by hand against a real
// OBS: which folders are backed up and how the release archive is laid out. A
// change made for another platform must not alter any of them unnoticed.
#include <catch2/catch_test_macros.hpp>

#include "core/ConfigPaths.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace obs_backuper;
namespace fs = std::filesystem;

namespace {

std::string ReadRepoFile(const std::string &relativePath)
{
	std::ifstream in(fs::path(OBS_BACKUPER_SOURCE_DIR) / relativePath, std::ios::binary);
	REQUIRE(in.is_open());
	std::ostringstream buffer;
	buffer << in.rdbuf();
	return buffer.str();
}

bool Contains(const std::string &text, const std::string &part)
{
	return text.find(part) != std::string::npos;
}

const PluginRoot *FindRoot(const std::vector<PluginRoot> &roots, const std::string &prefix)
{
	const auto it = std::find_if(roots.begin(), roots.end(), [&](const auto &r) { return r.archivePrefix == prefix; });
	return it == roots.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("A normal Windows install backs up the ProgramData plugins and the program folder", "[WindowsLayout]")
{
	const fs::path base = fs::path("C:/Program Files/obs-studio");
	const fs::path programData = fs::path("C:/ProgramData/obs-studio/plugins");

	const auto roots = ResolveWindowsPluginRoots(base, /*portableMode=*/false, programData);

	REQUIRE(roots.size() == 3);
	CHECK(roots[0].archivePrefix == "system-plugins");
	CHECK(roots[0].dir == programData);
	CHECK(roots[0].excludedStems.empty());

	REQUIRE(FindRoot(roots, "program-plugins-bin") != nullptr);
	CHECK(FindRoot(roots, "program-plugins-bin")->dir == base / "obs-plugins" / "64bit");
	REQUIRE(FindRoot(roots, "program-plugins-data") != nullptr);
	CHECK(FindRoot(roots, "program-plugins-data")->dir == base / "data" / "obs-plugins");
	CHECK(FindRoot(roots, "portable-plugins") == nullptr);
}

TEST_CASE("A portable Windows install backs up its own plugins folder and the program folder", "[WindowsLayout]")
{
	const fs::path base = fs::path("D:/OBS-portable");

	const auto roots = ResolveWindowsPluginRoots(base, /*portableMode=*/true, fs::path("C:/ProgramData/obs-studio/plugins"));

	REQUIRE(roots.size() == 3);
	CHECK(roots[0].archivePrefix == "portable-plugins");
	CHECK(roots[0].dir == base / "plugins");
	CHECK(FindRoot(roots, "system-plugins") == nullptr);
	CHECK(FindRoot(roots, "program-plugins-bin") != nullptr);
	CHECK(FindRoot(roots, "program-plugins-data") != nullptr);
}

TEST_CASE("Program-folder roots leave out what OBS ships and nothing else", "[WindowsLayout]")
{
	const auto roots = ResolveWindowsPluginRoots("C:/obs", false, fs::path("C:/pd"));

	for (const char *prefix : {"program-plugins-bin", "program-plugins-data"}) {
		const auto *root = FindRoot(roots, prefix);
		REQUIRE(root != nullptr);
		const auto has = [&](const std::string &stem) {
			return std::find(root->excludedStems.begin(), root->excludedStems.end(), stem) !=
			       root->excludedStems.end();
		};
		CHECK(has("obs-ffmpeg"));
		CHECK(has("win-capture"));
		CHECK(has("libcef"));
		CHECK_FALSE(has("obs-bakeup"));
		CHECK_FALSE(has("aitum-multistream"));
	}
}

TEST_CASE("Missing base folder or ProgramData path drops only the roots that need them", "[WindowsLayout]")
{
	const auto noBase = ResolveWindowsPluginRoots({}, false, fs::path("C:/pd"));
	REQUIRE(noBase.size() == 1);
	CHECK(noBase[0].archivePrefix == "system-plugins");

	CHECK(ResolveWindowsPluginRoots({}, true, fs::path("C:/pd")).empty());

	const auto noProgramData = ResolveWindowsPluginRoots("C:/obs", false, std::nullopt);
	REQUIRE(noProgramData.size() == 2);
	CHECK(FindRoot(noProgramData, "system-plugins") == nullptr);

	CHECK(ResolveWindowsPluginRoots("C:/obs", false, fs::path()).size() == 2);
}

TEST_CASE("The plugin keeps the name the Windows release was verified under", "[WindowsLayout][packaging]")
{
	CHECK(Contains(ReadRepoFile("buildspec.json"), "\"name\": \"obs-bakeup\""));
}

TEST_CASE("The Windows install step lays files out like an OBS folder", "[WindowsLayout][packaging]")
{
	const std::string helpers = ReadRepoFile("cmake/windows/helpers.cmake");

	// The module goes to obs-plugins/64bit and its data to data/obs-plugins/<name>,
	// so the release zip can be extracted straight into the OBS folder.
	CHECK(Contains(helpers, "RUNTIME DESTINATION \"obs-plugins/64bit\""));
	CHECK(Contains(helpers, "LIBRARY DESTINATION \"obs-plugins/64bit\""));
	CHECK(Contains(helpers, "DESTINATION \"obs-plugins/64bit\"\n    OPTIONAL"));
	CHECK(Contains(helpers, "DESTINATION \"data/obs-plugins/${target}\" USE_SOURCE_PERMISSIONS"));
	CHECK(Contains(helpers, "DESTINATION \"data/obs-plugins/${target}\" COMPONENT Runtime"));
	// The old nested layout must not come back.
	CHECK_FALSE(Contains(helpers, "\"${target}/bin/64bit\""));
	CHECK_FALSE(Contains(helpers, "\"${target}/data\""));
}

TEST_CASE("The Windows release zip is built from the install folder and named after the plugin", "[WindowsLayout][packaging]")
{
	const std::string build = ReadRepoFile(".github/scripts/Build-Windows.ps1");
	CHECK(Contains(build, "'--prefix', \"${ProjectRoot}/release/${Configuration}\""));

	const std::string package = ReadRepoFile(".github/scripts/Package-Windows.ps1");
	CHECK(Contains(package, "\"${ProductName}-${ProductVersion}-windows-${Target}\""));
	CHECK(Contains(package, "\"${ProjectRoot}/release/${Configuration}\""));
	CHECK(Contains(package, "Compress-Archive"));

	const std::string workflow = ReadRepoFile(".github/workflows/build.yaml");
	CHECK(Contains(workflow, "name: obs-bakeup-windows-x64-${{ github.sha }}"));
	CHECK(Contains(workflow, "release/obs-bakeup-*-windows-x64*.*"));
}

TEST_CASE("The Windows build is not affected by the macOS packaging settings", "[WindowsLayout][packaging]")
{
	// The bundle id belongs to macOS only; Windows takes name and version.
	const std::string spec = ReadRepoFile("buildspec.json");
	CHECK(Contains(spec, "\"platformConfig\""));
	CHECK(Contains(spec, "\"macos\""));
	CHECK(Contains(spec, "\"windows-x64\""));
}
