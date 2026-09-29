// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <QString>
#include <QStringList>
#include <QThread>

#include "../core/BackupManager.h"

// Runs BackupManager::CreateBackup on a background thread so it doesn't
// freeze the OBS UI while archiving (spec, Stage 3 readiness criteria).
// Progress and the result are delivered to the UI thread through regular Qt
// signals (Qt marshals them across threads automatically via
// QueuedConnection).
class BackupWorker : public QThread {
	Q_OBJECT

public:
	BackupWorker(obs_backuper::CollectionResult collected, obs_backuper::BackupOptions options,
		     QObject *parent = nullptr);

signals:
	// current/total — 1-based index of the current file.
	void progressChanged(qint64 current, qint64 total, const QString &currentFileName);

	// Encryption phase (password-protected backups only), plaintext bytes
	// done / total.
	void encryptionProgressChanged(qint64 done, qint64 total);

	// success/archivePath/archiveSizeBytes/errorMessage/errorKind (an
	// obs_backuper::ErrorKind)/warnings — see obs_backuper::BackupOutcome.
	void backupFinished(bool success, const QString &archivePath, qint64 archiveSizeBytes,
			     const QString &errorMessage, int errorKind, const QStringList &warnings);

protected:
	void run() override;

private:
	obs_backuper::CollectionResult collected_;
	obs_backuper::BackupOptions options_;
};
