// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "core/PluginSettings.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

using namespace obs_backuper;

namespace {

class TempDirFixture {
public:
	TempDirFixture() : root(std::filesystem::temp_directory_path() / "obs-backuper-settingstests")
	{
		std::filesystem::remove_all(root);
		std::filesystem::create_directories(root);
	}
	~TempDirFixture() { std::filesystem::remove_all(root); }

	const std::filesystem::path root;
};

void WriteRaw(const std::filesystem::path &path, const std::string &content)
{
	std::ofstream out(path, std::ios::binary);
	out << content;
}

} // namespace

TEST_CASE("PluginSettings::Load returns defaults when the file does not exist", "[PluginSettings]")
{
	TempDirFixture fixture;

	const auto settings = PluginSettings::Load(fixture.root / "missing.ini");

	CHECK_FALSE(settings.sensitiveWarningDismissed);
	CHECK(settings.lastBackupIso8601.empty());
	CHECK(settings.lastBackupPath.empty());
}

TEST_CASE("PluginSettings round-trips through Save and Load", "[PluginSettings]")
{
	TempDirFixture fixture;
	const auto path = fixture.root / "settings.ini";

	PluginSettings original;
	original.sensitiveWarningDismissed = true;
	original.lastBackupIso8601 = "2026-09-29T12:34:56Z";
	original.lastBackupPath = "/Users/me/Backups/obs-backup_2026-09-29_1234.zip";

	std::string error;
	REQUIRE(original.Save(path, error));

	const auto loaded = PluginSettings::Load(path);
	CHECK(loaded.sensitiveWarningDismissed);
	CHECK(loaded.lastBackupIso8601 == original.lastBackupIso8601);
	CHECK(loaded.lastBackupPath == original.lastBackupPath);
}

TEST_CASE("PluginSettings::Save creates missing parent directories", "[PluginSettings]")
{
	TempDirFixture fixture;
	const auto path = fixture.root / "a" / "b" / "settings.ini";

	std::string error;
	REQUIRE(PluginSettings{}.Save(path, error));
	CHECK(std::filesystem::exists(path));
}

TEST_CASE("PluginSettings::Save overwrites the previous content", "[PluginSettings]")
{
	TempDirFixture fixture;
	const auto path = fixture.root / "settings.ini";

	PluginSettings first;
	first.sensitiveWarningDismissed = true;
	first.lastBackupIso8601 = "2026-01-01T00:00:00Z";
	std::string error;
	REQUIRE(first.Save(path, error));

	PluginSettings second; // all defaults
	REQUIRE(second.Save(path, error));

	const auto loaded = PluginSettings::Load(path);
	CHECK_FALSE(loaded.sensitiveWarningDismissed);
	CHECK(loaded.lastBackupIso8601.empty());
}

TEST_CASE("PluginSettings::Save reports failure when the path is unwritable", "[PluginSettings]")
{
	TempDirFixture fixture;
	// A directory occupies the file's path, so it cannot be opened for writing.
	const auto path = fixture.root / "settings.ini";
	std::filesystem::create_directories(path);

	std::string error;
	CHECK_FALSE(PluginSettings{}.Save(path, error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("PluginSettings::Load tolerates garbage, unknown keys and CRLF line endings", "[PluginSettings]")
{
	TempDirFixture fixture;
	const auto path = fixture.root / "settings.ini";
	WriteRaw(path, "this line has no equals sign\r\n"
		       "unknownKey=whatever\r\n"
		       "sensitiveWarningDismissed=1\r\n"
		       "lastBackupIso8601=2026-09-29T00:00:00Z\r\n"
		       "=no key\r\n");

	const auto settings = PluginSettings::Load(path);

	CHECK(settings.sensitiveWarningDismissed);
	CHECK(settings.lastBackupIso8601 == "2026-09-29T00:00:00Z");
}

TEST_CASE("PluginSettings::Load treats any value other than 1 as not dismissed", "[PluginSettings]")
{
	TempDirFixture fixture;
	const auto path = fixture.root / "settings.ini";
	WriteRaw(path, "sensitiveWarningDismissed=true\n");

	CHECK_FALSE(PluginSettings::Load(path).sensitiveWarningDismissed);
}

TEST_CASE("PluginSettings::Save strips newlines so a value cannot inject extra keys", "[PluginSettings]")
{
	TempDirFixture fixture;
	const auto path = fixture.root / "settings.ini";

	PluginSettings settings;
	settings.lastBackupPath = "/tmp/x\nsensitiveWarningDismissed=1";
	std::string error;
	REQUIRE(settings.Save(path, error));

	const auto loaded = PluginSettings::Load(path);
	CHECK_FALSE(loaded.sensitiveWarningDismissed);
}

TEST_CASE("PluginSettings keeps non-ASCII paths intact", "[PluginSettings]")
{
	TempDirFixture fixture;
	const auto path = fixture.root / "settings.ini";

	PluginSettings settings;
	settings.lastBackupPath = "C:\\Users\\Пользователь\\Бэкапы\\obs-backup.zip";
	std::string error;
	REQUIRE(settings.Save(path, error));

	CHECK(PluginSettings::Load(path).lastBackupPath == settings.lastBackupPath);
}
