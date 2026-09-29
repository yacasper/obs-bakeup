// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <QString>
#include <QThread>

#include "../core/RestoreManager.h"

// Runs RestoreManager::PerformStagedRestore on a background thread so it
// doesn't freeze the OBS UI while extracting (mirrors BackupWorker, Stage 3).
// This only stages the restore into stagingDir and snapshots targetDir for
// safety -- it does NOT touch the settings in targetDir (installed plugins are
// the exception, see RestoreManager::PerformStagedRestore). Applying the staged
// settings onto targetDir happens later, at the next obs_module_load() (restoring
// directly into a live obs-studio directory isn't safe -- see
// src/core/RestoreManager.h for why, and for the two-step design that works
// around it).
class RestoreWorker : public QThread {
	Q_OBJECT

public:
	RestoreWorker(obs_backuper::RestoreOptions options, std::filesystem::path stagingDir, QObject *parent = nullptr);

signals:
	// current/total — 1-based index of the current file.
	void progressChanged(qint64 current, qint64 total, const QString &currentFileName);

	// Safety backup of the current settings, taken before restoring; current/
	// total is the 1-based index of the current file.
	void safetyBackupProgressChanged(qint64 current, qint64 total, const QString &currentFileName);

	// Decryption phase (password-protected backups only), plaintext bytes
	// done / total.
	void decryptionProgressChanged(qint64 done, qint64 total);

	// success/errorMessage/errorKind/stagingDir/safetyBackupPath — see
	// obs_backuper::StagedRestoreOutcome.
	void stagingFinished(bool success, const QString &errorMessage, int errorKind, const QString &stagingDir,
			      const QString &safetyBackupPath, int pluginFilesFailed, const QString &pluginFailureMessage);

protected:
	void run() override;

private:
	obs_backuper::RestoreOptions options_;
	std::filesystem::path stagingDir_;
};
