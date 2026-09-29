// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "BackupDialog.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.h>

#include "../ObsConfigPathProvider.h"
#include "../core/BackupManager.h"
#include "../core/FileCollector.h"
#include "../core/RestoreManager.h"
#include "../plugin-support.h"
#include "../worker/BackupWorker.h"
#include "../worker/RestoreWorker.h"
#include "../worker/UpdateChecker.h"
#include "../core/UpdateCheck.h"
#include "../core/Crypto.h"
#include "../core/PluginSettings.h"
#include "../worker/ValidateWorker.h"
#include "PasswordDialog.h"
#include "ErrorDialog.h"
#include "ProgressDialog.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFont>
#include <QLabel>
#include <QLocale>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStyle>
#include <QString>
#include <QSysInfo>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>

namespace {

constexpr const char *kAuthorName = "Chill Pixel Bakery";
constexpr const char *kAuthorUrl = "https://www.youtube.com/@ChillPixelBakery";

std::string CurrentSourceOs()
{
#if defined(_WIN32)
	return "windows";
#elif defined(__APPLE__)
	return "macos";
#else
	return "unknown";
#endif
}

#if defined(_WIN32)
QString PowerShellQuote(const QString &value)
{
	QString escaped = value;
	escaped.replace("'", "''");
	return "'" + escaped + "'";
}
#else
QString PosixShellQuote(const QString &value)
{
	QString escaped = value;
	escaped.replace("'", "'\\''");
	return "'" + escaped + "'";
}
#endif

// Schedules a helper (a detached shell/PowerShell one-liner) that waits for
// THIS process to actually disappear before launching a new OBS instance.
// Launching the new instance immediately -- without waiting -- races the
// current process's own shutdown: OBS uses a single-instance check on
// startup, and closing our main window only *requests* an exit, it doesn't
// block until the process has actually terminated. In practice the new
// instance started fast enough to see the still-running old one and refused
// to launch ("OBS is already running!"), and forcing the issue then left the
// old instance's shutdown interrupted, which OBS detected as an unclean exit
// on the next launch ("OBS Studio did not shut down properly"). The wait is
// capped at 30s as a safety net in case the old process never exits for some
// reason, so the relaunch attempt still eventually happens instead of hanging
// forever.
bool ScheduleDelayedRelaunch(const QString &program, const QStringList &arguments, const QString &workingDirectory)
{
	const qint64 pid = QCoreApplication::applicationPid();

#if defined(_WIN32)
	QStringList quotedArgs;
	for (const QString &arg : arguments)
		quotedArgs << PowerShellQuote(arg);
	const QString command = QString("$p=%1; $deadline=(Get-Date).AddSeconds(30); "
					 "while ((Get-Process -Id $p -ErrorAction SilentlyContinue) -and "
					 "(Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 300 }; "
					 "Start-Process -FilePath %2 -ArgumentList @(%3) -WorkingDirectory %4")
				       .arg(pid)
				       .arg(PowerShellQuote(program))
				       .arg(quotedArgs.join(", "))
				       .arg(PowerShellQuote(workingDirectory));
	return QProcess::startDetached("powershell.exe", {"-NoProfile", "-WindowStyle", "Hidden", "-Command", command});
#else
	QStringList quotedArgs;
	for (const QString &arg : arguments)
		quotedArgs << PosixShellQuote(arg);
	// Integer-second sleep (not e.g. "sleep 0.2") -- macOS's /bin/sleep only
	// accepts whole seconds.
	const QString command = QString("pid=%1; i=0; "
					 "while kill -0 \"$pid\" 2>/dev/null && [ \"$i\" -lt 30 ]; do sleep 1; i=$((i+1)); done; "
					 "cd %2 && exec %3 %4")
				       .arg(pid)
				       .arg(PosixShellQuote(workingDirectory))
				       .arg(PosixShellQuote(program))
				       .arg(quotedArgs.join(' '));
	return QProcess::startDetached("/bin/sh", {"-c", command});
#endif
}

// Schedules a new OBS process to start once this one has actually exited,
// then closes the current main window -- the same shutdown path OBS takes
// when the user closes it manually (so any "save on exit" logic still runs),
// rather than killing the process outright. Returns false if the relaunch
// helper could not even be scheduled, in which case nothing was closed.
bool RestartObs()
{
	const QString program = QCoreApplication::applicationFilePath();
	const QStringList arguments = QCoreApplication::arguments().mid(1);
	const QString workingDirectory = QCoreApplication::applicationDirPath();

	if (!ScheduleDelayedRelaunch(program, arguments, workingDirectory)) {
		obs_log(LOG_ERROR, "restart OBS: failed to schedule the delayed relaunch helper");
		return false;
	}

	obs_log(LOG_INFO, "restart OBS: relaunch helper scheduled, closing current instance");
	if (auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window()))
		mainWindow->close();
	return true;
}

