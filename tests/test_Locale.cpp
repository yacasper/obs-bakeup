// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

// Guards the Stage 5 readiness criterion "no UI string outside the locale
// files": every locale key the UI code asks for must exist in every locale,
// and the locales must stay in sync with each other (same keys, same %N
// placeholders), so a missing translation is caught here rather than by a
// user seeing a raw key like "BackupDialog.CreateBackup" in the OBS UI.
#include "core/ErrorKind.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#ifndef OBS_BACKUPER_SOURCE_DIR
#error "OBS_BACKUPER_SOURCE_DIR must point at the repository root"
#endif

namespace fs = std::filesystem;

namespace {

const fs::path kRoot = OBS_BACKUPER_SOURCE_DIR;
const char *const kLocales[] = {"en-US", "ru-RU"};

std::string ReadAll(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	REQUIRE(in.is_open());
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

std::map<std::string, std::string> LoadLocale(const std::string &name)
{
	std::map<std::string, std::string> entries;
	std::istringstream lines(ReadAll(kRoot / "data" / "locale" / (name + ".ini")));
	std::string line;
	int lineNo = 0;
	while (std::getline(lines, line)) {
		++lineNo;
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty())
			continue;

		INFO(name << ".ini line " << lineNo << ": " << line);
		const auto eq = line.find('=');
		REQUIRE(eq != std::string::npos);

		const std::string key = line.substr(0, eq);
		const std::string value = line.substr(eq + 1);
		REQUIRE(value.size() >= 2);
		CHECK(value.front() == '"');
		CHECK(value.back() == '"');
		CHECK(entries.count(key) == 0); // no duplicate keys
		entries[key] = value.substr(1, value.size() - 2);
	}
	return entries;
}

std::set<std::string> Placeholders(const std::string &text)
{
	std::set<std::string> found;
	static const std::regex re("%[1-9]");
	for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it)
		found.insert(it->str());
	return found;
}

// Every "Some.Key" string literal handed to obs_module_text() in the plugin's
// sources.
std::set<std::string> KeysUsedInSources()
{
	std::set<std::string> keys;
	static const std::regex re(R"re(obs_module_text\(\s*"([^"]+)"\s*\))re");
	static const std::regex keyLiteral(R"re("((?:[A-Z][A-Za-z]*)\.[A-Z][A-Za-z]*)")re");

	for (const auto &entry : fs::recursive_directory_iterator(kRoot / "src")) {
		const auto ext = entry.path().extension().string();
		if (!entry.is_regular_file() || (ext != ".cpp" && ext != ".h"))
			continue;

		const std::string text = ReadAll(entry.path());
		for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it)
			keys.insert((*it)[1].str());
		// Keys passed through a const char* parameter (e.g. AddActionRow's
		// labelKey) appear as bare "Section.Name" literals.
		for (std::sregex_iterator it(text.begin(), text.end(), keyLiteral), end; it != end; ++it)
			keys.insert((*it)[1].str());
	}
	return keys;
}

} // namespace

TEST_CASE("Every locale file is well-formed", "[Locale]")
{
	for (const char *locale : kLocales) {
		INFO(locale);
		CHECK_FALSE(LoadLocale(locale).empty());
	}
}

TEST_CASE("All locales define exactly the same keys", "[Locale]")
{
	const auto reference = LoadLocale("en-US");

	for (const char *locale : kLocales) {
		INFO(locale);
		const auto other = LoadLocale(locale);
		for (const auto &[key, _] : reference)
			CHECK(other.count(key) == 1);
		for (const auto &[key, _] : other)
			CHECK(reference.count(key) == 1);
	}
}

TEST_CASE("No locale has an empty translation", "[Locale]")
{
	for (const char *locale : kLocales) {
		for (const auto &[key, value] : LoadLocale(locale)) {
			INFO(locale << ": " << key);
			CHECK_FALSE(value.empty());
		}
	}
}

TEST_CASE("Translations use the same %N placeholders as the English text", "[Locale]")
{
	const auto reference = LoadLocale("en-US");

	for (const char *locale : kLocales) {
		const auto other = LoadLocale(locale);
		for (const auto &[key, value] : other) {
			INFO(locale << ": " << key);
			CHECK(Placeholders(value) == Placeholders(reference.at(key)));
		}
	}
}

TEST_CASE("Every key the UI code uses exists in every locale", "[Locale]")
{
	const auto used = KeysUsedInSources();
	REQUIRE_FALSE(used.empty());

	for (const char *locale : kLocales) {
		const auto entries = LoadLocale(locale);
		for (const auto &key : used) {
			INFO(locale << " is missing key: " << key);
			CHECK(entries.count(key) == 1);
		}
	}
}

TEST_CASE("Every ErrorKind's friendly message exists in every locale", "[Locale][ErrorKind]")
{
	using obs_backuper::ErrorKind;
	for (const char *locale : kLocales) {
		const auto entries = LoadLocale(locale);
		for (const ErrorKind kind :
		     {ErrorKind::None, ErrorKind::Unknown, ErrorKind::DestinationNotWritable,
		      ErrorKind::NotEnoughDiskSpace, ErrorKind::ArchiveWriteFailed, ErrorKind::InvalidArchive,
		      ErrorKind::UnsupportedVersion, ErrorKind::SafetyBackupFailed, ErrorKind::RestoreExtractFailed,
		      ErrorKind::RestoreApplyFailed, ErrorKind::WrongPasswordOrCorrupted,
		      ErrorKind::UnsupportedContainerVersion, ErrorKind::EncryptionFailed}) {
			INFO(locale << ": " << obs_backuper::ErrorKindLocaleKey(kind));
			CHECK(entries.count(obs_backuper::ErrorKindLocaleKey(kind)) == 1);
		}
	}
}

TEST_CASE("Friendly error messages do not leak raw technical wording", "[Locale]")
{
	// Task 6 of Stage 5: main error text must be understandable without
	// knowing what an errno, a manifest or an exception is.
	static const char *const forbidden[] = {"errno", "exception", "manifest", "0x", "std::", "miniz"};

	for (const char *locale : kLocales) {
		for (const auto &[key, value] : LoadLocale(locale)) {
			if (key.rfind("Error.", 0) != 0)
				continue;
			for (const char *word : forbidden) {
				INFO(locale << ": " << key << " contains \"" << word << "\"");
				CHECK(value.find(word) == std::string::npos);
			}
		}
	}
}
