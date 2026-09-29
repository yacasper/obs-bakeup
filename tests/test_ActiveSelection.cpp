// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include <catch2/catch_test_macros.hpp>

#include "core/ActiveSelection.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace obs_backuper;
namespace fs = std::filesystem;

namespace {

struct TempDir {
	fs::path root = fs::temp_directory_path() / "obs-backuper-activeselection-tests";
	TempDir()
	{
		fs::remove_all(root);
		fs::create_directories(root);
	}
	~TempDir()
	{
		std::error_code ec;
		fs::remove_all(root, ec);
	}
	void Write(const std::string &name, const std::string &text) const
	{
		std::ofstream(root / name, std::ios::binary) << text;
	}
};

} // namespace

TEST_CASE("ParseActiveSelection reads Profile and SceneCollection from [Basic]", "[ActiveSelection]")
{
	const auto selection = ParseActiveSelection("[General]\nProfile=Wrong\n\n[Basic]\nProfile=Stream\n"
						    "ProfileDir=Stream\nSceneCollection=My Scenes\n[Other]\nProfile=Nope\n");
	CHECK(selection.profile == "Stream");
	CHECK(selection.sceneCollection == "My Scenes");
}

TEST_CASE("ParseActiveSelection copes with CRLF, a byte order mark and non-ASCII names", "[ActiveSelection]")
{
	const std::string cyrillic = "\xD0\x9F\xD1\x80\xD0\xBE\xD1\x84\xD0\xB8\xD0\xBB\xD1\x8C";
	const auto selection = ParseActiveSelection("\xEF\xBB\xBF[Basic]\r\nProfile=" + cyrillic +
						    "\r\nSceneCollection=Scenes \r\n");
	CHECK(selection.profile == cyrillic);
	CHECK(selection.sceneCollection == "Scenes");
}

TEST_CASE("ParseActiveSelection gives empty values when nothing is recorded", "[ActiveSelection]")
{
	const auto selection = ParseActiveSelection("[General]\nFoo=bar\n");
	CHECK(selection.profile.empty());
	CHECK(selection.sceneCollection.empty());
	CHECK(ParseActiveSelection("").profile.empty());
}

TEST_CASE("ReadActiveSelection prefers user.ini and falls back to global.ini per key", "[ActiveSelection]")
{
	TempDir dir;
	dir.Write("user.ini", "[Basic]\nProfile=FromUser\n");
	dir.Write("global.ini", "[Basic]\nProfile=FromGlobal\nSceneCollection=FromGlobal\n");

	const auto selection = ReadActiveSelection(dir.root);
	CHECK(selection.profile == "FromUser");
	CHECK(selection.sceneCollection == "FromGlobal");
}

TEST_CASE("ReadActiveSelection handles missing files", "[ActiveSelection]")
{
	TempDir dir;
	const auto selection = ReadActiveSelection(dir.root);
	CHECK(selection.profile.empty());
	CHECK(selection.sceneCollection.empty());
	CHECK(ReadActiveSelection(dir.root / "does-not-exist").profile.empty());
}

TEST_CASE("ParseIniEntries keeps values exactly, including = and quotes", "[ActiveSelection]")
{
	const std::string json = "[{\"title\":\"Chat\",\"url\":\"http://x/?a=b\"}]";
	const auto entries = ParseIniEntries("[General]\r\nA=1\r\n\r\n; comment\n[BasicWindow]\nExtraBrowserDocks=" + json +
					     "\nEmpty=\nnot a pair\n=nokey\n");

	REQUIRE(entries.size() == 3);
	CHECK(entries[0].section == "General");
	CHECK(entries[0].key == "A");
	CHECK(entries[0].value == "1");
	CHECK(entries[1].section == "BasicWindow");
	CHECK(entries[1].key == "ExtraBrowserDocks");
	CHECK(entries[1].value == json);
	CHECK(entries[2].key == "Empty");
	CHECK(entries[2].value.empty());
}

TEST_CASE("ReadIniEntries reads a file and gives nothing for a missing one", "[ActiveSelection]")
{
	TempDir dir;
	dir.Write("user.ini", "[BasicWindow]\nDockState=abc\n");

	const auto entries = ReadIniEntries(dir.root, "user.ini");
	REQUIRE(entries.size() == 1);
	CHECK(entries[0].value == "abc");
	CHECK(ReadIniEntries(dir.root, "missing.ini").empty());
}