// OS identifiers from manifest.json ("windows", "macos") as a user-facing,
// localized name; anything unrecognized is shown as-is.
QString DisplayOsName(const std::string &os)
{
	if (os == "windows")
		return obs_module_text("OsName.windows");
	if (os == "macos")
		return obs_module_text("OsName.macos");
	return QString::fromStdString(os);
}

// "2026-09-29T12:00:00Z" (UTC) -> the user's local date and time in their
// locale's format. Falls back to the raw text if it doesn't parse.
QString FormatIsoLocal(const std::string &iso)
{
	const QDateTime utc = QDateTime::fromString(QString::fromStdString(iso), Qt::ISODate);
	if (!utc.isValid())
		return QString::fromStdString(iso);
	return QLocale::system().toString(utc.toLocalTime(), QLocale::ShortFormat);
}

// A button followed by a short description of what it does. The description
// is also exposed as the button's accessible description, since a separate
// QLabel isn't associated with the button for screen readers otherwise.
QPushButton *AddActionRow(QVBoxLayout *layout, QWidget *parent, QStyle::StandardPixmap icon, const char *labelKey,
			   const char *descriptionKey)
{
	auto *button = new QPushButton(parent->style()->standardIcon(icon), obs_module_text(labelKey), parent);
	button->setMinimumHeight(36);
	button->setAccessibleDescription(obs_module_text(descriptionKey));

	// Default text color (no hardcoded palette): stays readable in both the
	// dark and light OBS themes.
	auto *description = new QLabel(obs_module_text(descriptionKey), parent);
	description->setWordWrap(true);
	description->setContentsMargins(4, 0, 4, 8);
	QFont font = description->font();
	if (font.pointSizeF() > 0)
		font.setPointSizeF(font.pointSizeF() * 0.92);
	description->setFont(font);

	layout->addWidget(button);
	layout->addWidget(description);
	return button;
}

} // namespace

