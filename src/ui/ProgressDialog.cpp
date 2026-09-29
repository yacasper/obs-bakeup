// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "ProgressDialog.h"

#include "../core/ProgressEstimator.h"

#include <obs-module.h>

#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

#include <chrono>

ProgressDialog::ProgressDialog(QWidget *parent, const QString &title, const QString &startingText,
				const QString &stageFormat)
	: QDialog(parent),
	  stageFormat_(stageFormat.isEmpty() ? QString(obs_module_text("ProgressDialog.Stage")) : stageFormat)
{
	setWindowTitle(title.isEmpty() ? QString(obs_module_text("ProgressDialog.Title")) : title);
	setWindowModality(Qt::WindowModal);
	// No close button -- the dialog is closed only programmatically once the
	// backup/restore finishes (there is no cancellation).
	setWindowFlags((windowFlags() | Qt::CustomizeWindowHint) & ~Qt::WindowCloseButtonHint);

	statusLabel = new QLabel(startingText.isEmpty() ? QString(obs_module_text("ProgressDialog.Starting")) : startingText,
				  this);
	fileLabel = new QLabel(this);
	etaLabel = new QLabel(this);
	progressBar = new QProgressBar(this);
	progressBar->setRange(0, 0); // indeterminate until the first progress update arrives
	progressBar->setFormat("%p%");
	progressBar->setAccessibleName(obs_module_text("ProgressDialog.AccessibleName"));

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(statusLabel);
	layout->addWidget(fileLabel);
	layout->addWidget(progressBar);
	layout->addWidget(etaLabel);

	setMinimumWidth(460);
	elapsed_.start();
}

void ProgressDialog::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Escape) {
		event->accept();
		return;
	}
	QDialog::keyPressEvent(event);
}

void ProgressDialog::setCryptoPhase(CryptoPhase phase, const QString &stageText)
{
	cryptoPhase_ = phase;
	cryptoStageText_ = stageText;
	recomputeRanges();
}

void ProgressDialog::setSafetyPhase(bool enabled, const QString &stageFormat)
{
	safetyEnabled_ = enabled;
	safetyStageFormat_ = stageFormat;
	recomputeRanges();
}

// Rough shares of the whole operation: key derivation plus streaming
// encryption and saving the current settings are each quicker than
// compressing/extracting the backup itself, but not free.
void ProgressDialog::recomputeRanges()
{
	multiPhase_ = cryptoPhase_ == CryptoPhase::Before || cryptoPhase_ == CryptoPhase::After || safetyEnabled_;
	fileRange_ = {0.0, 1.0};
	cryptoRange_ = {0.0, 1.0};
	safetyRange_ = {0.0, 1.0};

	if (cryptoPhase_ == CryptoPhase::After) {
		fileRange_ = {0.0, 0.75};
		cryptoRange_ = {0.75, 1.0};
	} else if (cryptoPhase_ == CryptoPhase::Before && safetyEnabled_) {
		cryptoRange_ = {0.0, 0.15};
		safetyRange_ = {0.15, 0.4};
		fileRange_ = {0.4, 1.0};
	} else if (cryptoPhase_ == CryptoPhase::Before) {
		cryptoRange_ = {0.0, 0.25};
		fileRange_ = {0.25, 1.0};
	} else if (safetyEnabled_) {
		safetyRange_ = {0.0, 0.3};
		fileRange_ = {0.3, 1.0};
	}
}

void ProgressDialog::showFile(const QString &fileName)
{
	fileLabel->setToolTip(fileName);
	fileLabel->setText(fontMetrics().elidedText(fileName, Qt::ElideMiddle, minimumWidth() - 40));
}

void ProgressDialog::showOverallFraction(double fraction)
{
	progressBar->setRange(0, 100);
	progressBar->setValue(static_cast<int>(fraction * 100.0));

	const auto remaining = obs_backuper::ProgressEstimator::RemainingSeconds(
		static_cast<std::int64_t>(fraction * 1000.0), 1000, std::chrono::milliseconds(elapsed_.elapsed()));
	if (remaining) {
		const auto eta = obs_backuper::ToEtaDisplay(*remaining);
		etaLabel->setText(QString(obs_module_text(eta.inMinutes ? "ProgressDialog.EtaMinutes"
									   : "ProgressDialog.EtaSeconds"))
					   .arg(eta.value));
	} else {
		etaLabel->clear();
	}
}

void ProgressDialog::onCryptoProgressChanged(qint64 done, qint64 total)
{
	statusLabel->setText(cryptoStageText_);
	fileLabel->clear();
	fileLabel->setToolTip(QString());

	const double phaseFraction = total > 0 ? static_cast<double>(done) / static_cast<double>(total) : 0.0;
	showOverallFraction(obs_backuper::MapToOverallFraction(phaseFraction, cryptoRange_.start, cryptoRange_.end));
}

void ProgressDialog::onSafetyProgressChanged(qint64 current, qint64 total, const QString &currentFileName)
{
	statusLabel->setText(QString(safetyStageFormat_).arg(current).arg(total));
	showFile(currentFileName);

	const double fraction = total > 0 ? static_cast<double>(current - 1) / static_cast<double>(total) : 0.0;
	showOverallFraction(obs_backuper::MapToOverallFraction(fraction, safetyRange_.start, safetyRange_.end));
}

void ProgressDialog::onProgressChanged(qint64 current, qint64 total, const QString &currentFileName)
{
	if (multiPhase_) {
		statusLabel->setText(QString(stageFormat_).arg(current).arg(total));
		showFile(currentFileName);

		const double fileFraction = total > 0 ? static_cast<double>(current - 1) / static_cast<double>(total) : 0.0;
		showOverallFraction(obs_backuper::MapToOverallFraction(fileFraction, fileRange_.start, fileRange_.end));
		return;
	}

	if (total > 0) {
		progressBar->setRange(0, 100);
		progressBar->setValue(obs_backuper::ProgressPercent(current, total));
	}

	statusLabel->setText(QString(stageFormat_).arg(current).arg(total));

	fileLabel->setToolTip(currentFileName);
	fileLabel->setText(fontMetrics().elidedText(currentFileName, Qt::ElideMiddle, minimumWidth() - 40));

	// `current` is the file being processed now, so current - 1 are done.
	const auto remaining = obs_backuper::ProgressEstimator::RemainingSeconds(
		current - 1, total, std::chrono::milliseconds(elapsed_.elapsed()));
	if (remaining) {
		const auto eta = obs_backuper::ToEtaDisplay(*remaining);
		etaLabel->setText(QString(obs_module_text(eta.inMinutes ? "ProgressDialog.EtaMinutes"
									   : "ProgressDialog.EtaSeconds"))
					   .arg(eta.value));
	} else {
		etaLabel->clear();
	}
}
