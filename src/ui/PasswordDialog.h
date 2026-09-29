// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <QDialog>
#include <QString>

#include <string>

class QCheckBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;

// Password prompt for password-protected backups (Stage 7). Two modes:
//  - Create: password + repeat, a strength hint (minimum length is enforced,
//    weakness is only a warning) and the "cannot be recovered" notice;
//  - Enter: a single field, for restoring an encrypted backup.
// Enter/Tab/Esc behave like the other dialogs: Enter accepts once the input is
// valid, Esc cancels, Tab walks the fields.
class PasswordDialog : public QDialog {
	Q_OBJECT

public:
	enum class Mode { Create, Enter };

	// Shows the dialog modally. On acceptance returns true and sets
	// outPassword to the password as UTF-8, normalized to Unicode NFC so the
	// same typed password yields the same bytes on Windows and macOS. The
	// caller should SecureWipe() it once done.
	static bool Ask(QWidget *parent, Mode mode, std::string &outPassword);

private:
	PasswordDialog(QWidget *parent, Mode mode);

	void refreshState();
	std::string passwordUtf8() const;

	Mode mode_;
	QLineEdit *passwordEdit_;
	QLineEdit *repeatEdit_ = nullptr;
	QCheckBox *showCheck_;
	QLabel *strengthLabel_ = nullptr;
	QLabel *mismatchLabel_ = nullptr;
	QDialogButtonBox *buttons_;
};
