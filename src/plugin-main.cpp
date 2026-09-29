// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>

#include "ObsConfigPathProvider.h"
#include "core/RestoreFlow.h"
#include "core/RestoreManager.h"
#include "core/RestoreResult.h"
#include "core/ActiveSelection.h"
#include "core/PathUtf8.h"
#include "core/SceneCollectionSnapshot.h"
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
// The profile / scene collection the restored config names (empty: none).
static obs_backuper::ActiveSelection pendingRestoreSelection;
// The restored scene collection files exactly as they came out of the backup.
static std::vector<obs_backuper::SceneCollectionFile> pendingRestoreScenes;
static std::filesystem::path pendingRestoreDataDir;

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
// OBS read user.ini and global.ini into memory before any plugin loaded and
// writes that memory back when it exits, which would silently undo the files a
// restore just put in place (custom docks, dock layout, window settings).
// Copy the restored values into the in-memory configs too. The profile and
// scene collection are switched separately, and [General] holds install-specific
// bookkeeping (version, install id, first-run flags) that must stay this
// install's own.
static void MergeRestoredConfigIntoMemory(const std::filesystem::path &obsDataDir)
{
	struct Target {
		const char *file;
		config_t *config;
	};
	const Target targets[] = {{"user.ini", obs_frontend_get_user_config()},
				  {"global.ini", obs_frontend_get_app_config()}};

	for (const auto &target : targets) {
		if (target.config == nullptr)
			continue;
		int applied = 0;
		for (const auto &entry : obs_backuper::ReadIniEntries(obsDataDir, target.file)) {
			if (!obs_backuper::ShouldMergeIniEntry(entry))
				continue;
			config_set_string(target.config, entry.section.c_str(), entry.key.c_str(), entry.value.c_str());
			++applied;
		}
		if (applied > 0)
			config_save_safe(target.config, "tmp", nullptr);
		obs_log(LOG_INFO, "restored %d setting(s) from %s into the running OBS", applied, target.file);
	}
}

// Where things live, worked out once at load: at shutdown the frontend and
// parts of Qt are already gone, so nothing there may be asked again.
static std::filesystem::path safetyBackupDir;
static std::filesystem::path obsDataDir;

static void SetPendingRestoreResult(bool success, bool rolledBack, obs_backuper::ErrorKind kind,
				    const std::string &message)
{
	pendingRestoreHasResult = true;
	pendingRestoreSuccess = success;
	pendingRestoreRolledBack = rolledBack;
	pendingRestoreErrorKind = kind;
	pendingRestoreErrorMessage = message;
}

static void LogAppliedRestore(const obs_backuper::AppliedRestore &applied)
{
	const auto &commit = applied.commit;
	obs_log(LOG_INFO, "applied a pending restore (staged in \"%s\") to \"%s\"",
		obs_backuper::PathToUtf8(applied.marker.stagingDir).c_str(),
		obs_backuper::PathToUtf8(applied.marker.targetDir).c_str());

	if (commit.success && commit.filesSkipped > 0)
		obs_log(LOG_WARNING, "pending restore applied; %d browser file(s) were in use and left as they were, first: %s",
			commit.filesSkipped, commit.firstSkippedMessage.c_str());
	else if (commit.success)
		obs_log(LOG_INFO, "pending restore applied successfully");
	else if (commit.rolledBack)
		obs_log(LOG_ERROR, "pending restore failed and was rolled back: %s", commit.errorMessage.c_str());
	else
		obs_log(LOG_ERROR, "pending restore failed: %s", commit.errorMessage.c_str());
}

// The normal way a restore is applied: while OBS shuts down, after it has saved
// everything it is going to save. Applied any earlier, OBS would keep working
// from what it had already read (its profile and scene collection lists, its
// settings) and write that back over the restored files on exit. The next start
// then simply reads the restored files like any other start. The outcome goes
// to a file, since there is no window left to show it in.
static void ApplyPendingRestoreAtShutdown()
{
	const auto applied = obs_backuper::ApplyPendingRestoreAndStoreResult(safetyBackupDir, obsDataDir);
	if (applied.wasPending)
		LogAppliedRestore(applied);
}

