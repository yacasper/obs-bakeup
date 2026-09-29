// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "BackupWorker.h"

#include <obs-module.h>

#include "../core/Crypto.h"
#include "../plugin-support.h"

BackupWorker::BackupWorker(obs_backuper::CollectionResult collected, obs_backuper::BackupOptions options, QObject *parent)
	: QThread(parent),
	  collected_(std::move(collected)),
	  options_(std::move(options))
{
}

void BackupWorker::run()
{
	obs_log(LOG_INFO, "backup started: %zu file(s), destination \"%s\", password-protected: %s",
		collected_.files.size(), options_.destinationDir.string().c_str(),
		options_.password.empty() ? "no" : "yes");

	options_.onEncryptProgress = [this](std::uint64_t done, std::uint64_t total) {
		emit encryptionProgressChanged(static_cast<qint64>(done), static_cast<qint64>(total));
	};

	const auto outcome = obs_backuper::BackupManager::CreateBackup(
		collected_, options_,
		[this](std::size_t current, std::size_t total, const std::filesystem::path &currentFile) {
			emit progressChanged(static_cast<qint64>(current), static_cast<qint64>(total),
					      QString::fromStdString(currentFile.generic_string()));
		});

	// The password is not needed past this point.
	obs_backuper::crypto::SecureWipe(options_.password);

	QStringList warnings;
	for (const auto &warning : outcome.warnings) {
		const QString line =
			QString::fromStdString(warning.relativePath.generic_string() + ": " + warning.message);
		warnings.append(line);
		obs_log(LOG_WARNING, "backup: skipped file \"%s\": %s", warning.relativePath.string().c_str(),
			warning.message.c_str());
	}

	if (outcome.success) {
		obs_log(LOG_INFO, "backup finished successfully: \"%s\" (%llu bytes), %zu warning(s)",
			outcome.archivePath.string().c_str(),
			static_cast<unsigned long long>(outcome.archiveSizeBytes), outcome.warnings.size());
	} else {
		obs_log(LOG_ERROR, "backup failed: %s", outcome.errorMessage.c_str());
	}

	emit backupFinished(outcome.success, QString::fromStdString(outcome.archivePath.string()),
			     static_cast<qint64>(outcome.archiveSizeBytes),
			     QString::fromStdString(outcome.errorMessage), static_cast<int>(outcome.errorKind), warnings);
}
