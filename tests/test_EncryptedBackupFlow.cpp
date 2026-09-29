// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

// Stage 7 integration: password-protected CreateBackup -> ValidateArchive ->
// PerformStagedRestore -> CommitStagedRestore, plus the "no plaintext leftovers,
// no password anywhere" guarantees on every path.
#include "core/BackupManager.h"
#include "core/EncryptedContainer.h"
#include "core/FileCollector.h"
#include "core/PasswordStrength.h"
#include "core/PluginSettings.h"
#include "core/ProgressEstimator.h"
#include "core/RestoreManager.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

using namespace obs_backuper;
namespace fs = std::filesystem;

namespace {

class TempDirFixture {
public:
	TempDirFixture() : root(fs::temp_directory_path() / "obs-backuper-encflowtests" / UniqueName())
	{
		fs::create_directories(root);
	}
	~TempDirFixture() { fs::remove_all(root.parent_path()); }

	const fs::path root;

private:
	static std::string UniqueName()
	{
		static int counter = 0;
		return "fixture-" + std::to_string(++counter);
	}
};

const std::string kPassword = "\xd0\xbf\xd0\xb0\xd1\x80\xd0\xbe\xd0\xbb\xd1\x8c-SECRET-\xf0\x9f\x94\x90";
constexpr const char *kStreamKey = "live_1234567890_StreamKeyValue";

void WriteFile(const fs::path &path, const std::string &content)
{
	fs::create_directories(path.parent_path());
	std::ofstream file(path, std::ios::binary);
	file << content;
}

std::string ReadFile(const fs::path &path)
{
	std::ifstream file(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::set<std::string> ListNames(const fs::path &dir)
{
	std::set<std::string> names;
	for (const auto &entry : fs::directory_iterator(dir))
		names.insert(entry.path().filename().string());
	return names;
}

EncryptionParams FastParams()
{
	EncryptionParams params;
	params.kdf = {64, 1, 1};
	params.chunkSize = 4096;
	return params;
}

fs::path MakeSourceConfig(const fs::path &sourceDir, const std::string &marker = "original")
{
	WriteFile(sourceDir / "global.ini", "[General]\nmarker=" + marker + "\n");
	WriteFile(sourceDir / "basic" / "scenes" / "Record.json", R"({"scene":")" + marker + R"("})");
	WriteFile(sourceDir / "basic" / "profiles" / "Untitled" / "service.json",
		  std::string(R"({"settings":{"key":")") + kStreamKey + R"("}})");
	return sourceDir;
}

BackupOptions EncryptedOptions(const fs::path &dest, const std::string &password = kPassword)
{
	BackupOptions options;
	options.destinationDir = dest;
	options.pluginVersion = "1.2.3";
	options.obsVersion = "30.2.0";
	options.sourceOs = "macos";
	options.password = password;
	options.encryptionParams = FastParams();
	return options;
}

RestoreOptions MakeRestoreOptions(const fs::path &archive, const fs::path &target, const fs::path &safetyDir,
				   const std::string &password)
{
	RestoreOptions options;
	options.archivePath = archive;
	options.targetDir = target;
	options.safetyBackupDir = safetyDir;
	options.pluginVersion = "1.2.3";
	options.obsVersion = "30.2.0";
	options.sourceOs = "macos";
	options.password = password;
	options.fileWriteMaxAttempts = 1;
	options.fileWriteRetryDelayMs = 0;
	return options;
}

bool HasDecryptedTemp(const fs::path &dir)
{
	if (!fs::exists(dir))
		return false;
	for (const auto &name : ListNames(dir)) {
		if (name.rfind(".obs-backuper-", 0) == 0)
			return true;
	}
	return false;
}

} // namespace

TEST_CASE("An encrypted backup is a lone .obsbak with no plaintext archive left behind", "[EncryptedBackup]")
{
	TempDirFixture fixture;
	const auto source = MakeSourceConfig(fixture.root / "obs-studio");
	const auto dest = fixture.root / "backups";

	const auto outcome = BackupManager::CreateBackup(CollectFiles(source), EncryptedOptions(dest));
	REQUIRE(outcome.success);
	CHECK(outcome.archivePath.extension() == ".obsbak");
	CHECK(IsEncryptedArchive(outcome.archivePath));
	CHECK(outcome.archiveSizeBytes == fs::file_size(outcome.archivePath));

	CHECK(ListNames(dest) == std::set<std::string>{outcome.archivePath.filename().string()});

	// The stream key must not be visible anywhere in the file.
	CHECK(ReadFile(outcome.archivePath).find(kStreamKey) == std::string::npos);
	CHECK(ReadFile(outcome.archivePath).find("manifest.json") == std::string::npos);
	CHECK(ReadFile(outcome.archivePath).find("service.json") == std::string::npos);
}

TEST_CASE("Without a password the backup is the same plain .zip as before", "[EncryptedBackup]")
{
	TempDirFixture fixture;
	const auto source = MakeSourceConfig(fixture.root / "obs-studio");

	BackupOptions options = EncryptedOptions(fixture.root / "backups");
	options.password.clear();
	const auto outcome = BackupManager::CreateBackup(CollectFiles(source), options);

	REQUIRE(outcome.success);
	CHECK(outcome.archivePath.extension() == ".zip");
	CHECK_FALSE(IsEncryptedArchive(outcome.archivePath));
	CHECK(RestoreManager::ValidateArchive(outcome.archivePath).valid);
	CHECK(ListNames(fixture.root / "backups").size() == 1);
}

TEST_CASE("Encrypted backup file names get a unique suffix on collision", "[EncryptedBackup]")
{
	TempDirFixture fixture;
	const auto source = MakeSourceConfig(fixture.root / "obs-studio");
	const auto collected = CollectFiles(source);

	const auto first = BackupManager::CreateBackup(collected, EncryptedOptions(fixture.root / "b"));
	const auto second = BackupManager::CreateBackup(collected, EncryptedOptions(fixture.root / "b"));
	REQUIRE(first.success);
	REQUIRE(second.success);
	CHECK(first.archivePath != second.archivePath);
	CHECK(ListNames(fixture.root / "b").size() == 2);
}

TEST_CASE("Encryption progress is reported during an encrypted backup", "[EncryptedBackup]")
{
	TempDirFixture fixture;
	const auto source = MakeSourceConfig(fixture.root / "obs-studio");

	std::uint64_t lastDone = 0, lastTotal = 0;
	int calls = 0;
	BackupOptions options = EncryptedOptions(fixture.root / "b");
	options.onEncryptProgress = [&](std::uint64_t done, std::uint64_t total) {
		lastDone = done;
		lastTotal = total;
		++calls;
	};
	REQUIRE(BackupManager::CreateBackup(CollectFiles(source), options).success);
	CHECK(calls >= 2);
	CHECK(lastDone == lastTotal);
	CHECK(lastTotal > 0);
}

TEST_CASE("A failing encryption leaves neither output nor temporary plaintext", "[EncryptedBackup]")
{
	TempDirFixture fixture;
	const auto source = MakeSourceConfig(fixture.root / "obs-studio");
	const auto dest = fixture.root / "backups";

	BackupOptions options = EncryptedOptions(dest);
	options.encryptionParams.kdf.memoryKib = kMaxKdfMemoryKib + 1; // rejected by EncryptFile
	const auto outcome = BackupManager::CreateBackup(CollectFiles(source), options);

	CHECK_FALSE(outcome.success);
	CHECK(outcome.errorKind == ErrorKind::EncryptionFailed);
	CHECK(ListNames(dest).empty());
	CHECK(outcome.errorMessage.find(kPassword) == std::string::npos);
}

TEST_CASE("ValidateArchive tells plain, encrypted-without-password and wrong-password apart", "[EncryptedBackup]")
{
	TempDirFixture fixture;
	const auto source = MakeSourceConfig(fixture.root / "obs-studio");
	const auto outcome = BackupManager::CreateBackup(CollectFiles(source), EncryptedOptions(fixture.root / "b"));
	REQUIRE(outcome.success);
	const auto tempDir = fixture.root / "tmp";

	SECTION("no password: asks for one, and touches nothing")
	{
		const auto result = RestoreManager::ValidateArchive(outcome.archivePath, "", tempDir);
		CHECK_FALSE(result.valid);
		CHECK(result.passwordRequired);
		CHECK_FALSE(fs::exists(tempDir));
	}
	SECTION("wrong password")
	{
		const auto result = RestoreManager::ValidateArchive(outcome.archivePath, "nope", tempDir);
		CHECK_FALSE(result.valid);
		CHECK_FALSE(result.passwordRequired);
		CHECK(result.errorKind == ErrorKind::WrongPasswordOrCorrupted);
		CHECK_FALSE(HasDecryptedTemp(tempDir));
	}
	SECTION("right password: valid manifest, temporary plaintext deleted")
	{
		std::uint64_t reported = 0;
		const auto result = RestoreManager::ValidateArchive(outcome.archivePath, kPassword, tempDir,
								     [&](std::uint64_t done, std::uint64_t) { reported = done; });
		REQUIRE(result.valid);
		CHECK(result.manifest.pluginVersion == "1.2.3");
		CHECK(result.manifest.obsVersion == "30.2.0");
		CHECK(reported > 0);
		CHECK_FALSE(HasDecryptedTemp(tempDir));
	}
	SECTION("a plain zip needs no password")
	{
		BackupOptions plain = EncryptedOptions(fixture.root / "p");
		plain.password.clear();
		const auto zip = BackupManager::CreateBackup(CollectFiles(source), plain);
		const auto result = RestoreManager::ValidateArchive(zip.archivePath);
		CHECK(result.valid);
		CHECK_FALSE(result.passwordRequired);
	}
}

TEST_CASE("ValidateArchive reports damaged and future-version containers", "[EncryptedBackup]")
{
	TempDirFixture fixture;
	const auto source = MakeSourceConfig(fixture.root / "obs-studio");
	const auto outcome = BackupManager::CreateBackup(CollectFiles(source), EncryptedOptions(fixture.root / "b"));
	REQUIRE(outcome.success);

	std::string bytes = ReadFile(outcome.archivePath);

	SECTION("future container version is reported before asking for a password")
	{
		std::string future = bytes;
		future[8] = 9;
		WriteFile(fixture.root / "future.obsbak", future);
		const auto result = RestoreManager::ValidateArchive(fixture.root / "future.obsbak");
		CHECK_FALSE(result.valid);
		CHECK_FALSE(result.passwordRequired);
		CHECK(result.errorKind == ErrorKind::UnsupportedContainerVersion);
	}
	SECTION("a damaged body surfaces as wrong-password-or-corrupted")
	{
		bytes[bytes.size() / 2] ^= 0x10;
		WriteFile(fixture.root / "bad.obsbak", bytes);
		const auto result = RestoreManager::ValidateArchive(fixture.root / "bad.obsbak", kPassword, fixture.root / "t");
		CHECK(result.errorKind == ErrorKind::WrongPasswordOrCorrupted);
		CHECK_FALSE(HasDecryptedTemp(fixture.root / "t"));
	}
}

TEST_CASE("Encrypted backup -> validate -> staged restore -> commit reproduces the config", "[EncryptedBackup]")
{
	TempDirFixture fixture;
	const auto source = MakeSourceConfig(fixture.root / "src" / "obs-studio", "original");
	const auto backup = BackupManager::CreateBackup(CollectFiles(source), EncryptedOptions(fixture.root / "b"));
	REQUIRE(backup.success);

	// The live config has since drifted.
	const auto target = MakeSourceConfig(fixture.root / "live" / "obs-studio", "changed-later");
	const auto safetyDir = fixture.root / "live" / "obs-backuper-safety";
	const auto stagingDir = safetyDir / "pending-restore-staging";

	const auto validation = RestoreManager::ValidateArchive(backup.archivePath, kPassword, safetyDir);
	REQUIRE(validation.valid);
	CHECK_FALSE(HasDecryptedTemp(safetyDir));

	int progressCalls = 0;
	std::uint64_t decryptedBytes = 0;
	auto options = MakeRestoreOptions(backup.archivePath, target, safetyDir, kPassword);
	options.onDecryptProgress = [&](std::uint64_t done, std::uint64_t) { decryptedBytes = done; };
	const auto staged = RestoreManager::PerformStagedRestore(
		options, stagingDir, [&](std::size_t, std::size_t, const fs::path &) { ++progressCalls; });
	REQUIRE(staged.success);
	CHECK(progressCalls > 0);
	CHECK(decryptedBytes > 0);

	// Decrypted plaintext exists only inside staging -- no temporary ZIP.
	CHECK_FALSE(HasDecryptedTemp(safetyDir));
	CHECK(ReadFile(target / "global.ini").find("changed-later") != std::string::npos); // untouched so far
	CHECK(ReadFile(stagingDir / "global.ini").find("original") != std::string::npos);
	CHECK(fs::exists(staged.safetyBackupPath));
	CHECK_FALSE(IsEncryptedArchive(staged.safetyBackupPath)); // safety backups stay plain

#if !defined(_WIN32)
	struct stat st {};
	REQUIRE(::stat(stagingDir.c_str(), &st) == 0);
	CHECK((st.st_mode & 0777) == 0700);
#endif

	// The handoff marker carries no password and staging alone suffices.
	PendingRestoreMarker marker;
	marker.stagingDir = staged.stagingDir;
	marker.targetDir = target;
	marker.safetyBackupPath = staged.safetyBackupPath;
	const auto markerPath = safetyDir / "pending-restore.marker";
	std::string error;
	REQUIRE(RestoreManager::WritePendingRestoreMarker(markerPath, marker, error));
	CHECK(ReadFile(markerPath).find(kPassword) == std::string::npos);
	CHECK(ReadFile(markerPath).find("SECRET") == std::string::npos);

	const auto commit = RestoreManager::CommitStagedRestore(stagingDir, target, staged.safetyBackupPath, 1, 0);
	REQUIRE(commit.success);
	CHECK_FALSE(fs::exists(stagingDir));
	CHECK(ReadFile(target / "global.ini") == ReadFile(source / "global.ini"));
	CHECK(ReadFile(target / "basic" / "scenes" / "Record.json") == ReadFile(source / "basic" / "scenes" / "Record.json"));
	CHECK(ReadFile(target / "basic" / "profiles" / "Untitled" / "service.json") ==
	      ReadFile(source / "basic" / "profiles" / "Untitled" / "service.json"));

	// Nothing but the safety backup and the (still present) marker in the safety dir.
	for (const auto &name : ListNames(safetyDir))
		CHECK(name.rfind(".obs-backuper-", 0) != 0);
}

TEST_CASE("A staged restore with a wrong password changes nothing and leaves no plaintext", "[EncryptedBackup]")
{
	TempDirFixture fixture;
	const auto source = MakeSourceConfig(fixture.root / "src" / "obs-studio");
	const auto backup = BackupManager::CreateBackup(CollectFiles(source), EncryptedOptions(fixture.root / "b"));
	REQUIRE(backup.success);

	const auto target = MakeSourceConfig(fixture.root / "live" / "obs-studio", "live");
	const auto safetyDir = fixture.root / "live" / "obs-backuper-safety";
	const auto stagingDir = safetyDir / "pending-restore-staging";

	for (const std::string &pw : {std::string("zzz-not-it"), std::string()}) {
		const auto staged = RestoreManager::PerformStagedRestore(
			MakeRestoreOptions(backup.archivePath, target, safetyDir, pw), stagingDir);
		CHECK_FALSE(staged.success);
		CHECK(staged.errorMessage.find("zzz-not-it") == std::string::npos);
		CHECK_FALSE(fs::exists(stagingDir));
		CHECK_FALSE(HasDecryptedTemp(safetyDir));
		// No safety backup either: the failure happens before anything is touched.
		CHECK(staged.safetyBackupPath.empty());
	}
	CHECK(ReadFile(target / "global.ini").find("live") != std::string::npos);
}

TEST_CASE("A direct PerformRestore of an encrypted backup works and cleans up", "[EncryptedBackup]")
{
	TempDirFixture fixture;
	const auto source = MakeSourceConfig(fixture.root / "src" / "obs-studio", "original");
	const auto backup = BackupManager::CreateBackup(CollectFiles(source), EncryptedOptions(fixture.root / "b"));
	REQUIRE(backup.success);

	const auto target = MakeSourceConfig(fixture.root / "live" / "obs-studio", "live");
	const auto safetyDir = fixture.root / "live" / "obs-backuper-safety";

	const auto outcome =
		RestoreManager::PerformRestore(MakeRestoreOptions(backup.archivePath, target, safetyDir, kPassword));
	REQUIRE(outcome.success);
	CHECK(ReadFile(target / "global.ini") == ReadFile(source / "global.ini"));
	CHECK_FALSE(HasDecryptedTemp(safetyDir));

	const auto bad = RestoreManager::PerformRestore(MakeRestoreOptions(backup.archivePath, target, safetyDir, "bad"));
	CHECK_FALSE(bad.success);
	CHECK(bad.errorKind == ErrorKind::WrongPasswordOrCorrupted);
	CHECK_FALSE(bad.rolledBack);
	CHECK_FALSE(HasDecryptedTemp(safetyDir));
}

TEST_CASE("RemoveStaleTemporaryFiles deletes only decrypted-archive leftovers", "[EncryptedBackup]")
{
	TempDirFixture fixture;
	const auto dir = fixture.root / "safety";
	WriteFile(dir / ".obs-backuper-decrypted-aaaa.tmp", "plaintext");
	WriteFile(dir / ".obs-backuper-decrypted-bbbb.tmp", "plaintext");
	WriteFile(dir / "obs-backup_before-restore_2026-01-01_0000.zip", "keep");
	WriteFile(dir / "pending-restore.marker", "keep");
	WriteFile(dir / ".obs-backuper-decrypted-note.txt", "keep (wrong suffix)");
	fs::create_directories(dir / "pending-restore-staging");

	RestoreManager::RemoveStaleTemporaryFiles(dir);

	CHECK(ListNames(dir) == std::set<std::string>{"obs-backup_before-restore_2026-01-01_0000.zip",
						       "pending-restore.marker", ".obs-backuper-decrypted-note.txt",
						       "pending-restore-staging"});
	RestoreManager::RemoveStaleTemporaryFiles(fixture.root / "missing"); // must not throw
}

TEST_CASE("The password never appears in any outcome message", "[EncryptedBackup]")
{
	TempDirFixture fixture;
	const auto source = MakeSourceConfig(fixture.root / "src" / "obs-studio");
	const auto collected = CollectFiles(source);

	std::vector<std::string> messages;
	const auto backup = BackupManager::CreateBackup(collected, EncryptedOptions(fixture.root / "b"));
	REQUIRE(backup.success);
	messages.push_back(backup.errorMessage);

	BackupOptions broken = EncryptedOptions(fixture.root / "b2");
	broken.encryptionParams.chunkSize = 3;
	messages.push_back(BackupManager::CreateBackup(collected, broken).errorMessage);

	const auto target = fixture.root / "live" / "obs-studio";
	const auto safetyDir = fixture.root / "live" / "obs-backuper-safety";
	for (const std::string &pw : {kPassword + "x", std::string("SECRET"), kPassword}) {
		messages.push_back(RestoreManager::ValidateArchive(backup.archivePath, pw, safetyDir).errorMessage);
		messages.push_back(RestoreManager::PerformStagedRestore(
					   MakeRestoreOptions(backup.archivePath, target, safetyDir, pw),
					   safetyDir / "stage")
					   .errorMessage);
	}
	for (const auto &message : messages) {
		CHECK(message.find("SECRET") == std::string::npos);
		CHECK(message.find(kPassword) == std::string::npos);
	}
}

TEST_CASE("PluginSettings remembers the checkbox but has no place for a password", "[EncryptedBackup]")
{
	TempDirFixture fixture;
	const auto path = fixture.root / "settings.ini";

	CHECK_FALSE(PluginSettings::Load(path).encryptBackups); // default off

	PluginSettings settings;
	settings.encryptBackups = true;
	std::string error;
	REQUIRE(settings.Save(path, error));
	CHECK(PluginSettings::Load(path).encryptBackups);
	CHECK(ReadFile(path).find("assword") == std::string::npos);

	settings.encryptBackups = false;
	REQUIRE(settings.Save(path, error));
	CHECK_FALSE(PluginSettings::Load(path).encryptBackups);
}

TEST_CASE("Password strength: length is counted in characters, not bytes", "[PasswordStrength]")
{
	CHECK(Utf8Length("") == 0);
	CHECK(Utf8Length("abc") == 3);
	CHECK(Utf8Length("\xd0\xbf\xd0\xb0\xd1\x80\xd0\xbe\xd0\xbb\xd1\x8c") == 6); // 6 Cyrillic letters, 12 bytes
	CHECK(Utf8Length("\xf0\x9f\x94\x90\xf0\x9f\x94\x90") == 2);

	// 6 Cyrillic letters are 12 bytes but still too short.
	CHECK(EvaluatePasswordStrength("\xd0\xbf\xd0\xb0\xd1\x80\xd0\xbe\xd0\xbb\xd1\x8c") == PasswordStrength::TooShort);
	CHECK(EvaluatePasswordStrength("") == PasswordStrength::TooShort);
	CHECK(EvaluatePasswordStrength("abc1234") == PasswordStrength::TooShort); // 7
	CHECK(EvaluatePasswordStrength("abc12345") != PasswordStrength::TooShort); // 8
}

TEST_CASE("Password strength: soft warnings for guessable passwords, none for good ones", "[PasswordStrength]")
{
	CHECK(EvaluatePasswordStrength("password") == PasswordStrength::Weak);
	CHECK(EvaluatePasswordStrength("PASSWORD") == PasswordStrength::Weak);
	CHECK(EvaluatePasswordStrength("12345678") == PasswordStrength::Weak);
	CHECK(EvaluatePasswordStrength("aaaaaaaaaaaa") == PasswordStrength::Weak);
	CHECK(EvaluatePasswordStrength("abababababab") == PasswordStrength::Weak);
	CHECK(EvaluatePasswordStrength("lowercase") == PasswordStrength::Weak);

	CHECK(EvaluatePasswordStrength("Tr0ub4dor&3") == PasswordStrength::Fair);
	CHECK(EvaluatePasswordStrength("correct horse battery staple") == PasswordStrength::Good);
	CHECK(EvaluatePasswordStrength("Xk9#mQ2$vL7!pR4z") == PasswordStrength::Good);
	CHECK(EvaluatePasswordStrength("\xd1\x81\xd0\xbb\xd0\xbe\xd0\xb6\xd0\xbd\xd1\x8b\xd0\xb9-\xd0\xbf\xd0\xb0\xd1\x80\xd0\xbe\xd0\xbb\xd1\x8c-2026") ==
	      PasswordStrength::Good);
}

TEST_CASE("MapToOverallFraction places a phase inside the whole operation", "[ProgressEstimator]")
{
	CHECK(MapToOverallFraction(0.0, 0.0, 0.75) == Catch::Approx(0.0));
	CHECK(MapToOverallFraction(1.0, 0.0, 0.75) == Catch::Approx(0.75));
	CHECK(MapToOverallFraction(0.0, 0.75, 1.0) == Catch::Approx(0.75));
	CHECK(MapToOverallFraction(0.5, 0.75, 1.0) == Catch::Approx(0.875));
	CHECK(MapToOverallFraction(1.0, 0.75, 1.0) == Catch::Approx(1.0));
	CHECK(MapToOverallFraction(-3.0, 0.25, 1.0) == Catch::Approx(0.25)); // clamped
	CHECK(MapToOverallFraction(7.0, 0.25, 1.0) == Catch::Approx(1.0));
}

TEST_CASE("A staged restore reports the safety backup as its own progress stage, before extraction",
	  "[EncryptedBackup][Restore]")
{
	for (const bool encrypted : {false, true}) {
		INFO("encrypted: " << encrypted);
		TempDirFixture fixture;
		const auto source = MakeSourceConfig(fixture.root / "src" / "obs-studio");
		BackupOptions backupOptions = EncryptedOptions(fixture.root / "b");
		if (!encrypted)
			backupOptions.password.clear();
		const auto backup = BackupManager::CreateBackup(CollectFiles(source), backupOptions);
		REQUIRE(backup.success);

		const auto target = MakeSourceConfig(fixture.root / "live" / "obs-studio", "live");
		const auto safetyDir = fixture.root / "live" / "obs-backuper-safety";

		// Order of events: decrypt (if any) -> safety backup files -> extraction files.
		std::vector<std::string> events;
		std::size_t lastSafetyCurrent = 0, safetyTotal = 0;
		auto options = MakeRestoreOptions(backup.archivePath, target, safetyDir, encrypted ? kPassword : "");
		options.onDecryptProgress = [&](std::uint64_t done, std::uint64_t total) {
			if (done == total)
				events.push_back("decrypted");
		};
		options.onSafetyBackupProgress = [&](std::size_t current, std::size_t total, const fs::path &) {
			events.push_back("safety");
			lastSafetyCurrent = current;
			safetyTotal = total;
		};
		const auto staged = RestoreManager::PerformStagedRestore(
			options, safetyDir / "stage", [&](std::size_t, std::size_t, const fs::path &) {
				events.push_back("extract");
			});
		REQUIRE(staged.success);

		CHECK(safetyTotal == 3); // global.ini, Record.json, service.json of the live config
		CHECK(lastSafetyCurrent == safetyTotal);

		const auto first = [&](const char *name) {
			return std::find(events.begin(), events.end(), name) - events.begin();
		};
		const auto last = [&](const char *name) {
			return static_cast<std::ptrdiff_t>(events.rend() - std::find(events.rbegin(), events.rend(), name)) - 1;
		};
		REQUIRE(first("safety") < static_cast<std::ptrdiff_t>(events.size()));
		REQUIRE(first("extract") < static_cast<std::ptrdiff_t>(events.size()));
		CHECK(last("safety") < first("extract"));
		if (encrypted)
			CHECK(last("decrypted") < first("safety"));
	}
}
