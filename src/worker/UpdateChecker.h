// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;

// Asks GitHub for the latest release of the plugin, once, in the background
// (QNetworkAccessManager is asynchronous, so the UI never blocks). Emits
// updateAvailable() only if the latest release is newer than this build; every
// failure (offline, rate limit, bad response) is logged at debug level and
// otherwise ignored -- the plugin must work exactly the same without internet.
class UpdateChecker : public QObject {
	Q_OBJECT

public:
	explicit UpdateChecker(QObject *parent = nullptr);

	void start();

signals:
	void updateAvailable(const QString &version);

private:
	QNetworkAccessManager *network;
};