// A restore that was still waiting when OBS started: OBS was killed instead of
// shutting down, or a previous version left one. Apply it now and switch to what
// it restored as far as OBS's already-built lists allow; the rest is right from
// the next start on.
static void CommitPendingRestoreIfAny()
{
	obs_backuper::StoredRestoreResult stored;
	if (obs_backuper::TakeStoredRestoreResult(safetyBackupDir, stored))
		SetPendingRestoreResult(stored.success, stored.rolledBack, stored.errorKind, stored.errorMessage);

	const auto applied = obs_backuper::ApplyPendingRestore(safetyBackupDir, obsDataDir);
	if (!applied.wasPending)
		return;

	LogAppliedRestore(applied);
	SetPendingRestoreResult(applied.commit.success, applied.commit.rolledBack, applied.commit.errorKind,
				applied.commit.errorMessage);
	if (applied.commit.success) {
		pendingRestoreSelection = obs_backuper::ReadActiveSelection(applied.marker.targetDir);
		pendingRestoreScenes = obs_backuper::SnapshotSceneCollections(applied.marker.targetDir);
		pendingRestoreDataDir = applied.marker.targetDir;
		MergeRestoredConfigIntoMemory(applied.marker.targetDir);
	}
}

// The NULL-terminated list OBS returns as a vector. Frees the list.
static std::vector<std::string> TakeNames(char **list)
{
	std::vector<std::string> names;
	for (char **it = list; it != nullptr && *it != nullptr; ++it)
		names.emplace_back(*it);
	bfree(list);
	return names;
}

static std::string TakeName(char *name)
{
	const std::string copy = name != nullptr ? name : "";
	bfree(name);
	return copy;
}

// OBS chooses its profile before any plugin loads, so a restore applied after
// that comes too late for it: OBS would keep running on whatever profile it
// started with (canvas size, stream keys and hotkeys all live in the profile).
// The decisions are made by obs_backuper::PlanSelectionSwitch.
static void SwitchToRestoredSelection()
{
	const auto restored = pendingRestoreSelection;
	pendingRestoreSelection = {};
	const auto scenes = std::move(pendingRestoreScenes);
	pendingRestoreScenes.clear();

	const auto plan = obs_backuper::PlanSelectionSwitch(
		restored, TakeName(obs_frontend_get_current_profile()), TakeNames(obs_frontend_get_profiles()),
		TakeName(obs_frontend_get_current_scene_collection()), TakeNames(obs_frontend_get_scene_collections()),
		!scenes.empty());

	if (plan.switchProfile) {
		obs_log(LOG_INFO, "switching to the restored profile \"%s\"", plan.profile.c_str());
		obs_frontend_set_current_profile(plan.profile.c_str());
	}

	switch (plan.collectionAction) {
	case obs_backuper::CollectionAction::Switch:
		obs_log(LOG_INFO, "switching to the restored scene collection \"%s\"", plan.collection.c_str());
		obs_backuper::WriteSceneCollections(pendingRestoreDataDir, scenes);
		obs_frontend_set_current_scene_collection(plan.collection.c_str());
		break;
	case obs_backuper::CollectionAction::Reload:
		// OBS loaded this collection while still on the wrong profile's canvas,
		// rescaled it and saved it over the restored file. Step away (that save
		// happens now), put the files back as the backup had them, and load
		// the collection again on the right canvas.
		obs_log(LOG_INFO, "reloading the restored scene collection \"%s\"", plan.collection.c_str());
		obs_frontend_set_current_scene_collection(plan.bounceCollection.c_str());
		obs_backuper::WriteSceneCollections(pendingRestoreDataDir, scenes);
		obs_frontend_set_current_scene_collection(plan.collection.c_str());
		// Leaving the bounce collection saved it too, possibly rescaled.
		obs_backuper::WriteSceneCollections(pendingRestoreDataDir, scenes);
		break;
	case obs_backuper::CollectionAction::None:
		break;
	}
}

static void onFrontendEvent(enum obs_frontend_event event, void *)
{
	if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING || !pendingRestoreHasResult)
		return;

	pendingRestoreHasResult = false;
	if (pendingRestoreSuccess)
		SwitchToRestoredSelection();

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
	safetyBackupDir = obs_backuper::GetSafetyBackupDir();
	obsDataDir = obs_backuper::GetObsDataDir();
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

	ApplyPendingRestoreAtShutdown();

	obs_log(LOG_INFO, "plugin unloaded");
}
