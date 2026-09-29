// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace obs_backuper::crypto {

// Thin wrapper over Monocypher (Argon2id + XChaCha20-Poly1305) -- no
// cryptography of our own, just typed, bounds-checked access to the library's
// primitives. Used only by EncryptedContainer.

constexpr std::size_t kKeySize = 32;
constexpr std::size_t kSaltSize = 16;
constexpr std::size_t kNonceSize = 24; // XChaCha20
constexpr std::size_t kMacSize = 16;   // Poly1305

using Key = std::array<std::uint8_t, kKeySize>;
using Nonce = std::array<std::uint8_t, kNonceSize>;
using Mac = std::array<std::uint8_t, kMacSize>;

struct KdfParams {
	std::uint32_t memoryKib = 0;   // Argon2 memory, KiB (== number of 1 KiB blocks)
	std::uint32_t iterations = 0;  // Argon2 passes
	std::uint32_t parallelism = 0; // Argon2 lanes
};

// Fills `out` with cryptographically secure random bytes from the OS CSPRNG.
// Returns false if the OS refuses (never falls back to a weak generator).
bool RandomBytes(std::uint8_t *out, std::size_t size);

// Overwrites memory in a way the compiler may not optimize away.
void SecureWipe(void *data, std::size_t size);

// Wipes the string's current contents (its buffer, not just its length).
void SecureWipe(std::string &value);

// Argon2id(password, salt) -> 32-byte key. Returns false if the parameters are
// invalid (zero, or memory < 8 * parallelism) or the work memory could not be
// allocated. Callers must validate parameters that come from untrusted input
// first (see EncryptedContainer's limits) -- this does not cap them.
bool DeriveKey(const std::string &password, const std::array<std::uint8_t, kSaltSize> &salt, const KdfParams &params,
	       Key &outKey);

// Encrypts `size` bytes. `cipherText` must have room for `size` bytes and may
// alias `plainText`.
void AeadEncrypt(const Key &key, const Nonce &nonce, const std::uint8_t *associatedData, std::size_t associatedDataSize,
		 const std::uint8_t *plainText, std::size_t size, std::uint8_t *cipherText, Mac &outMac);

// Verifies (constant time, inside the library) and decrypts. Returns false --
// and writes nothing meaningful to `plainText` -- if authentication fails.
bool AeadDecrypt(const Key &key, const Nonce &nonce, const std::uint8_t *associatedData,
		 std::size_t associatedDataSize, const std::uint8_t *cipherText, std::size_t size, const Mac &mac,
		 std::uint8_t *plainText);

} // namespace obs_backuper::crypto
