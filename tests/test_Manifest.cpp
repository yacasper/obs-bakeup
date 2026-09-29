// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "core/Manifest.h"

#include <catch2/catch_test_macros.hpp>

using namespace obs_backuper;

namespace {

ManifestInfo SampleManifestInfo()
{
	ManifestInfo info;
	info.pluginVersion = "1.0.0";
	info.createdAtIso8601 = "2026-09-28T22:00:00Z";
	info.sourceOs = "windows";
	info.sourceOsVersion = "10.0.19045";
	info.obsVersion = "30.2.0";
	info.includedSections = {"global.ini", "basic", "plugin_config"};
	info.excludedSections = {"logs", "crashes"};
	return info;
}

} // namespace

TEST_CASE("BuildManifestJson includes all scalar fields", "[Manifest]")
{
	const std::string json = BuildManifestJson(SampleManifestInfo());

	CHECK(json.find("\"backup_format_version\": 1") != std::string::npos);
	CHECK(json.find("\"plugin_version\": \"1.0.0\"") != std::string::npos);
	CHECK(json.find("\"created_at\": \"2026-09-28T22:00:00Z\"") != std::string::npos);
	CHECK(json.find("\"source_os\": \"windows\"") != std::string::npos);
	CHECK(json.find("\"source_os_version\": \"10.0.19045\"") != std::string::npos);
	CHECK(json.find("\"obs_version\": \"30.2.0\"") != std::string::npos);
}

TEST_CASE("BuildManifestJson serializes section arrays", "[Manifest]")
{
	const std::string json = BuildManifestJson(SampleManifestInfo());

	CHECK(json.find("\"included_sections\": [\"global.ini\", \"basic\", \"plugin_config\"]") != std::string::npos);
	CHECK(json.find("\"excluded_sections\": [\"logs\", \"crashes\"]") != std::string::npos);
}

TEST_CASE("BuildManifestJson escapes special characters in string fields", "[Manifest]")
{
	ManifestInfo info = SampleManifestInfo();
	info.pluginVersion = "1.0.0-\"beta\"\\test\nline";

	const std::string json = BuildManifestJson(info);

	CHECK(json.find("1.0.0-\\\"beta\\\"\\\\test\\nline") != std::string::npos);
	// The raw unescaped quote must not appear on its own inside the value.
	CHECK(json.find("\"1.0.0-\"beta\"") == std::string::npos);
}

TEST_CASE("BuildManifestJson handles empty section lists and empty os version", "[Manifest]")
{
	ManifestInfo info = SampleManifestInfo();
	info.sourceOsVersion.clear();
	info.includedSections.clear();
	info.excludedSections.clear();

	const std::string json = BuildManifestJson(info);

	CHECK(json.find("\"source_os_version\": \"\"") != std::string::npos);
	CHECK(json.find("\"included_sections\": []") != std::string::npos);
	CHECK(json.find("\"excluded_sections\": []") != std::string::npos);
}

TEST_CASE("ParseManifestJson round-trips everything BuildManifestJson writes", "[Manifest]")
{
	const ManifestInfo original = SampleManifestInfo();
	const std::string json = BuildManifestJson(original);

	ManifestInfo parsed;
	std::string error;
	REQUIRE(ParseManifestJson(json, parsed, error));
	CHECK(error.empty());

	CHECK(parsed.backupFormatVersion == original.backupFormatVersion);
	CHECK(parsed.pluginVersion == original.pluginVersion);
	CHECK(parsed.createdAtIso8601 == original.createdAtIso8601);
	CHECK(parsed.sourceOs == original.sourceOs);
	CHECK(parsed.sourceOsVersion == original.sourceOsVersion);
	CHECK(parsed.obsVersion == original.obsVersion);
	CHECK(parsed.includedSections == original.includedSections);
	CHECK(parsed.excludedSections == original.excludedSections);
}

TEST_CASE("ParseManifestJson round-trips escaped characters and empty arrays", "[Manifest]")
{
	ManifestInfo original = SampleManifestInfo();
	original.pluginVersion = "1.0.0-\"beta\"\\test\nline";
	original.sourceOsVersion.clear();
	original.includedSections.clear();
	original.excludedSections.clear();

	ManifestInfo parsed;
	std::string error;
	REQUIRE(ParseManifestJson(BuildManifestJson(original), parsed, error));

	CHECK(parsed.pluginVersion == original.pluginVersion);
	CHECK(parsed.sourceOsVersion.empty());
	CHECK(parsed.includedSections.empty());
	CHECK(parsed.excludedSections.empty());
}

TEST_CASE("ParseManifestJson fails when backup_format_version is missing", "[Manifest]")
{
	ManifestInfo parsed;
	std::string error;
	CHECK_FALSE(ParseManifestJson("{\"plugin_version\": \"1.0.0\"}", parsed, error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("ParseManifestJson fails on input that isn't JSON at all", "[Manifest]")
{
	ManifestInfo parsed;
	std::string error;
	CHECK_FALSE(ParseManifestJson("not json content", parsed, error));
	CHECK_FALSE(error.empty());
}
