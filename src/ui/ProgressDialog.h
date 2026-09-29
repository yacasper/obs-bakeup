// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QString>

class QKeyEvent;
class QLabel;
class QProgressBar;

// Modal progress indicator: no cancellation, just display -- hooked up to
// BackupWorker's/RestoreWorker's signals. Shared between the "Create Backup"
// and "Restore from Backup" flows: the wording is parameterized ("Archiving"
// vs "Restoring"). Leaving a parameter empty falls back to the backup
// wording.
//
// Shows the current stage as text, the file being processed (elided in the
// middle so long paths don't stretch the dialog; the full path is in the
// tooltip), a percentage on the bar, and an estimated remaining time once
// enough has been observed for it to be meaningful.
class ProgressDialog : public QDialog {
	Q_OBJECT

public:
	// stageFormat is a QString::arg() template taking (current, total) in that
	// order, e.g. "Archiving file %1 of %2".
	explicit ProgressDialog(QWidget *parent = nullptr, const QString &title = QString(),
				 const QString &startingText = QString(), const QString &stageFormat = QString());

	// Where the encryption/decryption phase sits in the whole operation, so
	// the bar and the ETA cover both phases: before the file phase (restoring
	// an encrypted backup), after it (creating one), or alone (just opening a
	// backup to check it). stageText replaces the status line while that phase
	// runs.
	enum class CryptoPhase { None, Before, After, Only };
	void setCryptoPhase(CryptoPhase phase, const QString &stageText);

	// The restore flow first saves the current settings (a stage of its own,
	// with per-file progress) before extracting; stageFormat takes (current,
	// total) like the main one.
	void setSafetyPhase(bool enabled, const QString &stageFormat);

public slots:
	void onProgressChanged(qint64 current, qint64 total, const QString &currentFileName);
	void onSafetyProgressChanged(qint64 current, qint64 total, const QString &currentFileName);
	void onCryptoProgressChanged(qint64 done, qint64 total);

protected:
	// There is no cancellation, so Esc must not dismiss the dialog while the
	// worker is still running (QDialog's default would hide it).
	void keyPressEvent(QKeyEvent *event) override;

private:
	// Sets the bar and ETA from an overall fraction in [0, 1] (multi-phase mode).
	void showOverallFraction(double fraction);
	// Splits the bar between the phases in use (files / crypto / safety backup).
	void recomputeRanges();
	void showFile(const QString &fileName);

	struct Range {
		double start = 0.0;
		double end = 1.0;
	};

	QLabel *statusLabel;
	QLabel *fileLabel;
	QLabel *etaLabel;
	QProgressBar *progressBar;
	QString stageFormat_;
	QElapsedTimer elapsed_;
	CryptoPhase cryptoPhase_ = CryptoPhase::None;
	QString cryptoStageText_;
	bool safetyEnabled_ = false;
	QString safetyStageFormat_;
	bool multiPhase_ = false;
	Range fileRange_, cryptoRange_, safetyRange_;
};
