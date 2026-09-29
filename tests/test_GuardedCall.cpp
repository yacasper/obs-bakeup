// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include <catch2/catch_test_macros.hpp>

#include "core/GuardedCall.h"

#include <filesystem>
#include <stdexcept>

using namespace obs_backuper;

namespace {

struct FakeOutcome {
	bool success = false;
	ErrorKind errorKind = ErrorKind::None;
	std::string errorMessage;
	int value = 0;
};

} // namespace

TEST_CASE("RunGuarded passes a normal outcome through untouched", "[GuardedCall]")
{
	const auto outcome = RunGuarded<FakeOutcome>([] {
		FakeOutcome ok;
		ok.success = true;
		ok.value = 42;
		return ok;
	});
	CHECK(outcome.success);
	CHECK(outcome.value == 42);
	CHECK(outcome.errorMessage.empty());
}

TEST_CASE("RunGuarded turns a std::exception into a failed outcome", "[GuardedCall]")
{
	const auto outcome = RunGuarded<FakeOutcome>([]() -> FakeOutcome { throw std::runtime_error("disk exploded"); });
	CHECK_FALSE(outcome.success);
	CHECK(outcome.errorKind == ErrorKind::Unknown);
	CHECK(outcome.errorMessage.find("disk exploded") != std::string::npos);
}

TEST_CASE("RunGuarded catches a filesystem_error and a non-standard exception", "[GuardedCall]")
{
	const auto fromFilesystem = RunGuarded<FakeOutcome>([]() -> FakeOutcome {
		return (void)std::filesystem::file_size("/this/path/does/not/exist-guarded-call"), FakeOutcome{};
	});
	CHECK_FALSE(fromFilesystem.success);
	CHECK(fromFilesystem.errorKind == ErrorKind::Unknown);

	const auto fromOther = RunGuarded<FakeOutcome>([]() -> FakeOutcome { throw 7; });
	CHECK_FALSE(fromOther.success);
	CHECK(fromOther.errorMessage.find("unknown exception") != std::string::npos);
}
