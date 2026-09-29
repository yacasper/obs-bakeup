// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include <catch2/catch_test_macros.hpp>

#include "core/PathUtf8.h"
#include "core/SceneCollectionSnapshot.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace obs_backuper;
namespace fs = std::filesystem;

namespace {

struct TempDir {
	fs::path root = fs::temp_directory_path() / "obs-backuper-scenesnapshot-tests";
	TempDir()
	{
		fs::remove_all(root);
		fs::create_directories(root / "basic" / "scenes");
	}
	~TempDir()
	{
		std::error_code ec;
		fs::remove_all(root, ec);
	}
	void Write(const std::string &name, const std::string &text) const
	{
		std::ofstream(root / "basic" / "scenes" / PathFromUtf8(name), std::ios::binary) << text;
	}
	std::string Read(const std::string &name) const
	{
		std::ifstream in(root / "basic" / "scenes" / PathFromUtf8(name), std::ios::binary);
		std::ostringstream buffer;
		buffer << in.rdbuf();
		return buffer.str();
	}
};

const SceneCollectionFile *Find(const std::vector<SceneCollectionFile> &files, const std::string &name)
{
	const auto it = std::find_if(files.begin(), files.end(), [&](const auto &f) { return f.fileName == name; });
	return it == files.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("SnapshotSceneCollections takes the .json files and skips the backups", "[SceneCollectionSnapshot]")
{
	TempDir dir;
	dir.Write("Stream.json", "{\"crop_right\":1098}");
	dir.Write("Other.JSON", "{}");
	dir.Write("Stream.json.bak", "old");
	dir.Write("Stream.json.v1", "older");

	const auto files = SnapshotSceneCollections(dir.root);

	REQUIRE(files.size() == 2);
	REQUIRE(Find(files, "Stream.json") != nullptr);
	CHECK(Find(files, "Stream.json")->bytes == "{\"crop_right\":1098}");
	CHECK(Find(files, "Other.JSON") != nullptr);
	CHECK(Find(files, "Stream.json.bak") == nullptr);
}

TEST_CASE("Written-back snapshots undo what OBS saved over the files", "[SceneCollectionSnapshot]")
{
	TempDir dir;
	dir.Write("Stream.json", "{\"crop_right\":1098}");
	const auto snapshot = SnapshotSceneCollections(dir.root);

	dir.Write("Stream.json", "{\"crop_right\":1464}"); // rescaled and saved by OBS
	CHECK(WriteSceneCollections(dir.root, snapshot) == 1);
	CHECK(dir.Read("Stream.json") == "{\"crop_right\":1098}");
}

TEST_CASE("Snapshots keep non-ASCII file names and arbitrary bytes", "[SceneCollectionSnapshot]")
{
	TempDir dir;
	const std::string name = "\xD0\xA1ute_Cats.json"; // Cyrillic "С" at the start
	const std::string bytes = std::string("a\0b\r\n\xFF", 6);
	dir.Write(name, bytes);

	const auto snapshot = SnapshotSceneCollections(dir.root);
	REQUIRE(snapshot.size() == 1);
	CHECK(snapshot[0].fileName == name);

	dir.Write(name, "changed");
	CHECK(WriteSceneCollections(dir.root, snapshot) == 1);
	CHECK(dir.Read(name) == bytes);
}

TEST_CASE("SnapshotSceneCollections copes with a missing folder and a size limit", "[SceneCollectionSnapshot]")
{
	TempDir dir;
	CHECK(SnapshotSceneCollections(dir.root / "nowhere").empty());
	CHECK(SnapshotSceneCollections(dir.root).empty());

	dir.Write("Big.json", std::string(100, 'x'));
	CHECK(SnapshotSceneCollections(dir.root, 50).empty());
	CHECK(SnapshotSceneCollections(dir.root, 100).size() == 1);
}

TEST_CASE("WriteSceneCollections creates the folder when it is gone", "[SceneCollectionSnapshot]")
{
	TempDir dir;
	fs::remove_all(dir.root / "basic");
	CHECK(WriteSceneCollections(dir.root, {{"A.json", "{}"}}) == 1);
	CHECK(dir.Read("A.json") == "{}");
}
