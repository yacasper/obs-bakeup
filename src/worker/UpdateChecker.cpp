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
#include <QProcess>
#include <QStringList>
#include <QUrl>

namespace {
constexpr int kTimeoutMs = 5000;
constexpr int kCurlTimeoutSeconds = 5;

#if defined(__APPLE__)
constexpr const char *kCurlProgram = "/usr/bin/curl";
#else
constexpr const char *kCurlProgram = "curl"; // curl.exe in System32 on current Windows
#endif

QString UserAgent()
{
	return QString("obs-bakeup/%1").arg(PLUGIN_VERSION);
}
} // namespace

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent), network(new QNetworkAccessManager(this)) {}

void UpdateChecker::start()
{
#if defined(__APPLE__)
	startWithCurl();
#else
	startWithQt();
#endif
}

void UpdateChecker::handleBody(const std::string &body)
{
	const auto newer = obs_backuper::FindNewerVersion(body, PLUGIN_VERSION);
	if (!newer) {
		obs_log(LOG_INFO, "update check: this plugin is up to date");
		return;
	}

	obs_log(LOG_INFO, "a newer plugin version is available: %s", newer->ToString().c_str());
	emit updateAvailable(QString::fromStdString(newer->ToString()));
}

void UpdateChecker::startWithQt()
{
	QNetworkRequest request{QUrl(obs_backuper::kLatestReleaseApiUrl)};
	// GitHub rejects API requests that have no User-Agent.
	request.setHeader(QNetworkRequest::UserAgentHeader, UserAgent());
	request.setRawHeader("Accept", "application/vnd.github+json");
	request.setTransferTimeout(kTimeoutMs);

	QNetworkReply *reply = network->get(request);
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();

		if (reply->error() != QNetworkReply::NoError) {
			obs_log(LOG_WARNING, "update check through Qt failed (%s), trying curl",
				reply->errorString().toUtf8().constData());
			startWithCurl();
			return;
		}
		handleBody(reply->readAll().toStdString());
	});
}

void UpdateChecker::startWithCurl()
{
	QStringList arguments;
	for (const auto &argument : obs_backuper::LatestReleaseCurlArguments(UserAgent().toStdString(), kCurlTimeoutSeconds))
		arguments << QString::fromStdString(argument);

	auto *process = new QProcess(this);
	connect(process, &QProcess::errorOccurred, this, [process](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) {
			obs_log(LOG_WARNING, "update check: could not start curl");
			process->deleteLater();
		}
	});
	connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus status) {
		process->deleteLater();

		if (status != QProcess::NormalExit || exitCode != 0) {
			obs_log(LOG_WARNING, "update check: curl failed (exit code %d): %s", exitCode,
				QString::fromUtf8(process->readAllStandardError()).trimmed().toUtf8().constData());
			return;
		}
		handleBody(process->readAllStandardOutput().toStdString());
	});
	process->start(kCurlProgram, arguments);
}
