// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "RestoreWorker.h"

#include <obs-module.h>

#include "../core/Crypto.h"
#include "../core/GuardedCall.h"
#include "../core/PathUtf8.h"
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
		obs_backuper::PathToUtf8(options_.archivePath).c_str(), obs_backuper::PathToUtf8(stagingDir_).c_str(), obs_backuper::PathToUtf8(options_.targetDir).c_str());

	options_.onSafetyBackupProgress = [this](std::size_t current, std::size_t total,
						  const std::filesystem::path &currentFile) {
		emit safetyBackupProgressChanged(static_cast<qint64>(current), static_cast<qint64>(total),
						  QString::fromStdString(obs_backuper::GenericPathToUtf8(currentFile)));
	};
	options_.onDecryptProgress = [this](std::uint64_t done, std::uint64_t total) {
		emit decryptionProgressChanged(static_cast<qint64>(done), static_cast<qint64>(total));
	};

	const auto stage = [this] {
		return obs_backuper::RestoreManager::PerformStagedRestore(
			options_, stagingDir_,
			[this](std::size_t current, std::size_t total, const std::filesystem::path &currentFile) {
				emit progressChanged(static_cast<qint64>(current), static_cast<qint64>(total),
						      QString::fromStdString(obs_backuper::GenericPathToUtf8(currentFile)));
			});
	};
	const auto outcome = obs_backuper::RunGuarded<decltype(stage())>(stage);

	// The password is not needed past this point.
	obs_backuper::crypto::SecureWipe(options_.password);

	if (outcome.pluginFilesFailed > 0)
		obs_log(LOG_WARNING, "%d plugin file(s) could not be restored, first: %s", outcome.pluginFilesFailed,
			outcome.pluginFailureMessage.c_str());

	if (outcome.success) {
		obs_log(LOG_INFO, "restore staged successfully (safety backup: \"%s\"); will be applied on next OBS launch",
			obs_backuper::PathToUtf8(outcome.safetyBackupPath).c_str());
	} else {
		obs_log(LOG_ERROR, "failed to stage restore: %s", outcome.errorMessage.c_str());
	}

	emit stagingFinished(outcome.success, QString::fromStdString(outcome.errorMessage),
			      static_cast<int>(outcome.errorKind),
			      QString::fromStdString(obs_backuper::PathToUtf8(outcome.stagingDir)),
			      QString::fromStdString(obs_backuper::PathToUtf8(outcome.safetyBackupPath)), outcome.pluginFilesFailed,
			      QString::fromStdString(outcome.pluginFailureMessage));
}