BackupDialog::BackupDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(obs_module_text("BackupDialog.WindowTitle"));

	setMinimumWidth(440);

	auto *layout = new QVBoxLayout(this);

	auto *intro = new QLabel(obs_module_text("BackupDialog.Intro"), this);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	layout->addSpacing(8);

	// Hidden until the background update check finds a newer release.
	updateLabel = new QLabel(this);
	updateLabel->setTextFormat(Qt::RichText);
	updateLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
	updateLabel->setOpenExternalLinks(true);
	updateLabel->setWordWrap(true);
	updateLabel->setVisible(false);
	layout->addWidget(updateLabel);

	// Tab order follows creation order: Create -> Restore -> Close. Enter
	// activates the default (Create) button, Esc closes the dialog.
	createBackupButton = AddActionRow(layout, this, QStyle::SP_DialogSaveButton, "BackupDialog.CreateBackup",
					   "BackupDialog.CreateBackupDescription");
	createBackupButton->setDefault(true);

	// Stage 7: optional password protection. Only the checkbox state is
	// remembered, never a password.
	protectCheckBox = new QCheckBox(obs_module_text("BackupDialog.ProtectWithPassword"), this);
	protectCheckBox->setToolTip(obs_module_text("BackupDialog.ProtectWithPasswordHint"));
	protectCheckBox->setAccessibleDescription(obs_module_text("BackupDialog.ProtectWithPasswordHint"));
	protectCheckBox->setContentsMargins(4, 0, 4, 0);
	protectCheckBox->setChecked(
		obs_backuper::PluginSettings::Load(obs_backuper::GetPluginSettingsPath()).encryptBackups);
	layout->addWidget(protectCheckBox);
	layout->addSpacing(8);

	restoreBackupButton = AddActionRow(layout, this, QStyle::SP_DialogOpenButton, "BackupDialog.RestoreBackup",
					    "BackupDialog.RestoreBackupDescription");

	lastBackupLabel = new QLabel(this);
	lastBackupLabel->setWordWrap(true);
	layout->addWidget(lastBackupLabel);

	// Credit link. The brand name and URL are not translatable UI text, so
	// only the "Made by %1" wrapper lives in the locale files. Links are
	// keyboard-focusable (Tab, Enter) for accessibility.
	auto *madeBy = new QLabel(QString(obs_module_text("BackupDialog.MadeBy"))
					   .arg(QString("<a href=\"%1\">%2</a>").arg(kAuthorUrl, kAuthorName)),
				   this);
	madeBy->setTextFormat(Qt::RichText);
	madeBy->setTextInteractionFlags(Qt::TextBrowserInteraction);
	madeBy->setOpenExternalLinks(true);
	madeBy->setAlignment(Qt::AlignCenter);
	layout->addWidget(madeBy);

	auto *buttons = new QDialogButtonBox(this);
	auto *closeButton = buttons->addButton(obs_module_text("Common.Close"), QDialogButtonBox::RejectRole);
	layout->addWidget(buttons);

	connect(createBackupButton, &QPushButton::clicked, this, &BackupDialog::onCreateBackupClicked);
	connect(restoreBackupButton, &QPushButton::clicked, this, &BackupDialog::onRestoreBackupClicked);
	connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

	connect(protectCheckBox, &QCheckBox::toggled, this, [](bool checked) {
		const auto settingsPath = obs_backuper::GetPluginSettingsPath();
		auto settings = obs_backuper::PluginSettings::Load(settingsPath);
		settings.encryptBackups = checked;
		std::string saveError;
		if (!settings.Save(settingsPath, saveError))
			obs_log(LOG_WARNING, "could not remember the password-protection choice: %s", saveError.c_str());
	});

	setTabOrder(createBackupButton, protectCheckBox);
	setTabOrder(protectCheckBox, restoreBackupButton);
	setTabOrder(restoreBackupButton, closeButton);
	createBackupButton->setFocus();

	refreshLastBackupLabel();
}

void BackupDialog::refreshLastBackupLabel()
{
	const auto settings = obs_backuper::PluginSettings::Load(obs_backuper::GetPluginSettingsPath());
	if (settings.lastBackupIso8601.empty())
		lastBackupLabel->setText(obs_module_text("BackupDialog.LastBackupNever"));
	else
		lastBackupLabel->setText(QString(obs_module_text("BackupDialog.LastBackup"))
						  .arg(FormatIsoLocal(settings.lastBackupIso8601)));
}

