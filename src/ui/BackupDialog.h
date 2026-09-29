// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <QDialog>
#include <QString>

#include <filesystem>
#include <string>

#include "../core/RestoreManager.h"

class QCheckBox;
class QLabel;
class QShowEvent;
class QPushButton;
class BackupWorker;
class RestoreWorker;

class BackupDialog : public QDialog {
	Q_OBJECT

public:
	explicit BackupDialog(QWidget *parent = nullptr);

protected:
	// The update check starts the first time the dialog is opened.
	void showEvent(QShowEvent *event) override;

private slots:
	void onCreateBackupClicked();
	void onRestoreBackupClicked();

private:
	void refreshLastBackupLabel();
	// Returns false if the user backed out of the sensitive-data warning.
	bool confirmSensitiveDataWarning();

	// Restoring a password-protected backup: asks for the password and checks
	// it (off the UI thread), re-asking after a wrong one. Returns false if the
	// user cancelled or the file can't be opened at all (already reported).
	bool unlockEncryptedArchive(const std::filesystem::path &archivePath, std::string &password,
				    obs_backuper::ArchiveValidationResult &validation);

	QPushButton *createBackupButton;
	QCheckBox *protectCheckBox;
	QPushButton *restoreBackupButton;
	QLabel *lastBackupLabel;
	QLabel *updateLabel;
	bool updateCheckStarted = false;

	// Non-null while a backup/restore is running -- prevents starting a
	// second one, or the other kind, in parallel (both buttons are disabled
	// while either is running).
	BackupWorker *activeBackupWorker = nullptr;
	RestoreWorker *activeRestoreWorker = nullptr;
};
