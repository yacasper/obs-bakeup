// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "ValidateWorker.h"

#include "../core/Crypto.h"

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
	result_ = obs_backuper::RestoreManager::ValidateArchive(
		archivePath_, password_, tempDir_, [this](std::uint64_t done, std::uint64_t total) {
			emit decryptionProgressChanged(static_cast<qint64>(done), static_cast<qint64>(total));
		});
	obs_backuper::crypto::SecureWipe(password_);
}