bool BackupDialog::confirmSensitiveDataWarning()
{
	const auto settingsPath = obs_backuper::GetPluginSettingsPath();
	auto settings = obs_backuper::PluginSettings::Load(settingsPath);
	if (settings.sensitiveWarningDismissed)
		return true;

	QMessageBox box(this);
	box.setWindowTitle(obs_module_text("BackupDialog.Title"));
	box.setIcon(QMessageBox::Warning);
	box.setText(obs_module_text("BackupDialog.SensitiveWarning"));
	auto *dontShowAgain = new QCheckBox(obs_module_text("BackupDialog.DontShowAgain"), &box);
	box.setCheckBox(dontShowAgain);
	QPushButton *continueButton = box.addButton(obs_module_text("Common.Continue"), QMessageBox::AcceptRole);
	QPushButton *cancelButton = box.addButton(obs_module_text("Common.Cancel"), QMessageBox::RejectRole);
	box.setDefaultButton(continueButton);
	box.setEscapeButton(cancelButton);
	box.exec();

	if (box.clickedButton() != continueButton)
		return false;

	if (dontShowAgain->isChecked()) {
		settings.sensitiveWarningDismissed = true;
		std::string saveError;
		if (!settings.Save(settingsPath, saveError))
			obs_log(LOG_WARNING, "could not remember the dismissed warning: %s", saveError.c_str());
	}
	return true;
}

void BackupDialog::showEvent(QShowEvent *event)
{
	QDialog::showEvent(event);

	if (updateCheckStarted)
		return;
	updateCheckStarted = true;

	auto *checker = new UpdateChecker(this);
	connect(checker, &UpdateChecker::updateAvailable, this, [this](const QString &version) {
		updateLabel->setText(QString(obs_module_text("BackupDialog.UpdateAvailable"))
					     .arg(version, QString(obs_backuper::kReleasesPageUrl)));
		updateLabel->setVisible(true);
	});
	checker->start();
}

void BackupDialog::onCreateBackupClicked()
{
	if (activeBackupWorker || activeRestoreWorker)
		return;

	const auto collected = obs_backuper::CollectFiles(obs_backuper::GetObsDataDir());
	const double totalMegabytes = static_cast<double>(collected.totalSizeBytes) / (1024.0 * 1024.0);

	obs_log(LOG_INFO, "collected %zu files (%.2f MB) for backup", collected.files.size(), totalMegabytes);
	for (const auto &file : collected.files)
		obs_log(LOG_DEBUG, "  %s", file.relativePath.string().c_str());

	if (collected.files.empty()) {
		QMessageBox::warning(this, obs_module_text("BackupDialog.Title"),
				      obs_module_text("BackupDialog.NothingToBackup"));
		return;
	}

	// A password-protected backup no longer exposes stream keys, so the
	// sensitive-data warning applies only to plain ones.
	std::string password;
	const bool protectWithPassword = protectCheckBox->isChecked();
	if (protectWithPassword) {
		if (!PasswordDialog::Ask(this, PasswordDialog::Mode::Create, password))
			return;
	} else if (!confirmSensitiveDataWarning()) {
		return;
	}

	const QString destDir =
		QFileDialog::getExistingDirectory(this, obs_module_text("BackupDialog.ChooseFolder"));
	if (destDir.isEmpty()) {
		obs_backuper::crypto::SecureWipe(password);
		return;
	}

	obs_backuper::BackupOptions options;
	options.password = password;
	obs_backuper::crypto::SecureWipe(password);
	options.destinationDir = std::filesystem::path(destDir.toStdString());
	options.pluginVersion = PLUGIN_VERSION;
	options.sourceOs = CurrentSourceOs();
	options.sourceOsVersion = QSysInfo::productVersion().toStdString();
	options.obsVersion = obs_get_version_string();

	auto *worker = new BackupWorker(collected, options, this);
	auto *progress = new ProgressDialog(this);

	activeBackupWorker = worker;
	createBackupButton->setEnabled(false);
	restoreBackupButton->setEnabled(false);

	if (protectWithPassword) {
		progress->setCryptoPhase(ProgressDialog::CryptoPhase::After, obs_module_text("ProgressDialog.EncryptStage"));
		connect(worker, &BackupWorker::encryptionProgressChanged, progress,
			&ProgressDialog::onCryptoProgressChanged);
	}
	connect(worker, &BackupWorker::progressChanged, progress, &ProgressDialog::onProgressChanged);
	connect(worker, &BackupWorker::backupFinished, this,
		[this, progress, protectWithPassword](bool success, const QString &archivePath, qint64 archiveSizeBytes,
				  const QString &errorMessage, int errorKind, const QStringList &warnings) {
			progress->close();
			progress->deleteLater();

			activeBackupWorker = nullptr;
			createBackupButton->setEnabled(true);
			restoreBackupButton->setEnabled(true);

			if (success) {
					const auto settingsPath = obs_backuper::GetPluginSettingsPath();
					auto settings = obs_backuper::PluginSettings::Load(settingsPath);
					settings.lastBackupIso8601 =
						QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
					settings.lastBackupPath = archivePath.toStdString();
					std::string saveError;
					if (!settings.Save(settingsPath, saveError))
						obs_log(LOG_WARNING, "could not remember the last backup date: %s",
							saveError.c_str());
					refreshLastBackupLabel();

				const double archiveMegabytes = static_cast<double>(archiveSizeBytes) / (1024.0 * 1024.0);
				QString message = QString(obs_module_text(protectWithPassword ? "BackupDialog.BackupSuccessEncrypted"
											       : "BackupDialog.BackupSuccess"))
							   .arg(archivePath)
							   .arg(archiveMegabytes, 0, 'f', 2);
				if (!warnings.isEmpty()) {
					message += "\n\n" +
						   QString(obs_module_text("BackupDialog.BackupWarnings")).arg(warnings.size());
				}
				QMessageBox::information(this, obs_module_text("BackupDialog.Title"), message);
			} else {
				ShowErrorDialog(this, obs_module_text("BackupDialog.BackupError"),
						 static_cast<obs_backuper::ErrorKind>(errorKind), errorMessage);
			}
		});
	connect(worker, &BackupWorker::finished, worker, &QObject::deleteLater);

	worker->start();
	progress->show();
}

