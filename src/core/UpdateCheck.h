// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <optional>
#include <string>

namespace obs_backuper {

// Pure logic behind the "new version available" notice (no Qt, no network):
// the plugin asks GitHub for its latest release, and these helpers decide
// whether what came back is newer than the running build. The network call
// itself lives in src/worker/UpdateChecker.*.

// GitHub API endpoint for the latest published (non-draft, non-prerelease)
// release, and the page the notice links to.
inline constexpr const char *kLatestReleaseApiUrl = "https://api.github.com/repos/yacasper/obs-bakeup/releases/latest";
inline constexpr const char *kReleasesPageUrl = "https://github.com/yacasper/obs-bakeup/releases/latest";

struct Version {
	int major = 0;
	int minor = 0;
	int patch = 0;
	bool prerelease = false; // had a "-suffix" (e.g. "1.2.0-beta.1")

	std::string ToString() const; // "1.2.3", without a leading "v" or suffix
};

// Accepts "1", "1.2", "1.2.3", each optionally prefixed with "v"/"V" and
// followed by a "-prerelease" or "+build" suffix. Anything else -> nullopt.
std::optional<Version> ParseVersion(const std::string &text);

// True if `remote` is strictly newer than `current`. A prerelease is older
// than the release with the same numbers.
bool IsNewer(const Version &remote, const Version &current);

// Pulls "tag_name" out of a GitHub "latest release" JSON response body.
// nullopt if it is missing or not a string.
std::optional<std::string> ExtractTagName(const std::string &jsonBody);

// Convenience for the whole decision: the remote version if the response body
// describes a release newer than `currentVersion`, otherwise nullopt (also for
// any unparsable input -- an update check must never produce false alarms).
std::optional<Version> FindNewerVersion(const std::string &jsonBody, const std::string &currentVersion);

} // namespace obs_backuper
