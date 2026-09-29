// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include "ErrorKind.h"

#include <exception>
#include <string>
#include <utility>

namespace obs_backuper {

// Runs `operation` and returns its outcome. Any exception it lets escape
// (std::filesystem throws on some failures) becomes a failed Outcome instead.
// Left alone, an exception leaving a worker thread calls std::terminate and
// takes the whole of OBS down with no crash report and no explanation.
//
// Outcome needs default construction and the members `success`, `errorKind`
// and `errorMessage`.
template<typename Outcome, typename Operation> Outcome RunGuarded(Operation &&operation)
{
	std::string reason;
	try {
		return std::forward<Operation>(operation)();
	} catch (const std::exception &error) {
		reason = error.what();
	} catch (...) {
		reason = "unknown exception";
	}

	Outcome failed;
	failed.success = false;
	failed.errorKind = ErrorKind::Unknown;
	failed.errorMessage = "unexpected error: " + reason;
	return failed;
}

} // namespace obs_backuper