void BackupDialog::onRestoreBackupClicked()
{
	if (activeBackupWorker || activeRestoreWorker)
		return;

	const QString archivePathStr = QFileDialog::getOpenFileName(
		this, obs_module_text("RestoreDialog.ChooseArchive"), QString(),
		obs_module_text("RestoreDialog.ArchiveFileFilter"));
	if (archivePathStr.isEmpty())
		return;

	const std::filesystem::path archivePath(archivePathStr.toStdString());

	// For a password-protected file this only reads the header (no password
	// yet); the full check runs in unlockEncryptedArchive() below.
	auto validation = obs_backuper::RestoreManager::ValidateArchive(archivePath);
	std::string password;
	if (validation.passwordRequired) {
		if (!unlockEncryptedArchive(archivePath, password, validation))
			return;
	} else if (!validation.valid) {
		ShowErrorDialog(this, obs_module_text("RestoreDialog.InvalidArchive"), validation.errorKind,
				QString::fromStdString(validation.errorMessage));
		return;
	}
	const bool encryptedArchive = !password.empty();

	const std::string currentOs = CurrentSourceOs();
	const auto &manifest = validation.manifest;

	QString confirmText = QString(obs_module_text("RestoreDialog.ConfirmText"))
				      .arg(FormatIsoLocal(manifest.createdAtIso8601))
				      .arg(DisplayOsName(manifest.sourceOs))
				      .arg(QString::fromStdString(manifest.obsVersion));

	if (std::find(manifest.includedSections.begin(), manifest.includedSections.end(), "plugins") !=
	    manifest.includedSections.end()) {
		confirmText += "\n\n" + QString(obs_module_text("RestoreDialog.PluginsWarning"));
	}

	if (!manifest.sourceOs.empty() && manifest.sourceOs != currentOs) {
		confirmText += "\n\n" + QString(obs_module_text("RestoreDialog.OsMismatchWarning"))
					       .arg(DisplayOsName(manifest.sourceOs))
					       .arg(DisplayOsName(currentOs));
	}

	QMessageBox confirmBox(this);
	confirmBox.setWindowTitle(obs_module_text("BackupDialog.Title"));
	confirmBox.setIcon(QMessageBox::Warning);
	confirmBox.setText(confirmText);
	QPushButton *continueButton =
		confirmBox.addButton(obs_module_text("Common.Continue"), QMessageBox::AcceptRole);
	QPushButton *cancelButton = confirmBox.addButton(obs_module_text("Common.Cancel"), QMessageBox::RejectRole);
	confirmBox.setDefaultButton(cancelButton); // overwrites settings: Enter must not confirm by accident
	confirmBox.setEscapeButton(cancelButton);
	confirmBox.exec();
	if (confirmBox.clickedButton() != continueButton) {
		obs_backuper::crypto::SecureWipe(password);
		return;
	}

	const auto targetDir = obs_backuper::GetObsDataDir();
	const auto safetyBackupDir = obs_backuper::GetSafetyBackupDir();
	const auto stagingDir = safetyBackupDir / "pending-restore-staging";

	obs_backuper::RestoreOptions options;
	options.archivePath = archivePath;
	options.targetDir = targetDir;
	options.safetyBackupDir = safetyBackupDir;
	options.pluginVersion = PLUGIN_VERSION;
	options.sourceOs = currentOs;
	options.sourceOsVersion = QSysInfo::productVersion().toStdString();
	options.obsVersion = obs_get_version_string();
	options.password = password;
	obs_backuper::crypto::SecureWipe(password);

	// The archive is extracted into stagingDir here, NOT into targetDir --
	// applying it onto targetDir is deferred to the next obs_module_load(),
	// before OBS has loaded any scene collection into memory this session.
	// See src/core/RestoreManager.h for details: OBS
	// flushes its own in-memory copy of anything it touched this session back
	// to disk on a clean exit, silently undoing a restore written directly
	// into a live obs-studio directory -- including (especially) the
	// currently active scene collection.
	auto *worker = new RestoreWorker(options, stagingDir, this);
	auto *progress = new ProgressDialog(this, obs_module_text("RestoreProgressDialog.Title"),
					     obs_module_text("RestoreProgressDialog.Starting"),
					     obs_module_text("RestoreProgressDialog.Stage"));

	activeRestoreWorker = worker;
	createBackupButton->setEnabled(false);
	restoreBackupButton->setEnabled(false);

	progress->setSafetyPhase(true, obs_module_text("RestoreProgressDialog.SafetyStage"));
	connect(worker, &RestoreWorker::safetyBackupProgressChanged, progress,
		&ProgressDialog::onSafetyProgressChanged);
	if (encryptedArchive) {
		progress->setCryptoPhase(ProgressDialog::CryptoPhase::Before,
					  obs_module_text("RestoreProgressDialog.DecryptStage"));
		connect(worker, &RestoreWorker::decryptionProgressChanged, progress,
			&ProgressDialog::onCryptoProgressChanged);
	}
	connect(worker, &RestoreWorker::progressChanged, progress, &ProgressDialog::onProgressChanged);
	connect(worker, &RestoreWorker::stagingFinished, this,
		[this, progress, safetyBackupDir, options](bool success, const QString &errorMessage,
								    int errorKind, const QString &stagingDirStr,
								    const QString &safetyBackupPath) mutable {
			// The worker has its own copy; don't keep another one alive here.
			obs_backuper::crypto::SecureWipe(options.password);
			progress->close();
			progress->deleteLater();

			activeRestoreWorker = nullptr;
			createBackupButton->setEnabled(true);
			restoreBackupButton->setEnabled(true);

			if (!success) {
				ShowErrorDialog(this, obs_module_text("RestoreDialog.RestoreError"),
						 static_cast<obs_backuper::ErrorKind>(errorKind), errorMessage);
				return;
			}

			obs_backuper::PendingRestoreMarker marker;
			marker.stagingDir = std::filesystem::path(stagingDirStr.toStdString());
			marker.targetDir = obs_backuper::GetObsDataDir();
			marker.safetyBackupPath = std::filesystem::path(safetyBackupPath.toStdString());
			marker.fileWriteMaxAttempts = options.fileWriteMaxAttempts;
			marker.fileWriteRetryDelayMs = options.fileWriteRetryDelayMs;

			const auto markerPath = safetyBackupDir / "pending-restore.marker";
			std::string markerError;
			if (!obs_backuper::RestoreManager::WritePendingRestoreMarker(markerPath, marker, markerError)) {
				ShowErrorDialog(this, obs_module_text("RestoreDialog.RestoreError"), obs_backuper::ErrorKind::Unknown,
						QString::fromStdString(markerError));
				return;
			}

			QMessageBox successBox(this);
			successBox.setWindowTitle(obs_module_text("BackupDialog.Title"));
			successBox.setIcon(QMessageBox::Information);
			successBox.setText(QString(obs_module_text("RestoreDialog.RestoreStaged")).arg(safetyBackupPath));
			QPushButton *restartButton =
				successBox.addButton(obs_module_text("RestoreDialog.RestartNow"), QMessageBox::AcceptRole);
			QPushButton *laterButton =
					successBox.addButton(obs_module_text("RestoreDialog.RestartLater"), QMessageBox::RejectRole);
				successBox.setDefaultButton(restartButton);
				successBox.setEscapeButton(laterButton);
				successBox.exec();

			if (successBox.clickedButton() == restartButton && !RestartObs()) {
				QMessageBox::warning(this, obs_module_text("BackupDialog.Title"),
						      obs_module_text("RestoreDialog.RestartFailed"));
			}
		});
	connect(worker, &RestoreWorker::finished, worker, &QObject::deleteLater);

	worker->start();
	progress->show();
}

