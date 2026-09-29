// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "RestoreWorker.h"

#include <obs-module.h>

#include "../core/Crypto.h"
#include "../plugin-support.h"

RestoreWorker::RestoreWorker(obs_backuper::RestoreOptions options, std::filesystem::path stagingDir, QObject *parent)
	: QThread(parent),
	  options_(std::move(options)),
	  stagingDir_(std::move(stagingDir))
{
}

void RestoreWorker::run()
{
	obs_log(LOG_INFO, "restore: staging archive \"%s\" into \"%s\" (target \"%s\")",
		options_.archivePath.string().c_str(), stagingDir_.string().c_str(), options_.targetDir.string().c_str());

	options_.onSafetyBackupProgress = [this](std::size_t current, std::size_t total,
						  const std::filesystem::path &currentFile) {
		emit safetyBackupProgressChanged(static_cast<qint64>(current), static_cast<qint64>(total),
						  QString::fromStdString(currentFile.generic_string()));
	};
	options_.onDecryptProgress = [this](std::uint64_t done, std::uint64_t total) {
		emit decryptionProgressChanged(static_cast<qint64>(done), static_cast<qint64>(total));
	};

	const auto outcome = obs_backuper::RestoreManager::PerformStagedRestore(
		options_, stagingDir_,
		[this](std::size_t current, std::size_t total, const std::filesystem::path &currentFile) {
			emit progressChanged(static_cast<qint64>(current), static_cast<qint64>(total),
					      QString::fromStdString(currentFile.generic_string()));
		});

	// The password is not needed past this point.
	obs_backuper::crypto::SecureWipe(options_.password);

	if (outcome.success) {
		obs_log(LOG_INFO, "restore staged successfully (safety backup: \"%s\"); will be applied on next OBS launch",
			outcome.safetyBackupPath.string().c_str());
	} else {
		obs_log(LOG_ERROR, "failed to stage restore: %s", outcome.errorMessage.c_str());
	}

	emit stagingFinished(outcome.success, QString::fromStdString(outcome.errorMessage),
			      static_cast<int>(outcome.errorKind),
			      QString::fromStdString(outcome.stagingDir.string()),
			      QString::fromStdString(outcome.safetyBackupPath.string()));
}
