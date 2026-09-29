// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "ErrorDialog.h"

#include <obs-module.h>

#include <QMessageBox>
#include <QPushButton>

void ShowErrorDialog(QWidget *parent, const QString &headline, obs_backuper::ErrorKind kind,
		     const QString &technicalDetails, const QString &extraExplanation)
{
	QString text = headline;
	if (!extraExplanation.isEmpty())
		text += "\n\n" + extraExplanation;
	text += "\n\n" + QString(obs_module_text(obs_backuper::ErrorKindLocaleKey(kind)));

	QMessageBox box(parent);
	box.setWindowTitle(obs_module_text("BackupDialog.Title"));
	box.setIcon(QMessageBox::Critical);
	box.setText(text);

	QPushButton *detailsButton = nullptr;
	if (!technicalDetails.isEmpty())
		detailsButton = box.addButton(obs_module_text("Common.ShowDetails"), QMessageBox::ActionRole);
	QPushButton *okButton = box.addButton(obs_module_text("Common.Ok"), QMessageBox::AcceptRole);
	box.setDefaultButton(okButton);
	box.setEscapeButton(okButton);

	// QMessageBox closes on any button click, so "show details" re-opens the
	// same box with the details appended (and the button gone) rather than
	// using QMessageBox::setDetailedText, whose toggle button is labelled by
	// Qt itself and so would not follow the plugin's locale files.
	box.exec();
	if (detailsButton && box.clickedButton() == detailsButton) {
		box.removeButton(detailsButton);
		delete detailsButton;
		box.setText(text + "\n\n" + obs_module_text("Common.TechnicalDetails") + "\n" + technicalDetails);
		box.setTextInteractionFlags(Qt::TextSelectableByMouse);
		box.exec();
	}
}
