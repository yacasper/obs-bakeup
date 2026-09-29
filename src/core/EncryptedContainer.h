// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "Crypto.h"
#include "ErrorKind.h"

namespace obs_backuper {

// Password-protected backup container (".obsbak", Stage 7): a whole ZIP
// backup encrypted as one authenticated stream, so neither the contents nor
// file names nor manifest.json are readable without the password. The
// container wraps an ordinary ZIP, so the archive code is unchanged. The whole
// header is authenticated as AAD of every block (weakening the KDF parameters
// breaks authentication), the block number in the nonce prevents reordering and
// duplication, and the "last block" flag prevents truncation and appending.
// Header limits are checked before any key derivation.
//
//   header (59 bytes, all integers little-endian)
//     magic[8]  format_ver u16  kdf_id u8  memory_kib u32  iterations u32
//     parallelism u32  salt[16]  nonce_prefix[16]  chunk_size u32
//   then AEAD blocks: XChaCha20-Poly1305, each = ciphertext (<= chunk_size)
//   followed by its 16-byte MAC; every block but the last carries exactly
//   chunk_size plaintext bytes. Nonce = nonce_prefix || block index (u64 LE);
//   associated data = the entire header || final-block flag (1 byte).

constexpr std::size_t kContainerMagicSize = 8;
constexpr std::size_t kContainerHeaderSize = 59;
constexpr std::uint16_t kContainerFormatVersion = 1;
constexpr std::uint8_t kKdfIdArgon2id = 1;

// Upper bounds accepted from an (untrusted) file header, checked BEFORE any
// key derivation so a crafted file cannot make us allocate gigabytes or spin
// for minutes.
constexpr std::uint32_t kMaxKdfMemoryKib = 1024u * 1024u; // 1 GiB
constexpr std::uint32_t kMaxKdfIterations = 10;
constexpr std::uint32_t kMaxKdfParallelism = 4;
constexpr std::uint32_t kMinChunkSize = 1024;
constexpr std::uint32_t kMaxChunkSize = 16u * 1024u * 1024u;

struct EncryptionParams {
	crypto::KdfParams kdf;
	std::uint32_t chunkSize = 0;
};

// Production parameters: Argon2id 128 MiB x 3 passes, 64 KiB blocks --
// roughly 0.5 s of key derivation on a typical desktop machine.
EncryptionParams DefaultEncryptionParams();

struct ContainerHeader {
	std::uint16_t formatVersion = kContainerFormatVersion;
	std::uint8_t kdfId = kKdfIdArgon2id;
	crypto::KdfParams kdf;
	std::array<std::uint8_t, crypto::kSaltSize> salt{};
	std::array<std::uint8_t, 16> noncePrefix{};
	std::uint32_t chunkSize = 0;
};

struct ContainerResult {
	bool success = false;
	ErrorKind errorKind = ErrorKind::None;
	std::string errorMessage; // English, technical; never contains the password
};

// processedBytes/totalBytes of the plaintext stream. Called at most about once
// per percent (and at the end); may be invoked from a background thread.
using ContainerProgressCallback = std::function<void(std::uint64_t processedBytes, std::uint64_t totalBytes)>;

// True if the file starts with the container magic. Decided by content, never
// by extension. False for missing/unreadable/short files.
bool IsEncryptedArchive(const std::filesystem::path &path);

// Reads and validates the header: magic, format version, KDF id and the KDF /
// chunk-size limits. Does not need (or check) the password. Failure kinds:
// InvalidArchive (not a container / truncated header),
// UnsupportedContainerVersion (unknown version or KDF id),
// WrongPasswordOrCorrupted (parameters outside the accepted limits).
ContainerResult ReadContainerHeader(const std::filesystem::path &path, ContainerHeader &outHeader);

// Streams `zipPath` into a new container at `containerPath`. Fresh random salt
// and nonce prefix every call. On any failure the partial output is deleted.
// An empty password is rejected (EncryptionFailed).
ContainerResult EncryptFile(const std::filesystem::path &zipPath, const std::filesystem::path &containerPath,
			    const std::string &password, const ContainerProgressCallback &onProgress = {},
			    const EncryptionParams &params = DefaultEncryptionParams());

// Streams a container into `zipPath` (created readable by the current user
// only). Every failure -- wrong password, damaged, truncated, reordered,
// extended -- returns WrongPasswordOrCorrupted, and the partial output is
// deleted, so no partly decrypted file is ever left behind.
ContainerResult DecryptFile(const std::filesystem::path &containerPath, const std::filesystem::path &zipPath,
			    const std::string &password, const ContainerProgressCallback &onProgress = {});

} // namespace obs_backuper
