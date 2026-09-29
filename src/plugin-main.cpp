// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include <obs-module.h>
#include <obs-frontend-api.h>

#include "ObsConfigPathProvider.h"
#include "core/RestoreManager.h"
#include "plugin-support.h"
#include "ui/BackupDialog.h"
#include "ui/ErrorDialog.h"

#include <QMessageBox>
#include <QPointer>
#include <QString>
#include <QWidget>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

// QPointer, not a raw pointer: backupDialog is parented to the OBS main
// window (see openBackupDialog()), so when OBS shuts down, Qt destroys it
// together with that window *before* obs_shutdown() gets around to calling
// obs_module_unload() below. A raw pointer would then be dangling by the time
// obs_module_unload() runs, and `delete`-ing it crashes (use-after-free) --
// this was reproduced as a SIGSEGV in obs_module_unload on OBS exit, but only
// after the plugin's dialog had actually been opened at least once (an
// untouched nullptr made `delete` a harmless no-op, masking the bug).
// QPointer automatically resets to nullptr when the pointee is destroyed
// elsewhere, so the guard in obs_module_unload() below skips the redundant,
// unsafe delete in that case.
static QPointer<BackupDialog> backupDialog;

// Set by CommitPendingRestoreIfAny() (called from obs_module_load(), before
// OBS has loaded any scene collection this session -- see
// CommitPendingRestoreIfAny() below and src/core/RestoreManager.h) and consumed once by
// onFrontendEvent() to tell the user how the restore they staged before this
// restart turned out, once the main window actually exists to parent a
// message box to.
static bool pendingRestoreHasResult = false;
static bool pendingRestoreSuccess = false;
static bool pendingRestoreRolledBack = false;
static std::string pendingRestoreErrorMessage;
static obs_backuper::ErrorKind pendingRestoreErrorKind = obs_backuper::ErrorKind::None;

static void openBackupDialog()
{
	auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());

	if (!backupDialog)
		backupDialog = new BackupDialog(mainWindow);

	backupDialog->show();
	backupDialog->raise();
	backupDialog->activateWindow();
}

// Applies a restore staged by BackupDialog on a previous run of OBS, if any.
// Must run this early (from obs_module_load()) -- before OBS reads any scene
// collection off disk into memory this session, which is the only safe window:
// nothing has "claimed" the
// restored files in memory yet for OBS to later flush its own stale copy
// back over them.
static void CommitPendingRestoreIfAny()
{
	// A crash or kill mid-restore can leave a decrypted temporary archive
	// behind; never let plaintext outlive the operation that made it.
	obs_backuper::RestoreManager::RemoveStaleTemporaryFiles(obs_backuper::GetSafetyBackupDir());

	const auto markerPath = obs_backuper::GetSafetyBackupDir() / "pending-restore.marker";

	obs_backuper::PendingRestoreMarker marker;
	std::string readError;
	if (!obs_backuper::RestoreManager::ReadPendingRestoreMarker(markerPath, marker, readError))
		return; // nothing pending -- the common case

	obs_log(LOG_INFO, "found a pending restore (staged in \"%s\"), applying it to \"%s\"",
		marker.stagingDir.string().c_str(), marker.targetDir.string().c_str());

	const auto commit = obs_backuper::RestoreManager::CommitStagedRestore(
		marker.stagingDir, marker.targetDir, marker.safetyBackupPath, marker.fileWriteMaxAttempts,
		marker.fileWriteRetryDelayMs);

	obs_backuper::RestoreManager::RemovePendingRestoreMarker(markerPath);

	pendingRestoreHasResult = true;
	pendingRestoreSuccess = commit.success;
	pendingRestoreRolledBack = commit.rolledBack;
	pendingRestoreErrorMessage = commit.errorMessage;
	pendingRestoreErrorKind = commit.errorKind;

	if (commit.success) {
		obs_log(LOG_INFO, "pending restore applied successfully");
	} else if (commit.rolledBack) {
		obs_log(LOG_ERROR, "pending restore failed and was rolled back: %s", commit.errorMessage.c_str());
	} else {
		obs_log(LOG_ERROR, "pending restore failed: %s", commit.errorMessage.c_str());
	}
}

static void onFrontendEvent(enum obs_frontend_event event, void *)
{
	if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING || !pendingRestoreHasResult)
		return;

	pendingRestoreHasResult = false;

	auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	if (pendingRestoreSuccess) {
		QMessageBox::information(mainWindow, obs_module_text("BackupDialog.Title"),
					  obs_module_text("RestoreDialog.PendingRestoreSuccess"));
	} else if (pendingRestoreRolledBack) {
		ShowErrorDialog(mainWindow, obs_module_text("RestoreDialog.PendingRestoreError"), pendingRestoreErrorKind,
				QString::fromStdString(pendingRestoreErrorMessage),
				obs_module_text("RestoreDialog.PendingRestoreRolledBack"));
	} else {
		ShowErrorDialog(mainWindow, obs_module_text("RestoreDialog.PendingRestoreError"), pendingRestoreErrorKind,
				QString::fromStdString(pendingRestoreErrorMessage));
	}
}

bool obs_module_load(void)
{
	CommitPendingRestoreIfAny();

	obs_frontend_add_event_callback(onFrontendEvent, nullptr);

	obs_frontend_add_tools_menu_item(obs_module_text("Menu.OBSBackuper"), [](void *) {
		openBackupDialog();
	}, nullptr);

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);

	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);

	if (backupDialog)
		delete backupDialog;

	obs_log(LOG_INFO, "plugin unloaded");
}
