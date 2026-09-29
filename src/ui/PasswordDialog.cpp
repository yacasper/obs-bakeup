// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "PasswordDialog.h"

#include "../core/Crypto.h"
#include "../core/PasswordStrength.h"

#include <obs-module.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

PasswordDialog::PasswordDialog(QWidget *parent, Mode mode) : QDialog(parent), mode_(mode)
{
	const bool create = mode == Mode::Create;
	setWindowTitle(obs_module_text(create ? "PasswordDialog.CreateTitle" : "PasswordDialog.EnterTitle"));
	setMinimumWidth(420);

	auto *layout = new QVBoxLayout(this);

	auto *intro = new QLabel(obs_module_text(create ? "PasswordDialog.CreateIntro" : "PasswordDialog.EnterIntro"), this);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	layout->addSpacing(6);

	auto *passwordLabel = new QLabel(obs_module_text("PasswordDialog.Password"), this);
	passwordEdit_ = new QLineEdit(this);
	passwordEdit_->setEchoMode(QLineEdit::Password);
	passwordEdit_->setAccessibleName(obs_module_text("PasswordDialog.Password"));
	passwordLabel->setBuddy(passwordEdit_);
	layout->addWidget(passwordLabel);
	layout->addWidget(passwordEdit_);

	if (create) {
		auto *repeatLabel = new QLabel(obs_module_text("PasswordDialog.Repeat"), this);
		repeatEdit_ = new QLineEdit(this);
		repeatEdit_->setEchoMode(QLineEdit::Password);
		repeatEdit_->setAccessibleName(obs_module_text("PasswordDialog.Repeat"));
		repeatLabel->setBuddy(repeatEdit_);
		layout->addWidget(repeatLabel);
		layout->addWidget(repeatEdit_);
	}

	showCheck_ = new QCheckBox(obs_module_text("PasswordDialog.Show"), this);
	layout->addWidget(showCheck_);

	if (create) {
		strengthLabel_ = new QLabel(this);
		strengthLabel_->setWordWrap(true);
		mismatchLabel_ = new QLabel(this);
		mismatchLabel_->setWordWrap(true);
		layout->addWidget(strengthLabel_);
		layout->addWidget(mismatchLabel_);

		// The one thing the user must not miss: stated plainly, in bold, no
		// hardcoded colors (stays readable in both OBS themes).
		auto *warning = new QLabel(obs_module_text("PasswordDialog.NoRecoveryWarning"), this);
		warning->setWordWrap(true);
		QFont bold = warning->font();
		bold.setBold(true);
		warning->setFont(bold);
		layout->addSpacing(4);
		layout->addWidget(warning);
	}

	buttons_ = new QDialogButtonBox(this);
	auto *okButton = buttons_->addButton(obs_module_text("Common.Continue"), QDialogButtonBox::AcceptRole);
	buttons_->addButton(obs_module_text("Common.Cancel"), QDialogButtonBox::RejectRole);
	okButton->setDefault(true);
	layout->addWidget(buttons_);

	connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(passwordEdit_, &QLineEdit::textChanged, this, [this] { refreshState(); });
	if (repeatEdit_)
		connect(repeatEdit_, &QLineEdit::textChanged, this, [this] { refreshState(); });
	connect(showCheck_, &QCheckBox::toggled, this, [this](bool show) {
		const auto echo = show ? QLineEdit::Normal : QLineEdit::Password;
		passwordEdit_->setEchoMode(echo);
		if (repeatEdit_)
			repeatEdit_->setEchoMode(echo);
	});

	if (repeatEdit_) {
		setTabOrder(passwordEdit_, repeatEdit_);
		setTabOrder(repeatEdit_, showCheck_);
	} else {
		setTabOrder(passwordEdit_, showCheck_);
	}
	passwordEdit_->setFocus();
	refreshState();
}

std::string PasswordDialog::passwordUtf8() const
{
	return passwordEdit_->text().normalized(QString::NormalizationForm_C).toUtf8().toStdString();
}

void PasswordDialog::refreshState()
{
	QPushButton *ok = buttons_->buttons().isEmpty() ? nullptr : qobject_cast<QPushButton *>(buttons_->buttons().first());
	std::string password = passwordUtf8();

	bool valid = !password.empty();
	if (mode_ == Mode::Create) {
		using obs_backuper::PasswordStrength;
		const PasswordStrength strength = obs_backuper::EvaluatePasswordStrength(password);

		const char *key = nullptr;
		switch (strength) {
		case PasswordStrength::TooShort:
			key = "PasswordDialog.TooShort";
			break;
		case PasswordStrength::Weak:
			key = "PasswordDialog.Weak";
			break;
		case PasswordStrength::Fair:
			key = "PasswordDialog.Fair";
			break;
		case PasswordStrength::Good:
			key = "PasswordDialog.Good";
			break;
		}
		strengthLabel_->setText(password.empty() ? QString() : QString(obs_module_text(key)));

		const bool mismatch = !repeatEdit_->text().isEmpty() && repeatEdit_->text() != passwordEdit_->text();
		mismatchLabel_->setText(mismatch ? QString(obs_module_text("PasswordDialog.Mismatch")) : QString());

		// Length is a hard rule; weakness is only a warning.
		valid = strength != PasswordStrength::TooShort && repeatEdit_->text() == passwordEdit_->text();
	}
	obs_backuper::crypto::SecureWipe(password);

	if (ok)
		ok->setEnabled(valid);
}

bool PasswordDialog::Ask(QWidget *parent, Mode mode, std::string &outPassword)
{
	PasswordDialog dialog(parent, mode);
	const bool accepted = dialog.exec() == QDialog::Accepted;
	if (accepted)
		outPassword = dialog.passwordUtf8();
	// Best effort: drop the widgets' copies of the text right away.
	dialog.passwordEdit_->clear();
	if (dialog.repeatEdit_)
		dialog.repeatEdit_->clear();
	return accepted;
}