bool BackupDialog::unlockEncryptedArchive(const std::filesystem::path &archivePath, std::string &password,
					   obs_backuper::ArchiveValidationResult &validation)
{
	const auto tempDir = obs_backuper::GetSafetyBackupDir();

	for (;;) {
		if (!PasswordDialog::Ask(this, PasswordDialog::Mode::Enter, password))
			return false;

		// Checking a password means deriving the key and decrypting the whole
		// file, so it runs on a worker with a progress dialog; a local event
		// loop keeps the UI alive while waiting.
		createBackupButton->setEnabled(false);
		restoreBackupButton->setEnabled(false);

		ValidateWorker worker(archivePath, password, tempDir);
		ProgressDialog progress(this, obs_module_text("DecryptProgressDialog.Title"),
					 obs_module_text("RestoreProgressDialog.DecryptStage"));
		progress.setCryptoPhase(ProgressDialog::CryptoPhase::Only, obs_module_text("RestoreProgressDialog.DecryptStage"));
		connect(&worker, &ValidateWorker::decryptionProgressChanged, &progress,
			&ProgressDialog::onCryptoProgressChanged);

		QEventLoop loop;
		connect(&worker, &QThread::finished, &loop, &QEventLoop::quit);
		worker.start();
		progress.show();
		loop.exec();
		worker.wait();
		progress.close();

		createBackupButton->setEnabled(true);
		restoreBackupButton->setEnabled(true);

		validation = worker.result();
		if (validation.valid)
			return true;

		obs_backuper::crypto::SecureWipe(password);
		ShowErrorDialog(this, obs_module_text("RestoreDialog.CouldNotOpen"), validation.errorKind,
				QString::fromStdString(validation.errorMessage));

		// A wrong password (or damage AEAD can't tell apart from one) gets
		// another try without re-choosing the file; anything else is final.
		if (validation.errorKind != obs_backuper::ErrorKind::WrongPasswordOrCorrupted)
			return false;
	}
}
