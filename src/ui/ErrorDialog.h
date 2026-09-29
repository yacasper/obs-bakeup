// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include "../core/ErrorKind.h"

#include <QString>

class QWidget;

// Shows a failure the way a non-technical user can act on it: a localized
// headline plus a plain-language explanation chosen by `kind`, with the raw
// technical message (OS error text, file paths) hidden behind a "Show
// details" button. `extraExplanation`, if non-empty, goes between the
// headline and the kind explanation (used e.g. for "your previous settings
// were put back").
void ShowErrorDialog(QWidget *parent, const QString &headline, obs_backuper::ErrorKind kind,
		     const QString &technicalDetails, const QString &extraExplanation = QString());
