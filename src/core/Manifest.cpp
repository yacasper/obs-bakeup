// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "Manifest.h"

#include <regex>
#include <sstream>

namespace obs_backuper {

namespace {

std::string EscapeJsonString(const std::string &input)
{
	std::string out;
	out.reserve(input.size());

	for (const unsigned char c : input) {
		switch (c) {
		case '"':
			out += "\\\"";
			break;
		case '\\':
			out += "\\\\";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\r':
			out += "\\r";
			break;
		case '\t':
			out += "\\t";
			break;
		default:
			if (c < 0x20) {
				static const char *hexDigits = "0123456789abcdef";
				out += "\\u00";
				out += hexDigits[(c >> 4) & 0xF];
				out += hexDigits[c & 0xF];
			} else {
				out += static_cast<char>(c);
			}
			break;
		}
	}
	return out;
}

std::string JsonQuoted(const std::string &value)
{
	return "\"" + EscapeJsonString(value) + "\"";
}

std::string JsonStringArray(const std::vector<std::string> &items)
{
	std::ostringstream out;
	out << "[";
	for (std::size_t i = 0; i < items.size(); ++i) {
		if (i > 0)
			out << ", ";
		out << JsonQuoted(items[i]);
	}
	out << "]";
	return out.str();
}

std::string UnescapeJsonString(const std::string &input)
{
	std::string out;
	out.reserve(input.size());

	for (std::size_t i = 0; i < input.size(); ++i) {
		if (input[i] != '\\' || i + 1 >= input.size()) {
			out += input[i];
			continue;
		}

		const char next = input[++i];
		switch (next) {
		case 'n':
			out += '\n';
			break;
		case 'r':
			out += '\r';
			break;
		case 't':
			out += '\t';
			break;
		case '"':
			out += '"';
			break;
		case '\\':
			out += '\\';
			break;
		default:
			out += next;
			break;
		}
	}
	return out;
}

// Matches "key": "value" (value may be empty), capturing value's raw
// (still-escaped) contents.
bool ExtractJsonString(const std::string &json, const std::string &key, std::string &outValue)
{
	const std::regex pattern("\"" + key + "\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
	std::smatch match;
	if (!std::regex_search(json, match, pattern))
		return false;

	outValue = UnescapeJsonString(match[1].str());
	return true;
}

bool ExtractJsonInt(const std::string &json, const std::string &key, int &outValue)
{
	const std::regex pattern("\"" + key + "\"\\s*:\\s*(-?[0-9]+)");
	std::smatch match;
	if (!std::regex_search(json, match, pattern))
		return false;

	outValue = std::stoi(match[1].str());
	return true;
}

// Matches "key": [ "a", "b", ... ] — an empty array is valid and yields an
// empty vector.
bool ExtractJsonStringArray(const std::string &json, const std::string &key, std::vector<std::string> &outValues)
{
	const std::regex arrayPattern("\"" + key + "\"\\s*:\\s*\\[([^\\]]*)\\]");
	std::smatch arrayMatch;
	if (!std::regex_search(json, arrayMatch, arrayPattern))
		return false;

	outValues.clear();
	const std::string itemsText = arrayMatch[1].str();
	const std::regex itemPattern("\"((?:[^\"\\\\]|\\\\.)*)\"");
	for (auto it = std::sregex_iterator(itemsText.begin(), itemsText.end(), itemPattern); it != std::sregex_iterator();
	     ++it)
		outValues.push_back(UnescapeJsonString((*it)[1].str()));

	return true;
}

} // namespace

std::string BuildManifestJson(const ManifestInfo &info)
{
	std::ostringstream out;
	out << "{\n";
	out << "  \"backup_format_version\": " << info.backupFormatVersion << ",\n";
	out << "  \"plugin_version\": " << JsonQuoted(info.pluginVersion) << ",\n";
	out << "  \"created_at\": " << JsonQuoted(info.createdAtIso8601) << ",\n";
	out << "  \"source_os\": " << JsonQuoted(info.sourceOs) << ",\n";
	out << "  \"source_os_version\": " << JsonQuoted(info.sourceOsVersion) << ",\n";
	out << "  \"obs_version\": " << JsonQuoted(info.obsVersion) << ",\n";
	out << "  \"included_sections\": " << JsonStringArray(info.includedSections) << ",\n";
	out << "  \"excluded_sections\": " << JsonStringArray(info.excludedSections) << "\n";
	out << "}\n";
	return out.str();
}

bool ParseManifestJson(const std::string &json, ManifestInfo &outInfo, std::string &errorMessage)
{
	ManifestInfo info;

	if (!ExtractJsonInt(json, "backup_format_version", info.backupFormatVersion)) {
		errorMessage = "manifest.json is missing or has an invalid \"backup_format_version\" field";
		return false;
	}

	// The remaining fields are informational (shown to the user / logged) --
	// a missing one shouldn't block restoring an otherwise-valid archive, so
	// they're left at their default (empty) value rather than failing.
	ExtractJsonString(json, "plugin_version", info.pluginVersion);
	ExtractJsonString(json, "created_at", info.createdAtIso8601);
	ExtractJsonString(json, "source_os", info.sourceOs);
	ExtractJsonString(json, "source_os_version", info.sourceOsVersion);
	ExtractJsonString(json, "obs_version", info.obsVersion);
	ExtractJsonStringArray(json, "included_sections", info.includedSections);
	ExtractJsonStringArray(json, "excluded_sections", info.excludedSections);

	outInfo = std::move(info);
	return true;
}

} // namespace obs_backuper
