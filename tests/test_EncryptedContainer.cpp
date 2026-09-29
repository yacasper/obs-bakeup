// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "core/EncryptedContainer.h"

#include "core/SecureFile.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#ifndef OBS_BACKUPER_SOURCE_DIR
#error "OBS_BACKUPER_SOURCE_DIR must point at the repository root"
#endif

using namespace obs_backuper;
namespace fs = std::filesystem;

namespace {

// Byte offsets inside the 59-byte header (see EncryptedContainer.h).
constexpr std::size_t kOffVersion = 8;
constexpr std::size_t kOffKdfId = 10;
constexpr std::size_t kOffMemory = 11;
constexpr std::size_t kOffIterations = 15;
constexpr std::size_t kOffParallelism = 19;
constexpr std::size_t kOffSalt = 23;
constexpr std::size_t kOffNoncePrefix = 39;
constexpr std::size_t kOffChunkSize = 55;

constexpr std::uint32_t kChunk = 1024;
constexpr std::size_t kBlock = kChunk + crypto::kMacSize;

using Bytes = std::vector<std::uint8_t>;

class TempDirFixture {
public:
	TempDirFixture() : root(fs::temp_directory_path() / "obs-backuper-containertests" / UniqueName())
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

// Cheap KDF and small blocks so the tests stay fast and hit block boundaries
// with tiny inputs. The production defaults are exercised separately.
EncryptionParams FastParams()
{
	EncryptionParams params;
	params.kdf.memoryKib = 64;
	params.kdf.iterations = 1;
	params.kdf.parallelism = 1;
	params.chunkSize = kChunk;
	return params;
}

Bytes Payload(std::size_t size, std::uint32_t seed = 12345)
{
	Bytes out(size);
	std::uint32_t state = seed;
	for (auto &byte : out) {
		state = state * 1664525u + 1013904223u;
		byte = static_cast<std::uint8_t>(state >> 24);
	}
	return out;
}

void WriteBytes(const fs::path &path, const Bytes &data)
{
	fs::create_directories(path.parent_path());
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
}

Bytes ReadBytes(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Encrypts `plain` into <root>/x.obsbak and returns the container bytes.
Bytes Seal(const fs::path &root, const Bytes &plain, const std::string &password = "correct horse")
{
	WriteBytes(root / "in.zip", plain);
	const auto result = EncryptFile(root / "in.zip", root / "x.obsbak", password, {}, FastParams());
	REQUIRE(result.success);
	return ReadBytes(root / "x.obsbak");
}

// Tries to open a (possibly tampered) container; on failure nothing must be
// left at the output path.
ContainerResult TryOpen(const fs::path &root, const Bytes &container, const std::string &password = "correct horse")
{
	WriteBytes(root / "tampered.obsbak", container);
	fs::remove(root / "out.zip");
	const auto result = DecryptFile(root / "tampered.obsbak", root / "out.zip", password);
	if (!result.success)
		CHECK_FALSE(fs::exists(root / "out.zip"));
	return result;
}

void PutU32(Bytes &bytes, std::size_t offset, std::uint32_t value)
{
	for (int i = 0; i < 4; ++i)
		bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

} // namespace

TEST_CASE("Container round-trips byte-identically across block boundaries", "[EncryptedContainer]")
{
	TempDirFixture fixture;

	for (const std::size_t size : {std::size_t{0}, std::size_t{1}, std::size_t{100}, std::size_t{kChunk - 1},
				       std::size_t{kChunk}, std::size_t{kChunk + 1}, std::size_t{2 * kChunk},
				       std::size_t{2 * kChunk + 1}, std::size_t{5 * kChunk + 333}}) {
		INFO("size " << size);
		const Bytes plain = Payload(size);
		const Bytes container = Seal(fixture.root, plain);

		const std::size_t blocks = size == 0 ? 1 : (size + kChunk - 1) / kChunk;
		CHECK(container.size() == kContainerHeaderSize + size + blocks * crypto::kMacSize);

		fs::remove(fixture.root / "out.zip");
		const auto result = DecryptFile(fixture.root / "x.obsbak", fixture.root / "out.zip", "correct horse");
		REQUIRE(result.success);
		CHECK(ReadBytes(fixture.root / "out.zip") == plain);
	}
}

TEST_CASE("Container round-trips with the production KDF parameters", "[EncryptedContainer]")
{
	TempDirFixture fixture;
	const Bytes plain = Payload(200 * 1024);
	WriteBytes(fixture.root / "in.zip", plain);

	REQUIRE(EncryptFile(fixture.root / "in.zip", fixture.root / "x.obsbak", "pässword").success);
	REQUIRE(DecryptFile(fixture.root / "x.obsbak", fixture.root / "out.zip", "pässword").success);
	CHECK(ReadBytes(fixture.root / "out.zip") == plain);
}

TEST_CASE("Container round-trips a file over 100 MB", "[EncryptedContainer][.stress]")
{
	TempDirFixture fixture;
	const Bytes plain = Payload(101u * 1024u * 1024u + 7);
	WriteBytes(fixture.root / "in.zip", plain);

	auto params = FastParams();
	params.chunkSize = 64 * 1024;
	std::uint64_t lastProcessed = 0;
	REQUIRE(EncryptFile(fixture.root / "in.zip", fixture.root / "x.obsbak", "pw-long-file",
			    [&](std::uint64_t done, std::uint64_t total) {
				    CHECK(done >= lastProcessed);
				    CHECK(done <= total);
				    lastProcessed = done;
			    },
			    params)
		    .success);
	CHECK(lastProcessed == plain.size());
	REQUIRE(DecryptFile(fixture.root / "x.obsbak", fixture.root / "out.zip", "pw-long-file").success);
	CHECK(ReadBytes(fixture.root / "out.zip") == plain);
}

TEST_CASE("A wrong password fails with WrongPasswordOrCorrupted and leaves no output", "[EncryptedContainer]")
{
	TempDirFixture fixture;
	const Bytes container = Seal(fixture.root, Payload(3000));

	for (const char *wrong : {"correct horsE", "", " correct horse", "correct horse "}) {
		const auto result = TryOpen(fixture.root, container, wrong);
		CHECK_FALSE(result.success);
		// An empty password is still a wrong password at this layer.
		CHECK(result.errorKind == ErrorKind::WrongPasswordOrCorrupted);
	}
}

TEST_CASE("Flipping any single header byte is detected", "[EncryptedContainer]")
{
	TempDirFixture fixture;
	const Bytes container = Seal(fixture.root, Payload(2500));

	for (std::size_t i = 0; i < kContainerHeaderSize; ++i) {
		INFO("header byte " << i);
		Bytes tampered = container;
		tampered[i] ^= 0x01;
		CHECK_FALSE(TryOpen(fixture.root, tampered).success);
	}
}

TEST_CASE("Flipping a byte in the middle or in the last block is detected", "[EncryptedContainer]")
{
	TempDirFixture fixture;
	const Bytes container = Seal(fixture.root, Payload(5 * kChunk + 100));

	const std::size_t positions[] = {kContainerHeaderSize,			    // first ciphertext byte
					 kContainerHeaderSize + 2 * kBlock + 17,	    // middle of the stream
					 kContainerHeaderSize + 1 * kBlock - 1,	    // a MAC byte of block 0
					 container.size() - 1,			    // last MAC byte
					 container.size() - crypto::kMacSize - 1}; // last ciphertext byte
	for (const std::size_t pos : positions) {
		INFO("byte " << pos);
		Bytes tampered = container;
		tampered[pos] ^= 0x80;
		const auto result = TryOpen(fixture.root, tampered);
		CHECK_FALSE(result.success);
		CHECK(result.errorKind == ErrorKind::WrongPasswordOrCorrupted);
	}
}

TEST_CASE("Truncation is detected, on and off block boundaries", "[EncryptedContainer]")
{
	TempDirFixture fixture;
	const Bytes container = Seal(fixture.root, Payload(3 * kChunk + 50)); // 3 full blocks + a short last one

	const std::size_t keep[] = {0,
				    5,
				    kContainerHeaderSize - 1,
				    kContainerHeaderSize,			 // header only
				    kContainerHeaderSize + 10,
				    kContainerHeaderSize + kBlock,		 // exactly one block
				    kContainerHeaderSize + 3 * kBlock,	 // all full blocks, last one dropped
				    container.size() - 1,
				    container.size() - crypto::kMacSize};
	for (const std::size_t size : keep) {
		INFO("truncated to " << size);
		Bytes tampered(container.begin(), container.begin() + static_cast<std::ptrdiff_t>(size));
		CHECK_FALSE(TryOpen(fixture.root, tampered).success);
	}
}

TEST_CASE("Truncating an exact-multiple file at a block boundary is detected", "[EncryptedContainer]")
{
	// The final block is full-sized here; dropping it leaves a stream whose
	// new last block was authenticated as "not final".
	TempDirFixture fixture;
	const Bytes container = Seal(fixture.root, Payload(3 * kChunk));

	Bytes tampered(container.begin(), container.end() - static_cast<std::ptrdiff_t>(kBlock));
	CHECK_FALSE(TryOpen(fixture.root, tampered).success);
}

TEST_CASE("Appended bytes are detected", "[EncryptedContainer]")
{
	TempDirFixture fixture;

	for (const std::size_t size : {std::size_t{100}, std::size_t{kChunk}, std::size_t{2 * kChunk}}) {
		INFO("plaintext size " << size);
		const Bytes container = Seal(fixture.root, Payload(size));

		for (const std::size_t extra : {std::size_t{1}, std::size_t{crypto::kMacSize}, kBlock, kBlock + 5}) {
			INFO("appended " << extra);
			Bytes tampered = container;
			const Bytes junk = Payload(extra, 99);
			tampered.insert(tampered.end(), junk.begin(), junk.end());
			CHECK_FALSE(TryOpen(fixture.root, tampered).success);
		}
	}
}

TEST_CASE("Reordered or duplicated blocks are detected", "[EncryptedContainer]")
{
	TempDirFixture fixture;
	const Bytes container = Seal(fixture.root, Payload(4 * kChunk + 10));
	const auto block = [&](std::size_t index) {
		const auto begin = container.begin() + static_cast<std::ptrdiff_t>(kContainerHeaderSize + index * kBlock);
		return Bytes(begin, begin + static_cast<std::ptrdiff_t>(kBlock));
	};

	SECTION("two middle blocks swapped")
	{
		Bytes tampered(container.begin(), container.begin() + kContainerHeaderSize);
		for (const std::size_t index : {std::size_t{0}, std::size_t{2}, std::size_t{1}, std::size_t{3}}) {
			const Bytes b = block(index);
			tampered.insert(tampered.end(), b.begin(), b.end());
		}
		tampered.insert(tampered.end(), container.begin() + kContainerHeaderSize + 4 * kBlock, container.end());
		CHECK_FALSE(TryOpen(fixture.root, tampered).success);
	}
	SECTION("a block duplicated")
	{
		Bytes tampered(container.begin(), container.begin() + kContainerHeaderSize);
		for (const std::size_t index : {std::size_t{0}, std::size_t{0}, std::size_t{1}, std::size_t{2},
						std::size_t{3}}) {
			const Bytes b = block(index);
			tampered.insert(tampered.end(), b.begin(), b.end());
		}
		tampered.insert(tampered.end(), container.begin() + kContainerHeaderSize + 4 * kBlock, container.end());
		CHECK_FALSE(TryOpen(fixture.root, tampered).success);
	}
}

TEST_CASE("Blocks from a different container cannot be spliced in", "[EncryptedContainer]")
{
	TempDirFixture fixture;
	const Bytes a = Seal(fixture.root, Payload(3 * kChunk, 1));
	const Bytes b = Seal(fixture.root, Payload(3 * kChunk, 2)); // same password, different salt/nonce

	Bytes spliced = a;
	std::copy(b.begin() + kContainerHeaderSize + kBlock, b.begin() + kContainerHeaderSize + 2 * kBlock,
		  spliced.begin() + kContainerHeaderSize + kBlock);
	CHECK_FALSE(TryOpen(fixture.root, spliced).success);
}

TEST_CASE("Unknown container version and KDF id are reported as unsupported", "[EncryptedContainer]")
{
	TempDirFixture fixture;
	const Bytes container = Seal(fixture.root, Payload(500));

	Bytes futureVersion = container;
	futureVersion[kOffVersion] = 2;
	CHECK(TryOpen(fixture.root, futureVersion).errorKind == ErrorKind::UnsupportedContainerVersion);

	Bytes zeroVersion = container;
	zeroVersion[kOffVersion] = 0;
	CHECK(TryOpen(fixture.root, zeroVersion).errorKind == ErrorKind::UnsupportedContainerVersion);

	Bytes unknownKdf = container;
	unknownKdf[kOffKdfId] = 2;
	CHECK(TryOpen(fixture.root, unknownKdf).errorKind == ErrorKind::UnsupportedContainerVersion);
}

TEST_CASE("Out-of-range KDF and chunk parameters are rejected before any key derivation", "[EncryptedContainer]")
{
	TempDirFixture fixture;
	const Bytes container = Seal(fixture.root, Payload(500));

	struct Case {
		const char *name;
		std::size_t offset;
		std::uint32_t value;
	};
	const Case cases[] = {
		{"memory 4 GiB", kOffMemory, 0x400000u},
		{"memory 0xFFFFFFFF", kOffMemory, 0xFFFFFFFFu},
		{"memory just over the cap", kOffMemory, kMaxKdfMemoryKib + 1},
		{"memory below 8 * lanes", kOffMemory, 4},
		{"iterations 0", kOffIterations, 0},
		{"iterations over the cap", kOffIterations, kMaxKdfIterations + 1},
		{"iterations 0xFFFFFFFF", kOffIterations, 0xFFFFFFFFu},
		{"parallelism 0", kOffParallelism, 0},
		{"parallelism over the cap", kOffParallelism, kMaxKdfParallelism + 1},
		{"chunk 0", kOffChunkSize, 0},
		{"chunk below the minimum", kOffChunkSize, kMinChunkSize - 1},
		{"chunk over the maximum", kOffChunkSize, kMaxChunkSize + 1},
		{"chunk 0xFFFFFFFF", kOffChunkSize, 0xFFFFFFFFu},
	};
	for (const Case &c : cases) {
		INFO(c.name);
		Bytes tampered = container;
		PutU32(tampered, c.offset, c.value);

		WriteBytes(fixture.root / "t.obsbak", tampered);
		ContainerHeader header;
		const auto headerResult = ReadContainerHeader(fixture.root / "t.obsbak", header);
		CHECK_FALSE(headerResult.success);
		CHECK(headerResult.errorKind == ErrorKind::WrongPasswordOrCorrupted);

		const auto result = TryOpen(fixture.root, tampered);
		CHECK_FALSE(result.success);
		CHECK(result.errorKind == ErrorKind::WrongPasswordOrCorrupted);
	}
}

TEST_CASE("ReadContainerHeader parses a valid header and rejects non-containers", "[EncryptedContainer]")
{
	TempDirFixture fixture;
	const Bytes container = Seal(fixture.root, Payload(10));

	ContainerHeader header;
	REQUIRE(ReadContainerHeader(fixture.root / "x.obsbak", header).success);
	CHECK(header.formatVersion == kContainerFormatVersion);
	CHECK(header.kdfId == kKdfIdArgon2id);
	CHECK(header.kdf.memoryKib == 64);
	CHECK(header.kdf.iterations == 1);
	CHECK(header.kdf.parallelism == 1);
	CHECK(header.chunkSize == kChunk);
	CHECK(std::equal(header.salt.begin(), header.salt.end(), container.begin() + kOffSalt));

	WriteBytes(fixture.root / "junk", Payload(200));
	CHECK(ReadContainerHeader(fixture.root / "junk", header).errorKind == ErrorKind::InvalidArchive);
	WriteBytes(fixture.root / "short", Bytes(container.begin(), container.begin() + 20));
	CHECK(ReadContainerHeader(fixture.root / "short", header).errorKind == ErrorKind::InvalidArchive);
	CHECK(ReadContainerHeader(fixture.root / "missing", header).errorKind == ErrorKind::InvalidArchive);
}

TEST_CASE("Encrypting the same file twice gives different salt, nonce and ciphertext", "[EncryptedContainer]")
{
	TempDirFixture fixture;
	const Bytes plain = Payload(2 * kChunk);
	const Bytes first = Seal(fixture.root, plain);
	const Bytes second = Seal(fixture.root, plain);
	REQUIRE(first.size() == second.size());

	const auto same = [&](std::size_t offset, std::size_t length) {
		return std::equal(first.begin() + static_cast<std::ptrdiff_t>(offset),
				  first.begin() + static_cast<std::ptrdiff_t>(offset + length),
				  second.begin() + static_cast<std::ptrdiff_t>(offset));
	};
	CHECK_FALSE(same(kOffSalt, crypto::kSaltSize));
	CHECK_FALSE(same(kOffNoncePrefix, 16));
	CHECK_FALSE(same(kContainerHeaderSize, first.size() - kContainerHeaderSize));
}

TEST_CASE("Identical plaintext blocks encrypt to different ciphertext blocks", "[EncryptedContainer]")
{
	TempDirFixture fixture;
	const Bytes container = Seal(fixture.root, Bytes(4 * kChunk, 0x41));

	const auto block = [&](std::size_t i) {
		const auto begin = container.begin() + static_cast<std::ptrdiff_t>(kContainerHeaderSize + i * kBlock);
		return Bytes(begin, begin + static_cast<std::ptrdiff_t>(kChunk));
	};
	CHECK(block(0) != block(1));
	CHECK(block(1) != block(2));
}

TEST_CASE("IsEncryptedArchive decides by content, not by extension", "[EncryptedContainer]")
{
	TempDirFixture fixture;
	const Bytes container = Seal(fixture.root, Payload(100));

	CHECK(IsEncryptedArchive(fixture.root / "x.obsbak"));

	fs::copy_file(fixture.root / "x.obsbak", fixture.root / "looks-like.zip");
	CHECK(IsEncryptedArchive(fixture.root / "looks-like.zip"));

	WriteBytes(fixture.root / "plain.zip", Bytes{'P', 'K', 3, 4, 20, 0, 0, 0, 8, 0, 1, 2, 3});
	CHECK_FALSE(IsEncryptedArchive(fixture.root / "plain.zip"));

	WriteBytes(fixture.root / "renamed.obsbak", Bytes{'P', 'K', 3, 4, 20, 0, 0, 0, 8, 0});
	CHECK_FALSE(IsEncryptedArchive(fixture.root / "renamed.obsbak"));

	WriteBytes(fixture.root / "empty.obsbak", Bytes{});
	CHECK_FALSE(IsEncryptedArchive(fixture.root / "empty.obsbak"));

	WriteBytes(fixture.root / "garbage.bin", Payload(500));
	CHECK_FALSE(IsEncryptedArchive(fixture.root / "garbage.bin"));

	WriteBytes(fixture.root / "magic-prefix", Bytes(container.begin(), container.begin() + 7));
	CHECK_FALSE(IsEncryptedArchive(fixture.root / "magic-prefix"));

	CHECK_FALSE(IsEncryptedArchive(fixture.root / "does-not-exist"));
	CHECK_FALSE(IsEncryptedArchive(fixture.root)); // a directory
}

TEST_CASE("Passwords with Cyrillic and emoji round-trip; other normalization forms do not", "[EncryptedContainer]")
{
	TempDirFixture fixture;
	const Bytes plain = Payload(1500);
	WriteBytes(fixture.root / "in.zip", plain);

	for (const std::string &password : {std::string("п\xd0\xb0р\xd0\xbeль-\xd1\x82\xd0\xb5\xd1\x81\xd1\x82"),
					   std::string("correct \xf0\x9f\x94\x90 horse \xf0\x9f\x8e\xa5"),
					   std::string("\xf0\x9f\x98\x80")}) {
		REQUIRE(EncryptFile(fixture.root / "in.zip", fixture.root / "u.obsbak", password, {}, FastParams()).success);
		fs::remove(fixture.root / "out.zip");
		REQUIRE(DecryptFile(fixture.root / "u.obsbak", fixture.root / "out.zip", password).success);
		CHECK(ReadBytes(fixture.root / "out.zip") == plain);
	}

	// Normalization is the caller's job (the UI normalizes to NFC); at this
	// layer the password is just bytes, so NFC and NFD spellings differ.
	const std::string nfc = "caf\xc3\xa9-password";
	const std::string nfd = "cafe\xcc\x81-password";
	REQUIRE(EncryptFile(fixture.root / "in.zip", fixture.root / "n.obsbak", nfc, {}, FastParams()).success);
	CHECK_FALSE(DecryptFile(fixture.root / "n.obsbak", fixture.root / "out2.zip", nfd).success);
	CHECK(DecryptFile(fixture.root / "n.obsbak", fixture.root / "out2.zip", nfc).success);
}

TEST_CASE("Paths with non-ASCII characters work", "[EncryptedContainer]")
{
	TempDirFixture fixture;
	const fs::path dir = fixture.root / fs::u8path("\xd0\x9f\xd0\xb0\xd0\xbf\xd0\xba\xd0\xb0 \xf0\x9f\x93\x81");
	const Bytes plain = Payload(3000);
	WriteBytes(dir / fs::u8path("\xd0\xb1\xd1\x8d\xd0\xba\xd0\xb0\xd0\xbf.zip"), plain);

	const auto in = dir / fs::u8path("\xd0\xb1\xd1\x8d\xd0\xba\xd0\xb0\xd0\xbf.zip");
	const auto enc = dir / fs::u8path("\xd0\xb1\xd1\x8d\xd0\xba\xd0\xb0\xd0\xbf.obsbak");
	const auto out = dir / fs::u8path("\xd0\xb2\xd0\xbe\xd1\x81\xd1\x81\xd1\x82.zip");

	REQUIRE(EncryptFile(in, enc, "pw", {}, FastParams()).success);
	CHECK(IsEncryptedArchive(enc));
	REQUIRE(DecryptFile(enc, out, "pw").success);
	CHECK(ReadBytes(out) == plain);
}

TEST_CASE("EncryptFile rejects bad input and leaves no output", "[EncryptedContainer]")
{
	TempDirFixture fixture;
	WriteBytes(fixture.root / "in.zip", Payload(100));

	SECTION("empty password")
	{
		const auto r = EncryptFile(fixture.root / "in.zip", fixture.root / "x.obsbak", "", {}, FastParams());
		CHECK_FALSE(r.success);
		CHECK(r.errorKind == ErrorKind::EncryptionFailed);
		CHECK_FALSE(fs::exists(fixture.root / "x.obsbak"));
	}
	SECTION("missing input")
	{
		const auto r = EncryptFile(fixture.root / "nope.zip", fixture.root / "x.obsbak", "pw", {}, FastParams());
		CHECK_FALSE(r.success);
		CHECK(r.errorKind == ErrorKind::EncryptionFailed);
		CHECK_FALSE(fs::exists(fixture.root / "x.obsbak"));
	}
	SECTION("unwritable output location")
	{
		const auto r = EncryptFile(fixture.root / "in.zip", fixture.root / "no-such-dir" / "x.obsbak", "pw", {},
					   FastParams());
		CHECK_FALSE(r.success);
		CHECK(r.errorKind == ErrorKind::EncryptionFailed);
	}
	SECTION("parameters we would refuse to read back")
	{
		auto params = FastParams();
		params.kdf.memoryKib = kMaxKdfMemoryKib + 1;
		CHECK_FALSE(EncryptFile(fixture.root / "in.zip", fixture.root / "x.obsbak", "pw", {}, params).success);
		params = FastParams();
		params.chunkSize = 10;
		CHECK_FALSE(EncryptFile(fixture.root / "in.zip", fixture.root / "x.obsbak", "pw", {}, params).success);
		CHECK_FALSE(fs::exists(fixture.root / "x.obsbak"));
	}
}

TEST_CASE("DecryptFile on a non-container fails with InvalidArchive", "[EncryptedContainer]")
{
	TempDirFixture fixture;
	WriteBytes(fixture.root / "plain.zip", Bytes{'P', 'K', 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
	const auto r = DecryptFile(fixture.root / "plain.zip", fixture.root / "out.zip", "pw");
	CHECK_FALSE(r.success);
	CHECK(r.errorKind == ErrorKind::InvalidArchive);
	CHECK_FALSE(fs::exists(fixture.root / "out.zip"));
}

TEST_CASE("Ciphertext does not contain the plaintext or the password, and errors do not echo the password",
	  "[EncryptedContainer]")
{
	TempDirFixture fixture;
	const std::string password = "S3cret-\xd0\xbf\xd0\xb0\xd1\x80\xd0\xbe\xd0\xbb\xd1\x8c-needle";
	const std::string marker = "STREAM-KEY-live_1234567890abcdef";
	Bytes plain(2000, 'x');
	std::copy(marker.begin(), marker.end(), plain.begin() + 500);

	WriteBytes(fixture.root / "in.zip", plain);
	REQUIRE(EncryptFile(fixture.root / "in.zip", fixture.root / "x.obsbak", password, {}, FastParams()).success);
	const Bytes container = ReadBytes(fixture.root / "x.obsbak");

	const auto contains = [&](const std::string &needle) {
		return std::search(container.begin(), container.end(), needle.begin(), needle.end()) != container.end();
	};
	CHECK_FALSE(contains(marker));
	CHECK_FALSE(contains(password));

	// Every failure message, for a range of failure causes, stays password-free.
	Bytes truncated(container.begin(), container.begin() + 100);
	Bytes flipped = container;
	flipped[kContainerHeaderSize + 3] ^= 1;
	for (const Bytes &bad : {truncated, flipped, container}) {
		WriteBytes(fixture.root / "bad.obsbak", bad);
		const auto r = DecryptFile(fixture.root / "bad.obsbak", fixture.root / "o.zip", password + "-wrong");
		CHECK(r.errorMessage.find(password) == std::string::npos);
		CHECK(r.errorMessage.find("needle") == std::string::npos);
	}
	const auto empty = EncryptFile(fixture.root / "missing.zip", fixture.root / "y.obsbak", password, {}, FastParams());
	CHECK(empty.errorMessage.find(password) == std::string::npos);
}

TEST_CASE("Progress is reported monotonically and ends at the total", "[EncryptedContainer]")
{
	TempDirFixture fixture;
	const Bytes plain = Payload(50 * kChunk + 5);
	WriteBytes(fixture.root / "in.zip", plain);

	for (const bool encrypting : {true, false}) {
		std::vector<std::uint64_t> seen;
		std::uint64_t reportedTotal = 0;
		const auto cb = [&](std::uint64_t done, std::uint64_t total) {
			seen.push_back(done);
			reportedTotal = total;
		};
		if (encrypting) {
			REQUIRE(EncryptFile(fixture.root / "in.zip", fixture.root / "x.obsbak", "pw", cb, FastParams()).success);
		} else {
			REQUIRE(DecryptFile(fixture.root / "x.obsbak", fixture.root / "out.zip", "pw", cb).success);
		}
		REQUIRE_FALSE(seen.empty());
		CHECK(seen.front() == 0);
		CHECK(seen.back() == plain.size());
		CHECK(reportedTotal == plain.size());
		CHECK(std::is_sorted(seen.begin(), seen.end()));
		CHECK(seen.size() <= 102); // at most ~once per percent, plus start/end
	}
}

#if !defined(_WIN32)
TEST_CASE("A decrypted file is readable by its owner only", "[EncryptedContainer]")
{
	TempDirFixture fixture;
	Seal(fixture.root, Payload(100));
	REQUIRE(DecryptFile(fixture.root / "x.obsbak", fixture.root / "out.zip", "correct horse").success);

	struct stat st {};
	REQUIRE(::stat((fixture.root / "out.zip").c_str(), &st) == 0);
	CHECK((st.st_mode & 0777) == 0600);
}
#endif

TEST_CASE("A container written by version 1 of the plugin stays readable", "[EncryptedContainer][Compat]")
{
	// tests/data/compat-v1.obsbak was produced once by the "Regenerate the
	// compatibility vector" test below and is committed: if this fails, the
	// on-disk format changed incompatibly.
	const fs::path vector = fs::path(OBS_BACKUPER_SOURCE_DIR) / "tests" / "data" / "compat-v1.obsbak";
	REQUIRE(fs::exists(vector));

	TempDirFixture fixture;
	CHECK(IsEncryptedArchive(vector));

	const std::string password = "\xd1\x82\xd0\xb5\xd1\x81\xd1\x82-\xd0\xbf\xd0\xb0\xd1\x80\xd0\xbe\xd0\xbb\xd1\x8c \xf0\x9f\x94\x90";
	REQUIRE(DecryptFile(vector, fixture.root / "out.bin", password).success);

	std::string expected = "OBS Backuper compatibility vector v1\n";
	expected.resize(2500, '.');
	const Bytes actual = ReadBytes(fixture.root / "out.bin");
	CHECK(std::string(actual.begin(), actual.end()) == expected);

	CHECK_FALSE(DecryptFile(vector, fixture.root / "out2.bin", "wrong").success);
}

TEST_CASE("Regenerate the compatibility vector", "[EncryptedContainer][.regenerate]")
{
	// Run explicitly ("[.regenerate]") only when the format version is bumped
	// on purpose; never as part of the normal suite.
	TempDirFixture fixture;
	std::string plain = "OBS Backuper compatibility vector v1\n";
	plain.resize(2500, '.');
	WriteBytes(fixture.root / "in.bin", Bytes(plain.begin(), plain.end()));

	const std::string password = "\xd1\x82\xd0\xb5\xd1\x81\xd1\x82-\xd0\xbf\xd0\xb0\xd1\x80\xd0\xbe\xd0\xbb\xd1\x8c \xf0\x9f\x94\x90";
	const fs::path target = fs::path(OBS_BACKUPER_SOURCE_DIR) / "tests" / "data" / "compat-v1.obsbak";
	fs::create_directories(target.parent_path());
	fs::remove(target);
	REQUIRE(EncryptFile(fixture.root / "in.bin", target, password, {}, FastParams()).success);
}
