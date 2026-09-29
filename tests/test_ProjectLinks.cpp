// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include <catch2/catch_test_macros.hpp>

#include "core/ProjectLinks.h"
#include "core/UpdateCheck.h"

#include <fstream>
#include <sstream>
#include <string>

using namespace obs_backuper;

namespace {

std::string ReadRepoFile(const std::string &relativePath)
{
	std::ifstream in(std::string(OBS_BACKUPER_SOURCE_DIR) + "/" + relativePath, std::ios::binary);
	REQUIRE(in.is_open());
	std::ostringstream buffer;
	buffer << in.rdbuf();
	return buffer.str();
}

bool StartsWith(const std::string &text, const std::string &prefix)
{
	return text.rfind(prefix, 0) == 0;
}

} // namespace

TEST_CASE("The links shown in the dialog are the intended ones", "[ProjectLinks]")
{
	CHECK(std::string(kAuthorName) == "Chill Pixel Bakery");
	CHECK(std::string(kAuthorUrl) == "https://www.youtube.com/@ChillPixelBakery");
	CHECK(std::string(kSupportUrl) == "https://destream.net/live/Chillcody");
	CHECK(std::string(kProjectUrl) == "https://github.com/yacasper/obs-bakeup");
}

TEST_CASE("Every link is https and free of characters that would break an HTML attribute", "[ProjectLinks]")
{
	const std::string urls[] = {kAuthorUrl, kSupportUrl, kProjectUrl};
	for (const std::string &url : urls) {
		INFO(url);
		CHECK(StartsWith(url, "https://"));
		CHECK(url.find('"') == std::string::npos);
		CHECK(url.find('<') == std::string::npos);
		CHECK(url.find(' ') == std::string::npos);
	}
}

TEST_CASE("The update check and the release page point at the project repository", "[ProjectLinks]")
{
	CHECK(StartsWith(kLatestReleaseApiUrl, "https://api.github.com/repos/yacasper/obs-bakeup/"));
	CHECK(StartsWith(kReleasesPageUrl, std::string(kProjectUrl) + "/"));
}

TEST_CASE("The README links to the same author and support pages as the dialog", "[ProjectLinks]")
{
	const std::string readme = ReadRepoFile("README.md");
	CHECK(readme.find(kAuthorUrl) != std::string::npos);
	CHECK(readme.find(kSupportUrl) != std::string::npos);
	CHECK(readme.find(kProjectUrl) != std::string::npos);
}
