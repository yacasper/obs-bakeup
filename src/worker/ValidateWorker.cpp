// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "ValidateWorker.h"

#include "../core/Crypto.h"

#include <exception>

ValidateWorker::ValidateWorker(std::filesystem::path archivePath, std::string password, std::filesystem::path tempDir,
			       QObject *parent)
	: QThread(parent),
	  archivePath_(std::move(archivePath)),
	  password_(std::move(password)),
	  tempDir_(std::move(tempDir))
{
}

void ValidateWorker::run()
{
	// An exception escaping a thread would take all of OBS down (see GuardedCall.h).
	try {
		result_ = obs_backuper::RestoreManager::ValidateArchive(
			archivePath_, password_, tempDir_, [this](std::uint64_t done, std::uint64_t total) {
				emit decryptionProgressChanged(static_cast<qint64>(done), static_cast<qint64>(total));
			});
	} catch (const std::exception &error) {
		result_ = {};
		result_.errorKind = obs_backuper::ErrorKind::Unknown;
		result_.errorMessage = std::string("unexpected error: ") + error.what();
	} catch (...) {
		result_ = {};
		result_.errorKind = obs_backuper::ErrorKind::Unknown;
		result_.errorMessage = "unexpected error: unknown exception";
	}
	obs_backuper::crypto::SecureWipe(password_);
}
