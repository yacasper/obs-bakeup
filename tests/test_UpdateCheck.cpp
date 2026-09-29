// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "core/UpdateCheck.h"

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

using namespace obs_backuper;

TEST_CASE("ParseVersion accepts common version spellings", "[updatecheck]")
{
	auto v = ParseVersion("1.2.3");
	REQUIRE(v);
	CHECK(v->major == 1);
	CHECK(v->minor == 2);
	CHECK(v->patch == 3);
	CHECK_FALSE(v->prerelease);

	v = ParseVersion("v0.10.4");
	REQUIRE(v);
	CHECK(v->ToString() == "0.10.4");

	v = ParseVersion("V2");
	REQUIRE(v);
	CHECK(v->ToString() == "2.0.0");

	v = ParseVersion("1.5");
	REQUIRE(v);
	CHECK(v->ToString() == "1.5.0");
}

TEST_CASE("ParseVersion handles prerelease and build suffixes", "[updatecheck]")
{
	auto v = ParseVersion("1.2.0-beta.1");
	REQUIRE(v);
	CHECK(v->prerelease);
	CHECK(v->ToString() == "1.2.0");

	v = ParseVersion("1.2.0+build5");
	REQUIRE(v);
	CHECK_FALSE(v->prerelease);
}

TEST_CASE("ParseVersion rejects garbage", "[updatecheck]")
{
	CHECK_FALSE(ParseVersion(""));
	CHECK_FALSE(ParseVersion("v"));
	CHECK_FALSE(ParseVersion("latest"));
	CHECK_FALSE(ParseVersion("1."));
	CHECK_FALSE(ParseVersion("1..2"));
	CHECK_FALSE(ParseVersion("1.2.3.4"));
	CHECK_FALSE(ParseVersion("1.2x"));
	CHECK_FALSE(ParseVersion(".1"));
	CHECK_FALSE(ParseVersion("99999999999.0.0"));
}

TEST_CASE("IsNewer compares numerically, not as text", "[updatecheck]")
{
	CHECK(IsNewer(*ParseVersion("1.10.0"), *ParseVersion("1.9.0")));
	CHECK(IsNewer(*ParseVersion("2.0.0"), *ParseVersion("1.99.99")));
	CHECK(IsNewer(*ParseVersion("0.1.1"), *ParseVersion("0.1.0")));
	CHECK_FALSE(IsNewer(*ParseVersion("1.9.0"), *ParseVersion("1.10.0")));
	CHECK_FALSE(IsNewer(*ParseVersion("1.2.3"), *ParseVersion("1.2.3")));
}

TEST_CASE("IsNewer treats a prerelease as older than its release", "[updatecheck]")
{
	CHECK(IsNewer(*ParseVersion("1.2.0"), *ParseVersion("1.2.0-beta")));
	CHECK_FALSE(IsNewer(*ParseVersion("1.2.0-beta"), *ParseVersion("1.2.0")));
	CHECK_FALSE(IsNewer(*ParseVersion("1.2.0-beta"), *ParseVersion("1.2.0-beta")));
}

TEST_CASE("ExtractTagName reads the field from a release response", "[updatecheck]")
{
	const std::string body = R"({"url":"x","html_url":"y","tag_name":"v1.4.0","name":"Release","draft":false})";
	CHECK(ExtractTagName(body) == "v1.4.0");

	CHECK(ExtractTagName("{ \"tag_name\" :\n \"v2\" }") == "v2");
	CHECK(ExtractTagName(R"({"tag_name":"v1\"x"})") == "v1\"x");
}

TEST_CASE("ExtractTagName rejects malformed or unrelated bodies", "[updatecheck]")
{
	CHECK_FALSE(ExtractTagName(""));
	CHECK_FALSE(ExtractTagName("{}"));
	CHECK_FALSE(ExtractTagName(R"({"message":"Not Found"})"));
	CHECK_FALSE(ExtractTagName(R"({"tag_name":null})"));
	CHECK_FALSE(ExtractTagName(R"({"tag_name":12})"));
	CHECK_FALSE(ExtractTagName(R"({"tag_name":"v1.0)"));
	CHECK_FALSE(ExtractTagName(R"({"tag_name" "v1"})"));
	CHECK_FALSE(ExtractTagName(R"({"tag_name":"v1\)"));
}

TEST_CASE("FindNewerVersion decides from a whole response", "[updatecheck]")
{
	const std::string body = R"({"tag_name":"v0.2.0"})";

	auto newer = FindNewerVersion(body, "0.1.0");
	REQUIRE(newer);
	CHECK(newer->ToString() == "0.2.0");

	CHECK_FALSE(FindNewerVersion(body, "0.2.0"));
	CHECK_FALSE(FindNewerVersion(body, "0.3.0"));
}

TEST_CASE("FindNewerVersion never raises a false alarm on bad input", "[updatecheck]")
{
	CHECK_FALSE(FindNewerVersion("", "0.1.0"));
	CHECK_FALSE(FindNewerVersion(R"({"message":"rate limit exceeded"})", "0.1.0"));
	CHECK_FALSE(FindNewerVersion(R"({"tag_name":"nightly"})", "0.1.0"));
	CHECK_FALSE(FindNewerVersion(R"({"tag_name":"v9.0.0"})", "unknown"));
}

TEST_CASE("The release links point at the plugin's GitHub repository", "[updatecheck]")
{
	CHECK(std::string(kLatestReleaseApiUrl).rfind("https://api.github.com/repos/yacasper/obs-bakeup/", 0) == 0);
	CHECK(std::string(kReleasesPageUrl).rfind("https://github.com/yacasper/obs-bakeup/", 0) == 0);
}

TEST_CASE("LatestReleaseCurlArguments builds a safe request for the latest release", "[UpdateCheck][curl]")
{
	const auto args = LatestReleaseCurlArguments("obs-bakeup/0.1.1", 5);

	const auto has = [&](const std::string &value) { return std::find(args.begin(), args.end(), value) != args.end(); };
	const auto after = [&](const std::string &flag) {
		const auto it = std::find(args.begin(), args.end(), flag);
		return it == args.end() || it + 1 == args.end() ? std::string() : *(it + 1);
	};

	// Quiet on success, an HTTP error status is a failure, redirects are followed.
	CHECK(has("--silent"));
	CHECK(has("--fail"));
	CHECK(has("--location"));
	CHECK(after("--max-time") == "5");
	// GitHub rejects requests without a User-Agent.
	CHECK(after("--user-agent") == "obs-bakeup/0.1.1");
	CHECK(after("--header") == "Accept: application/vnd.github+json");
	// The address is the last argument, so nothing can be read as a flag after it.
	CHECK(args.back() == kLatestReleaseApiUrl);
	CHECK(std::count(args.begin(), args.end(), std::string(kLatestReleaseApiUrl)) == 1);
}

TEST_CASE("LatestReleaseCurlArguments passes the timeout and a user agent with spaces as single arguments",
	  "[UpdateCheck][curl]")
{
	const auto args = LatestReleaseCurlArguments("my agent/1 (x)", 12);

	const auto it = std::find(args.begin(), args.end(), "--user-agent");
	REQUIRE(it != args.end());
	CHECK(*(it + 1) == "my agent/1 (x)");
	const auto timeout = std::find(args.begin(), args.end(), "--max-time");
	REQUIRE(timeout != args.end());
	CHECK(*(timeout + 1) == "12");
}
