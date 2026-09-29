// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "Crypto.h"

#include <monocypher.h>

#include <cstdlib>
#include <limits>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX // windows.h's min/max macros break std::numeric_limits<>::max()
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/random.h>
#endif
#endif

namespace obs_backuper::crypto {

bool RandomBytes(std::uint8_t *out, std::size_t size)
{
#if defined(_WIN32)
	while (size > 0) {
		const ULONG chunk = static_cast<ULONG>(size > 0x10000000u ? 0x10000000u : size);
		if (BCryptGenRandom(nullptr, out, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
			return false;
		out += chunk;
		size -= chunk;
	}
	return true;
#else
	// getentropy() is limited to 256 bytes per call.
	while (size > 0) {
		const std::size_t chunk = size > 256 ? 256 : size;
		if (getentropy(out, chunk) != 0)
			return false;
		out += chunk;
		size -= chunk;
	}
	return true;
#endif
}

void SecureWipe(void *data, std::size_t size)
{
	if (data != nullptr && size > 0)
		crypto_wipe(data, size);
}

void SecureWipe(std::string &value)
{
	if (!value.empty())
		crypto_wipe(&value[0], value.size());
}

bool DeriveKey(const std::string &password, const std::array<std::uint8_t, kSaltSize> &salt, const KdfParams &params,
	       Key &outKey)
{
	if (params.iterations == 0 || params.parallelism == 0 || params.memoryKib < 8u * params.parallelism)
		return false;
	if (password.size() > std::numeric_limits<std::uint32_t>::max())
		return false;

	const std::size_t workBytes = static_cast<std::size_t>(params.memoryKib) * 1024u;
	void *workArea = std::malloc(workBytes);
	if (workArea == nullptr)
		return false;

	const crypto_argon2_config config = {CRYPTO_ARGON2_ID, params.memoryKib, params.iterations, params.parallelism};
	const crypto_argon2_inputs inputs = {reinterpret_cast<const std::uint8_t *>(password.data()), salt.data(),
					     static_cast<std::uint32_t>(password.size()),
					     static_cast<std::uint32_t>(salt.size())};

	crypto_argon2(outKey.data(), static_cast<std::uint32_t>(outKey.size()), workArea, config, inputs,
		      crypto_argon2_no_extras);

	// The work area holds password-derived state; scrub it before releasing.
	crypto_wipe(workArea, workBytes);
	std::free(workArea);
	return true;
}

void AeadEncrypt(const Key &key, const Nonce &nonce, const std::uint8_t *associatedData, std::size_t associatedDataSize,
		 const std::uint8_t *plainText, std::size_t size, std::uint8_t *cipherText, Mac &outMac)
{
	crypto_aead_lock(cipherText, outMac.data(), key.data(), nonce.data(), associatedData, associatedDataSize,
			 plainText, size);
}

bool AeadDecrypt(const Key &key, const Nonce &nonce, const std::uint8_t *associatedData,
		 std::size_t associatedDataSize, const std::uint8_t *cipherText, std::size_t size, const Mac &mac,
		 std::uint8_t *plainText)
{
	return crypto_aead_unlock(plainText, mac.data(), key.data(), nonce.data(), associatedData, associatedDataSize,
				  cipherText, size) == 0;
}

} // namespace obs_backuper::crypto
