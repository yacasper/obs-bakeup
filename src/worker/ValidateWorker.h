// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <QString>
#include <QThread>

#include <filesystem>
#include <string>

#include "../core/RestoreManager.h"

// Runs RestoreManager::ValidateArchive off the UI thread. For a password-
// protected backup that means a full decryption (key derivation plus the whole
// file), which would otherwise freeze OBS. Read result() after finished().
class ValidateWorker : public QThread {
	Q_OBJECT

public:
	ValidateWorker(std::filesystem::path archivePath, std::string password, std::filesystem::path tempDir,
		       QObject *parent = nullptr);

	const obs_backuper::ArchiveValidationResult &result() const { return result_; }

signals:
	// Decryption progress, plaintext bytes done / total.
	void decryptionProgressChanged(qint64 done, qint64 total);

protected:
	void run() override;

private:
	std::filesystem::path archivePath_;
	std::string password_;
	std::filesystem::path tempDir_;
	obs_backuper::ArchiveValidationResult result_;
};
