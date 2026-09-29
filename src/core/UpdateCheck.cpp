// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "UpdateCheck.h"

#include <cctype>
#include <tuple>

namespace obs_backuper {

namespace {

// Parses an unsigned decimal number starting at `pos`; advances `pos`.
bool ParseNumber(const std::string &text, size_t &pos, int &out)
{
	const size_t start = pos;
	long long value = 0;
	while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
		value = value * 10 + (text[pos] - '0');
		if (value > 1000000) // absurd -> not a real version
			return false;
		++pos;
	}
	if (pos == start)
		return false;
	out = static_cast<int>(value);
	return true;
}

} // namespace

std::string Version::ToString() const
{
	return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

std::optional<Version> ParseVersion(const std::string &text)
{
	size_t pos = 0;
	if (pos < text.size() && (text[pos] == 'v' || text[pos] == 'V'))
		++pos;

	int parts[3] = {0, 0, 0};
	int count = 0;
	while (count < 3) {
		if (!ParseNumber(text, pos, parts[count]))
			return std::nullopt;
		++count;
		if (pos < text.size() && text[pos] == '.' && count < 3)
			++pos;
		else
			break;
	}

	Version version;
	version.major = parts[0];
	version.minor = parts[1];
	version.patch = parts[2];

	if (pos < text.size()) {
		if (text[pos] == '-') {
			version.prerelease = true;
		} else if (text[pos] != '+') {
			return std::nullopt; // e.g. "1.2.3.4" or "1.2x"
		}
	}
	return version;
}

bool IsNewer(const Version &remote, const Version &current)
{
	const auto r = std::make_tuple(remote.major, remote.minor, remote.patch);
	const auto c = std::make_tuple(current.major, current.minor, current.patch);
	if (r != c)
		return r > c;
	return current.prerelease && !remote.prerelease;
}

std::optional<std::string> ExtractTagName(const std::string &jsonBody)
{
	static const std::string key = "\"tag_name\"";
	const size_t keyPos = jsonBody.find(key);
	if (keyPos == std::string::npos)
		return std::nullopt;

	size_t pos = keyPos + key.size();
	while (pos < jsonBody.size() && std::isspace(static_cast<unsigned char>(jsonBody[pos])))
		++pos;
	if (pos >= jsonBody.size() || jsonBody[pos] != ':')
		return std::nullopt;
	++pos;
	while (pos < jsonBody.size() && std::isspace(static_cast<unsigned char>(jsonBody[pos])))
		++pos;
	if (pos >= jsonBody.size() || jsonBody[pos] != '"')
		return std::nullopt;
	++pos;

	std::string value;
	while (pos < jsonBody.size()) {
		const char c = jsonBody[pos++];
		if (c == '"')
			return value;
		if (c == '\\') {
			if (pos >= jsonBody.size())
				return std::nullopt;
			// Real tags never need escapes beyond these; a version parser
			// rejects anything odd anyway.
			const char escaped = jsonBody[pos++];
			value.push_back(escaped == 'n' ? '\n' : escaped);
		} else {
			value.push_back(c);
		}
	}
	return std::nullopt; // unterminated string
}

std::optional<Version> FindNewerVersion(const std::string &jsonBody, const std::string &currentVersion)
{
	const auto tag = ExtractTagName(jsonBody);
	if (!tag)
		return std::nullopt;

	const auto remote = ParseVersion(*tag);
	const auto current = ParseVersion(currentVersion);
	if (!remote || !current)
		return std::nullopt;

	if (!IsNewer(*remote, *current))
		return std::nullopt;
	return remote;
}

} // namespace obs_backuper
