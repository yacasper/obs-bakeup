// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include <catch2/catch_test_macros.hpp>

#include "core/Crypto.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

using namespace obs_backuper::crypto;

namespace {

// Small parameters keep the key derivation fast in tests.
const KdfParams kFastParams{/*memoryKib=*/64, /*iterations=*/1, /*parallelism=*/1};

std::array<std::uint8_t, kSaltSize> Salt(std::uint8_t fill)
{
	std::array<std::uint8_t, kSaltSize> salt{};
	salt.fill(fill);
	return salt;
}

Key KeyFor(const std::string &password, std::uint8_t saltFill = 1)
{
	Key key{};
	REQUIRE(DeriveKey(password, Salt(saltFill), kFastParams, key));
	return key;
}

Nonce NonceFilled(std::uint8_t fill)
{
	Nonce nonce{};
	nonce.fill(fill);
	return nonce;
}

} // namespace

TEST_CASE("RandomBytes fills the buffer and differs between calls", "[Crypto]")
{
	std::array<std::uint8_t, 32> a{};
	std::array<std::uint8_t, 32> b{};
	REQUIRE(RandomBytes(a.data(), a.size()));
	REQUIRE(RandomBytes(b.data(), b.size()));

	CHECK(a != b);
	CHECK(std::any_of(a.begin(), a.end(), [](std::uint8_t v) { return v != 0; }));
	CHECK(RandomBytes(a.data(), 0));
}

TEST_CASE("SecureWipe zeroes raw memory and string contents", "[Crypto]")
{
	std::array<std::uint8_t, 16> bytes;
	bytes.fill(0xAB);
	SecureWipe(bytes.data(), bytes.size());
	CHECK(std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t v) { return v == 0; }));

	std::string secret = "hunter2-hunter2";
	SecureWipe(secret);
	CHECK(std::all_of(secret.begin(), secret.end(), [](char c) { return c == 0; }));

	std::string empty;
	SecureWipe(empty);
	SecureWipe(nullptr, 0);
}

TEST_CASE("DeriveKey is deterministic and depends on password, salt and parameters", "[Crypto]")
{
	const Key base = KeyFor("correct horse");

	CHECK(KeyFor("correct horse") == base);
	CHECK(KeyFor("correct horsf") != base);
	CHECK(KeyFor("correct horse", 2) != base);

	Key moreIterations{};
	REQUIRE(DeriveKey("correct horse", Salt(1), {64, 2, 1}, moreIterations));
	CHECK(moreIterations != base);
}

TEST_CASE("DeriveKey accepts non-ASCII and empty passwords", "[Crypto]")
{
	const std::string cyrillic = "\xD0\xBF\xD0\xB0\xD1\x80\xD0\xBE\xD0\xBB\xD1\x8C";
	CHECK(KeyFor(cyrillic) != KeyFor("parol"));
	CHECK(KeyFor("") != KeyFor("x"));
}

TEST_CASE("DeriveKey rejects invalid parameters", "[Crypto]")
{
	Key key{};
	CHECK_FALSE(DeriveKey("pw", Salt(1), {0, 1, 1}, key));
	CHECK_FALSE(DeriveKey("pw", Salt(1), {64, 0, 1}, key));
	CHECK_FALSE(DeriveKey("pw", Salt(1), {64, 1, 0}, key));
	CHECK_FALSE(DeriveKey("pw", Salt(1), {4, 1, 1}, key)); // memory < 8 * parallelism
	CHECK_FALSE(DeriveKey("pw", Salt(1), {64, 1, 16}, key));
}

TEST_CASE("AeadEncrypt and AeadDecrypt round-trip, including in place", "[Crypto]")
{
	const Key key = KeyFor("pw");
	const Nonce nonce = NonceFilled(7);
	const std::vector<std::uint8_t> associated = {1, 2, 3};
	const std::string text = "stream key: live_123456";
	const std::vector<std::uint8_t> plain(text.begin(), text.end());

	std::vector<std::uint8_t> cipher(plain.size());
	Mac mac{};
	AeadEncrypt(key, nonce, associated.data(), associated.size(), plain.data(), plain.size(), cipher.data(), mac);
	CHECK(cipher != plain);

	std::vector<std::uint8_t> decrypted(plain.size());
	REQUIRE(AeadDecrypt(key, nonce, associated.data(), associated.size(), cipher.data(), cipher.size(), mac,
			    decrypted.data()));
	CHECK(decrypted == plain);

	// The buffers may alias, as the container code does.
	std::vector<std::uint8_t> buffer = plain;
	AeadEncrypt(key, nonce, associated.data(), associated.size(), buffer.data(), buffer.size(), buffer.data(), mac);
	REQUIRE(AeadDecrypt(key, nonce, associated.data(), associated.size(), buffer.data(), buffer.size(), mac,
			    buffer.data()));
	CHECK(buffer == plain);
}

TEST_CASE("AeadDecrypt rejects a wrong key, nonce, associated data, tampered text or tampered MAC", "[Crypto]")
{
	const Key key = KeyFor("pw");
	const Nonce nonce = NonceFilled(7);
	const std::vector<std::uint8_t> associated = {1, 2, 3};
	const std::vector<std::uint8_t> plain = {10, 20, 30, 40, 50};

	std::vector<std::uint8_t> cipher(plain.size());
	Mac mac{};
	AeadEncrypt(key, nonce, associated.data(), associated.size(), plain.data(), plain.size(), cipher.data(), mac);

	std::vector<std::uint8_t> out(plain.size());
	CHECK_FALSE(AeadDecrypt(KeyFor("other"), nonce, associated.data(), associated.size(), cipher.data(),
				cipher.size(), mac, out.data()));
	CHECK_FALSE(AeadDecrypt(key, NonceFilled(8), associated.data(), associated.size(), cipher.data(), cipher.size(),
				mac, out.data()));

	const std::vector<std::uint8_t> otherAssociated = {1, 2, 4};
	CHECK_FALSE(AeadDecrypt(key, nonce, otherAssociated.data(), otherAssociated.size(), cipher.data(), cipher.size(),
				mac, out.data()));

	auto tamperedCipher = cipher;
	tamperedCipher[2] ^= 1;
	CHECK_FALSE(AeadDecrypt(key, nonce, associated.data(), associated.size(), tamperedCipher.data(),
				tamperedCipher.size(), mac, out.data()));

	Mac tamperedMac = mac;
	tamperedMac[0] ^= 1;
	CHECK_FALSE(AeadDecrypt(key, nonce, associated.data(), associated.size(), cipher.data(), cipher.size(),
				tamperedMac, out.data()));
}

TEST_CASE("AEAD handles an empty message and no associated data", "[Crypto]")
{
	const Key key = KeyFor("pw");
	const Nonce nonce = NonceFilled(3);
	Mac mac{};
	AeadEncrypt(key, nonce, nullptr, 0, nullptr, 0, nullptr, mac);

	CHECK(AeadDecrypt(key, nonce, nullptr, 0, nullptr, 0, mac, nullptr));

	Mac wrong = mac;
	wrong[15] ^= 0x80;
	CHECK_FALSE(AeadDecrypt(key, nonce, nullptr, 0, nullptr, 0, wrong, nullptr));
}
