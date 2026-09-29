// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "core/ProgressEstimator.h"

#include <catch2/catch_test_macros.hpp>

using namespace obs_backuper;
using std::chrono::milliseconds;

TEST_CASE("ProgressPercent counts the file in flight as not yet done", "[ProgressEstimator]")
{
	CHECK(ProgressPercent(1, 10) == 0);
	CHECK(ProgressPercent(6, 10) == 50);
	CHECK(ProgressPercent(10, 10) == 90);
}

TEST_CASE("ProgressPercent is clamped and safe for degenerate input", "[ProgressEstimator]")
{
	CHECK(ProgressPercent(0, 10) == 0);
	CHECK(ProgressPercent(-5, 10) == 0);
	CHECK(ProgressPercent(99, 10) == 100);
	CHECK(ProgressPercent(5, 0) == 0);
	CHECK(ProgressPercent(5, -1) == 0);
}

TEST_CASE("RemainingSeconds extrapolates from the average time per item", "[ProgressEstimator]")
{
	// 5 of 10 done in 5 s -> 1 s per item -> 5 s left.
	CHECK(ProgressEstimator::RemainingSeconds(5, 10, milliseconds(5000)) == 5);
}

TEST_CASE("RemainingSeconds rounds up to whole seconds", "[ProgressEstimator]")
{
	// 4 of 8 in 2 s -> 0.5 s per item -> 2 s left exactly.
	CHECK(ProgressEstimator::RemainingSeconds(4, 8, milliseconds(2000)) == 2);
	// 3 of 10 in 2 s -> 666.6 ms per item * 7 = 4666 ms -> 5 s.
	CHECK(ProgressEstimator::RemainingSeconds(3, 10, milliseconds(2000)) == 5);
}

TEST_CASE("RemainingSeconds gives no estimate until enough has been observed", "[ProgressEstimator]")
{
	CHECK_FALSE(ProgressEstimator::RemainingSeconds(2, 100, milliseconds(10000)).has_value()); // too few samples
	CHECK_FALSE(ProgressEstimator::RemainingSeconds(50, 100, milliseconds(1000)).has_value()); // too little time
	CHECK(ProgressEstimator::RemainingSeconds(3, 100, milliseconds(1500)).has_value());        // both thresholds met
}

TEST_CASE("RemainingSeconds gives no estimate when nothing is left or the total is invalid", "[ProgressEstimator]")
{
	CHECK_FALSE(ProgressEstimator::RemainingSeconds(10, 10, milliseconds(5000)).has_value());
	CHECK_FALSE(ProgressEstimator::RemainingSeconds(11, 10, milliseconds(5000)).has_value());
	CHECK_FALSE(ProgressEstimator::RemainingSeconds(5, 0, milliseconds(5000)).has_value());
}

TEST_CASE("ToEtaDisplay switches to minutes (rounded up) from one minute on", "[ProgressEstimator]")
{
	auto e = ToEtaDisplay(0);
	CHECK_FALSE(e.inMinutes);
	CHECK(e.value == 0);

	e = ToEtaDisplay(59);
	CHECK_FALSE(e.inMinutes);
	CHECK(e.value == 59);

	e = ToEtaDisplay(60);
	CHECK(e.inMinutes);
	CHECK(e.value == 1);

	e = ToEtaDisplay(61);
	CHECK(e.inMinutes);
	CHECK(e.value == 2);

	e = ToEtaDisplay(-3);
	CHECK_FALSE(e.inMinutes);
	CHECK(e.value == 0);
}
