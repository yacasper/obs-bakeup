// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include <catch2/catch_test_macros.hpp>

#include "core/ProjectLinks.h"
#include "core/UpdateCheck.h"

#include <algorithm>
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

	// A Windows checkout may turn line ends into CRLF; compare content only.
	std::string text = buffer.str();
	text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
	return text;
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

TEST_CASE("The GitHub Sponsor button and the issue chooser use the same support page as the dialog", "[ProjectLinks][github]")
{
	const std::string funding = ReadRepoFile(".github/FUNDING.yml");
	CHECK(funding.find("custom: [\"" + std::string(kSupportUrl) + "\"]") != std::string::npos);

	const std::string chooser = ReadRepoFile(".github/ISSUE_TEMPLATE/config.yml");
	CHECK(chooser.find(kSupportUrl) != std::string::npos);
	// Reports must go through a form, so they arrive with the details needed to act on them.
	CHECK(chooser.find("blank_issues_enabled: false") != std::string::npos);
}

TEST_CASE("The bug report form asks for what a fix needs", "[ProjectLinks][github]")
{
	const std::string form = ReadRepoFile(".github/ISSUE_TEMPLATE/bug_report.yml");

	for (const char *field : {"id: plugin_version", "id: obs_version", "id: system", "id: install_kind", "id: action",
				  "id: what_happened", "id: steps", "id: logs", "id: checks"}) {
		INFO(field);
		CHECK(form.find(field) != std::string::npos);
	}
	// The essentials cannot be skipped.
	for (const char *field : {"plugin_version", "obs_version", "system", "what_happened"}) {
		const auto at = form.find(std::string("id: ") + field);
		REQUIRE(at != std::string::npos);
		const auto next = form.find("  - type:", at);
		const std::string block = form.substr(at, next == std::string::npos ? std::string::npos : next - at);
		INFO(field);
		CHECK(block.find("required: true") != std::string::npos);
	}
	// Secrets must not be posted, and the form says so twice: in the intro and as a required check.
	CHECK(form.find("stream keys") != std::string::npos);
	CHECK(form.find("I removed stream keys, tokens and passwords") != std::string::npos);
	// Where to find the logs on both systems.
	CHECK(form.find("%APPDATA%") != std::string::npos);
	CHECK(form.find("Library/Application Support/obs-studio/logs") != std::string::npos);
}

TEST_CASE("Both issue forms are complete forms for GitHub", "[ProjectLinks][github]")
{
	for (const char *file : {".github/ISSUE_TEMPLATE/bug_report.yml", ".github/ISSUE_TEMPLATE/feature_request.yml"}) {
		const std::string form = ReadRepoFile(file);
		INFO(file);
		CHECK(form.rfind("name: ", 0) == 0);
		CHECK(form.find("\ndescription: ") != std::string::npos);
		CHECK(form.find("\nbody:\n") != std::string::npos);
		CHECK(form.find("\nlabels: ") != std::string::npos);
	}
}
