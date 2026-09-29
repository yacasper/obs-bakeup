// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "core/ZipArchive.h"

#include <catch2/catch_test_macros.hpp>
#include <miniz.h>
#include <miniz_zip.h>

#include <fstream>

using namespace obs_backuper;

namespace {

class TempDirFixture {
public:
	TempDirFixture() : root(std::filesystem::temp_directory_path() / "obs-backuper-ziptests" / UniqueName())
	{
		std::filesystem::create_directories(root);
	}

	~TempDirFixture() { std::filesystem::remove_all(root.parent_path()); }

	const std::filesystem::path root;

private:
	static std::string UniqueName()
	{
		static int counter = 0;
		return "fixture-" + std::to_string(++counter);
	}
};

std::filesystem::path WriteSourceFile(const std::filesystem::path &path, std::string_view content)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream file(path, std::ios::binary);
	file << content;
	return path;
}

// Reads one entry's content back out of a written archive, for assertions.
std::string ReadZipEntry(const std::filesystem::path &zipPath, const std::string &entryName)
{
	mz_zip_archive reader{};
	REQUIRE(mz_zip_reader_init_file(&reader, zipPath.string().c_str(), 0));

	size_t size = 0;
	void *data = mz_zip_reader_extract_file_to_heap(&reader, entryName.c_str(), &size, 0);
	REQUIRE(data != nullptr);

	std::string result(static_cast<const char *>(data), size);
	mz_free(data);
	mz_zip_reader_end(&reader);
	return result;
}

mz_uint CountZipEntries(const std::filesystem::path &zipPath)
{
	mz_zip_archive reader{};
	REQUIRE(mz_zip_reader_init_file(&reader, zipPath.string().c_str(), 0));
	const mz_uint count = mz_zip_reader_get_num_files(&reader);
	mz_zip_reader_end(&reader);
	return count;
}

} // namespace

TEST_CASE("ZipArchive writes files and in-memory data that can be read back", "[ZipArchive]")
{
	TempDirFixture fixture;
	const auto sourceFile = WriteSourceFile(fixture.root / "source" / "global.ini", "size=1\n");
	const auto zipPath = fixture.root / "out.zip";

	{
		ZipArchive archive(zipPath);
		std::string error;
		REQUIRE(archive.Open(error));
		REQUIRE(archive.AddFile(sourceFile, "global.ini", error));
		REQUIRE(archive.AddData("{\"a\":1}", "manifest.json", error));
		REQUIRE(archive.Close(error));
	}

	REQUIRE(std::filesystem::exists(zipPath));
	CHECK(CountZipEntries(zipPath) == 2);
	CHECK(ReadZipEntry(zipPath, "global.ini") == "size=1\n");
	CHECK(ReadZipEntry(zipPath, "manifest.json") == "{\"a\":1}");
}

TEST_CASE("ZipArchive preserves nested relative paths with forward slashes", "[ZipArchive]")
{
	TempDirFixture fixture;
	const auto sourceFile = WriteSourceFile(fixture.root / "source" / "scene.json", "{}");
	const auto zipPath = fixture.root / "out.zip";

	{
		ZipArchive archive(zipPath);
		std::string error;
		REQUIRE(archive.Open(error));
		REQUIRE(archive.AddFile(sourceFile, std::filesystem::path("basic") / "scenes" / "scene.json", error));
		REQUIRE(archive.Close(error));
	}

	CHECK(ReadZipEntry(zipPath, "basic/scenes/scene.json") == "{}");
}

TEST_CASE("ZipArchive::AddFile fails for a source file that does not exist", "[ZipArchive]")
{
	TempDirFixture fixture;
	const auto zipPath = fixture.root / "out.zip";

	ZipArchive archive(zipPath);
	std::string error;
	REQUIRE(archive.Open(error));

	error.clear();
	CHECK_FALSE(archive.AddFile(fixture.root / "does-not-exist.ini", "does-not-exist.ini", error));
	CHECK_FALSE(error.empty());

	REQUIRE(archive.Close(error));
}

TEST_CASE("ZipArchive::Open fails for a destination directory that does not exist", "[ZipArchive]")
{
	ZipArchive archive("/this/path/does/not/exist-obs-backuper-test/out.zip");
	std::string error;
	CHECK_FALSE(archive.Open(error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("ZipReader lists entries and reads them back", "[ZipReader]")
{
	TempDirFixture fixture;
	const auto zipPath = fixture.root / "out.zip";

	{
		ZipArchive archive(zipPath);
		std::string error;
		REQUIRE(archive.Open(error));
		REQUIRE(archive.AddData("size=1\n", "global.ini", error));
		REQUIRE(archive.AddFile(WriteSourceFile(fixture.root / "source" / "scene.json", "{}"),
					 std::filesystem::path("basic") / "scenes" / "scene.json", error));
		REQUIRE(archive.Close(error));
	}

	ZipReader reader(zipPath);
	std::string error;
	REQUIRE(reader.Open(error));

	const auto entries = reader.ListEntries();
	REQUIRE(entries.size() == 2);

	std::string data;
	REQUIRE(reader.ReadEntryToString("global.ini", data, error));
	CHECK(data == "size=1\n");

	REQUIRE(reader.ReadEntryToString("basic/scenes/scene.json", data, error));
	CHECK(data == "{}");
}

TEST_CASE("ZipReader::ExtractEntryToFile writes the entry to disk, creating parent directories", "[ZipReader]")
{
	TempDirFixture fixture;
	const auto zipPath = fixture.root / "out.zip";

	{
		ZipArchive archive(zipPath);
		std::string error;
		REQUIRE(archive.Open(error));
		REQUIRE(archive.AddData("{}", std::filesystem::path("basic") / "scenes" / "scene.json", error));
		REQUIRE(archive.Close(error));
	}

	ZipReader reader(zipPath);
	std::string error;
	REQUIRE(reader.Open(error));

	const auto destination = fixture.root / "restored" / "basic" / "scenes" / "scene.json";
	REQUIRE(reader.ExtractEntryToFile("basic/scenes/scene.json", destination, error));

	std::ifstream file(destination, std::ios::binary);
	const std::string content{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
	CHECK(content == "{}");
}

TEST_CASE("ZipReader::ReadEntryToString fails for an entry that does not exist", "[ZipReader]")
{
	TempDirFixture fixture;
	const auto zipPath = fixture.root / "out.zip";

	{
		ZipArchive archive(zipPath);
		std::string error;
		REQUIRE(archive.Open(error));
		REQUIRE(archive.AddData("size=1\n", "global.ini", error));
		REQUIRE(archive.Close(error));
	}

	ZipReader reader(zipPath);
	std::string error;
	REQUIRE(reader.Open(error));

	std::string data;
	CHECK_FALSE(reader.ReadEntryToString("does-not-exist.ini", data, error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("ZipReader::Open fails for a file that does not exist", "[ZipReader]")
{
	ZipReader reader("/this/path/does/not/exist-obs-backuper-test/out.zip");
	std::string error;
	CHECK_FALSE(reader.Open(error));
	CHECK_FALSE(error.empty());
}
