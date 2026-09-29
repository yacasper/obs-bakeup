// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "ProgressEstimator.h"

#include <algorithm>

namespace obs_backuper {

int ProgressPercent(std::int64_t current, std::int64_t total)
{
	if (total <= 0)
		return 0;

	const std::int64_t done = std::clamp<std::int64_t>(current - 1, 0, total);
	return static_cast<int>(done * 100 / total);
}

double MapToOverallFraction(double phaseFraction, double phaseStart, double phaseEnd)
{
	const double clamped = std::min(1.0, std::max(0.0, phaseFraction));
	return phaseStart + clamped * (phaseEnd - phaseStart);
}

EtaDisplay ToEtaDisplay(std::int64_t remainingSeconds)
{
	if (remainingSeconds < 0)
		remainingSeconds = 0;
	if (remainingSeconds < 60)
		return {false, remainingSeconds};
	return {true, (remainingSeconds + 59) / 60};
}

std::optional<std::int64_t> ProgressEstimator::RemainingSeconds(std::int64_t completed, std::int64_t total,
								 std::chrono::milliseconds elapsed)
{
	if (total <= 0 || completed < kMinSamples || completed >= total || elapsed < kMinElapsed)
		return std::nullopt;

	const double msPerItem = static_cast<double>(elapsed.count()) / static_cast<double>(completed);
	const double remainingMs = msPerItem * static_cast<double>(total - completed);
	return static_cast<std::int64_t>((remainingMs + 999.0) / 1000.0);
}

} // namespace obs_backuper
