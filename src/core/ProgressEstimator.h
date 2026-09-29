// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace obs_backuper {

// Percent complete for a "current of total" (1-based current, i.e. the file
// being processed right now) progress report. Returns 0 when total is 0, and
// is clamped to [0, 100]. Counts the file in flight as not yet done, so the
// bar reaches 100 only when the operation actually finishes.
int ProgressPercent(std::int64_t current, std::int64_t total);

// Maps a fraction (clamped to [0, 1]) of one phase of a multi-phase operation
// onto the whole operation's [0, 1] scale, given the share [start, end] of the
// whole that phase occupies -- e.g. archiving is 0..0.75 and encrypting
// 0.75..1 of a password-protected backup, so the bar and the ETA cover both.
double MapToOverallFraction(double phaseFraction, double phaseStart, double phaseEnd);

// How an ETA is shown: whole seconds under a minute, otherwise whole minutes
// rounded up ("About 2 min remaining" is friendlier than "About 95 s").
struct EtaDisplay {
	bool inMinutes = false;
	std::int64_t value = 0;
};
EtaDisplay ToEtaDisplay(std::int64_t remainingSeconds);

// Estimates the remaining time from how long `completed` of `total` items took.
// Deliberately conservative: returns nullopt until enough has been observed
// for the number to mean something (fewer than kMinSamples items done or less
// than kMinElapsed elapsed), and once nothing is left.
class ProgressEstimator {
public:
	static constexpr std::int64_t kMinSamples = 3;
	static constexpr std::chrono::milliseconds kMinElapsed{1500};

	// Whole seconds remaining, rounded up.
	static std::optional<std::int64_t> RemainingSeconds(std::int64_t completed, std::int64_t total,
							     std::chrono::milliseconds elapsed);
};

} // namespace obs_backuper
