// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include <catch2/catch_test_macros.hpp>

#include "core/ProjectLinks.h"
#include "core/UpdateCheck.h"

#include <algorithm>
#include <cstdint>
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

namespace {

std::string ReadRepoBinary(const std::string &relativePath)
{
	std::ifstream in(std::string(OBS_BACKUPER_SOURCE_DIR) + "/" + relativePath, std::ios::binary);
	REQUIRE(in.is_open());
	std::ostringstream buffer;
	buffer << in.rdbuf();
	return buffer.str();
}

std::uint32_t BigEndian32(const std::string &bytes, std::size_t offset)
{
	const auto b = [&](std::size_t i) { return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + i])); };
	return (b(0) << 24) | (b(1) << 16) | (b(2) << 8) | b(3);
}

} // namespace

TEST_CASE("The social preview image is what GitHub asks for", "[ProjectLinks][socialpreview]")
{
	const std::string png = ReadRepoBinary("assets/social-preview.png");

	// PNG signature, then the IHDR chunk with the width and height.
	REQUIRE(png.size() > 24);
	CHECK(png.compare(0, 8, std::string("\x89PNG\r\n\x1a\n", 8)) == 0);
	CHECK(png.compare(12, 4, "IHDR") == 0);
	CHECK(BigEndian32(png, 16) == 1280);
	CHECK(BigEndian32(png, 20) == 640);
	// GitHub rejects social previews of 1 MB or more.
	CHECK(png.size() < 1000 * 1000);
}

TEST_CASE("The card template keeps its size, its defaults and its safety", "[ProjectLinks][socialpreview]")
{
	const std::string card = ReadRepoFile("assets/social-preview/card.html");

	CHECK(card.find("--width: 1280px;") != std::string::npos);
	CHECK(card.find("--height: 640px;") != std::string::npos);
	// The defaults are the repository card.
	CHECK(card.find("title: \"OBS Bakeup\"") != std::string::npos);
	CHECK(card.find("credit: \"Chill Pixel Bakery\"") != std::string::npos);
	CHECK(card.find("image: \"../dialog.png\"") != std::string::npos);
	// The croissant from the README's heading comes before the title, in its own element:
	// inside the gradient text a colour emoji would lose its colour.
	CHECK(card.find("icon: \"\\u{1F950}\"") != std::string::npos);
	CHECK(card.find("<h1><span class=\"icon\" id=\"icon\"></span><span class=\"title\" id=\"title\"></span></h1>") !=
	      std::string::npos);
	CHECK(card.find("document.getElementById(\"icon\").textContent = value(\"icon\")") != std::string::npos);
	// Every parameter is optional and read from the address.
	for (const char *parameter : {"tag", "icon", "title", "subtitle", "chips", "footer", "credit", "image"})
		CHECK(card.find(std::string("  ") + parameter + ": ") != std::string::npos);
	// Texts from the address are inserted as plain text, never as markup.
	CHECK(card.find("innerHTML") == std::string::npos);
	CHECK(card.find("insertAdjacentHTML") == std::string::npos);
	CHECK(card.find("document.write") == std::string::npos);
	CHECK(card.find(".textContent = value(") != std::string::npos);
}

TEST_CASE("The screenshot the card shows exists", "[ProjectLinks][socialpreview]")
{
	const std::string screenshot = ReadRepoBinary("assets/dialog.png");
	REQUIRE(screenshot.size() > 24);
	CHECK(screenshot.compare(1, 3, "PNG") == 0);
	// README shows the same picture.
	CHECK(ReadRepoFile("README.md").find("assets/dialog.png") != std::string::npos);
}

TEST_CASE("The export script renders the template at the card size", "[ProjectLinks][socialpreview]")
{
	const std::string script = ReadRepoFile("assets/social-preview/export.sh");

	CHECK(script.find("--window-size=1280,640") != std::string::npos);
	CHECK(script.find("--force-device-scale-factor=1") != std::string::npos);
	CHECK(script.find("card.html") != std::string::npos);
	CHECK(script.find("social-preview.png") != std::string::npos);
}
