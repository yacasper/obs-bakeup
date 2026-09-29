// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "core/ErrorKind.h"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

using namespace obs_backuper;

TEST_CASE("ErrorKindLocaleKey maps every kind to a non-empty Error.* key", "[ErrorKind]")
{
	const ErrorKind all[] = {ErrorKind::None,		    ErrorKind::Unknown,
				  ErrorKind::DestinationNotWritable, ErrorKind::NotEnoughDiskSpace,
				  ErrorKind::ArchiveWriteFailed,     ErrorKind::InvalidArchive,
				  ErrorKind::UnsupportedVersion,     ErrorKind::SafetyBackupFailed,
				  ErrorKind::RestoreExtractFailed,   ErrorKind::RestoreApplyFailed,
				  ErrorKind::WrongPasswordOrCorrupted, ErrorKind::UnsupportedContainerVersion,
				  ErrorKind::EncryptionFailed};

	for (const ErrorKind kind : all) {
		const std::string key = ErrorKindLocaleKey(kind);
		CHECK(key.rfind("Error.", 0) == 0);
		CHECK(key.size() > std::string("Error.").size());
	}
}

TEST_CASE("ErrorKindLocaleKey gives each specific kind its own key, and None falls back to Unknown", "[ErrorKind]")
{
	std::set<std::string> keys;
	for (const ErrorKind kind : {ErrorKind::DestinationNotWritable, ErrorKind::NotEnoughDiskSpace,
				      ErrorKind::ArchiveWriteFailed, ErrorKind::InvalidArchive,
				      ErrorKind::UnsupportedVersion, ErrorKind::SafetyBackupFailed,
				      ErrorKind::RestoreExtractFailed, ErrorKind::RestoreApplyFailed,
				      ErrorKind::WrongPasswordOrCorrupted, ErrorKind::UnsupportedContainerVersion,
				      ErrorKind::EncryptionFailed})
		keys.insert(ErrorKindLocaleKey(kind));

	CHECK(keys.size() == 11);
	CHECK(keys.count(ErrorKindLocaleKey(ErrorKind::Unknown)) == 0);
	CHECK(std::string(ErrorKindLocaleKey(ErrorKind::None)) == ErrorKindLocaleKey(ErrorKind::Unknown));
}
