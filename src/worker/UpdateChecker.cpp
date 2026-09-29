// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "UpdateChecker.h"

#include <obs-module.h>

#include "../core/UpdateCheck.h"
#include "../plugin-support.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {
constexpr int kTimeoutMs = 5000;
}

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent), network(new QNetworkAccessManager(this)) {}

void UpdateChecker::start()
{
	QNetworkRequest request{QUrl(obs_backuper::kLatestReleaseApiUrl)};
	// GitHub rejects API requests that have no User-Agent.
	request.setHeader(QNetworkRequest::UserAgentHeader, QString("obs-bakeup/%1").arg(PLUGIN_VERSION));
	request.setRawHeader("Accept", "application/vnd.github+json");
	request.setTransferTimeout(kTimeoutMs);

	QNetworkReply *reply = network->get(request);
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			obs_log(LOG_DEBUG, "update check skipped: %s", reply->errorString().toUtf8().constData());
			return;
		}

		const std::string body = reply->readAll().toStdString();
		const auto newer = obs_backuper::FindNewerVersion(body, PLUGIN_VERSION);
		if (!newer) {
			obs_log(LOG_DEBUG, "update check: plugin is up to date");
			return;
		}

		obs_log(LOG_INFO, "a newer plugin version is available: %s", newer->ToString().c_str());
		emit updateAvailable(QString::fromStdString(newer->ToString()));
	});
}
