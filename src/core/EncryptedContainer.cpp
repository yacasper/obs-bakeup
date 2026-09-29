// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "EncryptedContainer.h"
#include "PathUtf8.h"

#include "SecureFile.h"

#include <cstdio>
#include <cstring>
#include <system_error>
#include <vector>

namespace obs_backuper {

namespace {

constexpr std::array<std::uint8_t, kContainerMagicSize> kMagic = {'O', 'B', 'S', 'B', 'A', 'K', 0x1A, 0x0A};

using HeaderBytes = std::array<std::uint8_t, kContainerHeaderSize>;

void PutU16(std::uint8_t *out, std::uint16_t v)
{
	out[0] = static_cast<std::uint8_t>(v);
	out[1] = static_cast<std::uint8_t>(v >> 8);
}

void PutU32(std::uint8_t *out, std::uint32_t v)
{
	for (int i = 0; i < 4; ++i)
		out[i] = static_cast<std::uint8_t>(v >> (8 * i));
}

void PutU64(std::uint8_t *out, std::uint64_t v)
{
	for (int i = 0; i < 8; ++i)
		out[i] = static_cast<std::uint8_t>(v >> (8 * i));
}

std::uint16_t GetU16(const std::uint8_t *in)
{
	return static_cast<std::uint16_t>(in[0] | (in[1] << 8));
}

std::uint32_t GetU32(const std::uint8_t *in)
{
	std::uint32_t v = 0;
	for (int i = 0; i < 4; ++i)
		v |= static_cast<std::uint32_t>(in[i]) << (8 * i);
	return v;
}

HeaderBytes SerializeHeader(const ContainerHeader &h)
{
	HeaderBytes out{};
	std::uint8_t *p = out.data();
	std::memcpy(p, kMagic.data(), kMagic.size());
	p += kMagic.size();
	PutU16(p, h.formatVersion);
	p += 2;
	*p++ = h.kdfId;
	PutU32(p, h.kdf.memoryKib);
	p += 4;
	PutU32(p, h.kdf.iterations);
	p += 4;
	PutU32(p, h.kdf.parallelism);
	p += 4;
	std::memcpy(p, h.salt.data(), h.salt.size());
	p += h.salt.size();
	std::memcpy(p, h.noncePrefix.data(), h.noncePrefix.size());
	p += h.noncePrefix.size();
	PutU32(p, h.chunkSize);
	return out;
}

ContainerResult Fail(ErrorKind kind, std::string message)
{
	ContainerResult result;
	result.errorKind = kind;
	result.errorMessage = std::move(message);
	return result;
}

ContainerResult Ok()
{
	ContainerResult result;
	result.success = true;
	return result;
}

// Limits shared by writing and reading: we never produce a file we would
// refuse to read.
bool ParamsWithinLimits(const crypto::KdfParams &kdf, std::uint32_t chunkSize)
{
	return kdf.parallelism >= 1 && kdf.parallelism <= kMaxKdfParallelism && kdf.iterations >= 1 &&
	       kdf.iterations <= kMaxKdfIterations && kdf.memoryKib >= 8u * kdf.parallelism &&
	       kdf.memoryKib <= kMaxKdfMemoryKib && chunkSize >= kMinChunkSize && chunkSize <= kMaxChunkSize;
}

// Parses an already-read header buffer.
ContainerResult ParseHeader(const HeaderBytes &raw, ContainerHeader &out)
{
	if (std::memcmp(raw.data(), kMagic.data(), kMagic.size()) != 0)
		return Fail(ErrorKind::InvalidArchive, "not an encrypted OBS Backuper archive (bad magic)");

	const std::uint8_t *p = raw.data() + kMagic.size();
	ContainerHeader h;
	h.formatVersion = GetU16(p);
	p += 2;
	h.kdfId = *p++;
	h.kdf.memoryKib = GetU32(p);
	p += 4;
	h.kdf.iterations = GetU32(p);
	p += 4;
	h.kdf.parallelism = GetU32(p);
	p += 4;
	std::memcpy(h.salt.data(), p, h.salt.size());
	p += h.salt.size();
	std::memcpy(h.noncePrefix.data(), p, h.noncePrefix.size());
	p += h.noncePrefix.size();
	h.chunkSize = GetU32(p);

	if (h.formatVersion != kContainerFormatVersion) {
		return Fail(ErrorKind::UnsupportedContainerVersion,
			    "unsupported container format version " + std::to_string(h.formatVersion) +
				    " (this plugin reads version " + std::to_string(kContainerFormatVersion) + ")");
	}
	if (h.kdfId != kKdfIdArgon2id)
		return Fail(ErrorKind::UnsupportedContainerVersion, "unknown key derivation function id " +
									    std::to_string(static_cast<int>(h.kdfId)));
	if (!ParamsWithinLimits(h.kdf, h.chunkSize))
		return Fail(ErrorKind::WrongPasswordOrCorrupted, "container header has out-of-range parameters");

	out = h;
	return Ok();
}

crypto::Nonce BlockNonce(const ContainerHeader &h, std::uint64_t blockIndex)
{
	crypto::Nonce nonce{};
	std::memcpy(nonce.data(), h.noncePrefix.data(), h.noncePrefix.size());
	PutU64(nonce.data() + h.noncePrefix.size(), blockIndex);
	return nonce;
}

// Associated data of a block: the whole header plus the "last block" flag.
using BlockAad = std::array<std::uint8_t, kContainerHeaderSize + 1>;

BlockAad MakeAad(const HeaderBytes &header, bool isFinal)
{
	BlockAad aad{};
	std::memcpy(aad.data(), header.data(), header.size());
	aad[header.size()] = isFinal ? 1 : 0;
	return aad;
}

struct FileCloser {
	std::FILE *file = nullptr;
	explicit FileCloser(std::FILE *f) : file(f) {}
	~FileCloser()
	{
		if (file != nullptr)
			std::fclose(file);
	}
	// Closes now, reporting whether buffered data made it out.
	bool Close()
	{
		const bool ok = file == nullptr || std::fclose(file) == 0;
		file = nullptr;
		return ok;
	}
};

struct KeyWiper {
	crypto::Key &key;
	~KeyWiper() { crypto::SecureWipe(key.data(), key.size()); }
};

class ProgressThrottle {
public:
	ProgressThrottle(const ContainerProgressCallback &cb, std::uint64_t total) : cb_(cb), total_(total) {}

	void Report(std::uint64_t processed, bool force = false)
	{
		if (!cb_)
			return;
		const int percent = total_ == 0 ? 100 : static_cast<int>((processed * 100) / total_);
		if (force || percent != lastPercent_) {
			lastPercent_ = percent;
			cb_(processed, total_);
		}
	}

private:
	const ContainerProgressCallback &cb_;
	std::uint64_t total_;
	int lastPercent_ = -1;
};

// Shared tail of both directions: close the output, and on any failure delete
// it so no partial file survives.
ContainerResult Finish(ContainerResult result, FileCloser &out, const std::filesystem::path &outPath)
{
	if (result.success && !out.Close())
		result = Fail(ErrorKind::EncryptionFailed,
			      "failed to flush output file");
	if (!result.success) {
		out.Close();
		std::error_code ec;
		std::filesystem::remove(outPath, ec);
	}
	return result;
}

} // namespace

EncryptionParams DefaultEncryptionParams()
{
	EncryptionParams params;
	params.kdf.memoryKib = 128u * 1024u;
	params.kdf.iterations = 3;
	params.kdf.parallelism = 1;
	params.chunkSize = 64u * 1024u;
	return params;
}

bool IsEncryptedArchive(const std::filesystem::path &path)
{
	std::FILE *file = OpenForRead(path);
	if (file == nullptr)
		return false;
	FileCloser closer(file);

	std::array<std::uint8_t, kContainerMagicSize> head{};
	if (std::fread(head.data(), 1, head.size(), file) != head.size())
		return false;
	return head == kMagic;
}

ContainerResult ReadContainerHeader(const std::filesystem::path &path, ContainerHeader &outHeader)
{
	std::FILE *file = OpenForRead(path);
	if (file == nullptr)
		return Fail(ErrorKind::InvalidArchive, "failed to open file: " + PathToUtf8(path));
	FileCloser closer(file);

	HeaderBytes raw{};
	if (std::fread(raw.data(), 1, raw.size(), file) != raw.size())
		return Fail(ErrorKind::InvalidArchive, "file is too short to be an encrypted archive");
	return ParseHeader(raw, outHeader);
}

ContainerResult EncryptFile(const std::filesystem::path &zipPath, const std::filesystem::path &containerPath,
			    const std::string &password, const ContainerProgressCallback &onProgress,
			    const EncryptionParams &params)
{
	if (password.empty())
		return Fail(ErrorKind::EncryptionFailed, "an empty password is not allowed");
	if (!ParamsWithinLimits(params.kdf, params.chunkSize))
		return Fail(ErrorKind::EncryptionFailed, "encryption parameters are out of range");

	std::error_code sizeEc;
	const std::uint64_t totalBytes = std::filesystem::file_size(zipPath, sizeEc);
	if (sizeEc)
		return Fail(ErrorKind::EncryptionFailed, "failed to read input size: " + sizeEc.message());

	std::FILE *in = OpenForRead(zipPath);
	if (in == nullptr)
		return Fail(ErrorKind::EncryptionFailed, "failed to open input archive: " + PathToUtf8(zipPath));
	FileCloser inCloser(in);

	ContainerHeader header;
	header.kdf = params.kdf;
	header.chunkSize = params.chunkSize;
	if (!crypto::RandomBytes(header.salt.data(), header.salt.size()) ||
	    !crypto::RandomBytes(header.noncePrefix.data(), header.noncePrefix.size()))
		return Fail(ErrorKind::EncryptionFailed, "the system random number generator is unavailable");

	crypto::Key key{};
	KeyWiper wiper{key};
	if (!crypto::DeriveKey(password, header.salt, header.kdf, key))
		return Fail(ErrorKind::EncryptionFailed, "key derivation failed (invalid parameters or out of memory)");

	std::FILE *outFile = OpenForWrite(containerPath, /*ownerOnly=*/false);
	if (outFile == nullptr)
		return Fail(ErrorKind::EncryptionFailed, "failed to create output file: " + PathToUtf8(containerPath));
	FileCloser outCloser(outFile);

	const HeaderBytes headerBytes = SerializeHeader(header);
	if (std::fwrite(headerBytes.data(), 1, headerBytes.size(), outFile) != headerBytes.size())
		return Finish(Fail(ErrorKind::EncryptionFailed, "failed to write output file"), outCloser, containerPath);

	ProgressThrottle progress(onProgress, totalBytes);
	progress.Report(0, true);

	// Read one byte past each block so we know whether the block just read is
	// the last one (a file that is an exact multiple of the block size still
	// gets a properly flagged final block).
	const std::size_t chunk = header.chunkSize;
	std::vector<std::uint8_t> buffer(chunk + 1);
	std::size_t have = std::fread(buffer.data(), 1, buffer.size(), in);

	std::uint64_t blockIndex = 0;
	std::uint64_t processed = 0;
	for (;;) {
		if (std::ferror(in))
			return Finish(Fail(ErrorKind::EncryptionFailed, "failed to read input archive"), outCloser,
				      containerPath);

		const bool isFinal = have <= chunk;
		const std::size_t blockLen = isFinal ? have : chunk;

		const BlockAad aad = MakeAad(headerBytes, isFinal);
		crypto::Mac mac{};
		crypto::AeadEncrypt(key, BlockNonce(header, blockIndex), aad.data(), aad.size(), buffer.data(), blockLen,
				    buffer.data(), mac);

		if (std::fwrite(buffer.data(), 1, blockLen, outFile) != blockLen ||
		    std::fwrite(mac.data(), 1, mac.size(), outFile) != mac.size())
			return Finish(Fail(ErrorKind::EncryptionFailed, "failed to write output file"), outCloser,
				      containerPath);

		processed += blockLen;
		progress.Report(processed, isFinal);
		if (isFinal)
			break;

		// Carry the lookahead byte over and refill.
		buffer[0] = buffer[chunk];
		have = 1 + std::fread(buffer.data() + 1, 1, chunk, in);
		++blockIndex;
	}

	return Finish(Ok(), outCloser, containerPath);
}

ContainerResult DecryptFile(const std::filesystem::path &containerPath, const std::filesystem::path &zipPath,
			    const std::string &password, const ContainerProgressCallback &onProgress)
{
	static const char *const kCorrupted = "wrong password, or the file is damaged or has been modified";

	ContainerHeader header;
	if (const ContainerResult headerResult = ReadContainerHeader(containerPath, header); !headerResult.success)
		return headerResult;

	std::error_code sizeEc;
	const std::uint64_t fileSize = std::filesystem::file_size(containerPath, sizeEc);
	if (sizeEc)
		return Fail(ErrorKind::InvalidArchive, "failed to read file size: " + sizeEc.message());

	// The block structure is fully determined by the file size, so it is
	// validated before any key derivation or output is produced.
	const std::uint64_t payload = fileSize - kContainerHeaderSize;
	const std::uint64_t fullBlock = static_cast<std::uint64_t>(header.chunkSize) + crypto::kMacSize;
	if (fileSize < kContainerHeaderSize + crypto::kMacSize)
		return Fail(ErrorKind::WrongPasswordOrCorrupted, std::string("file is truncated: ") + kCorrupted);
	const std::uint64_t blockCount = (payload + fullBlock - 1) / fullBlock;
	const std::uint64_t lastBlock = payload - (blockCount - 1) * fullBlock;
	if (lastBlock < crypto::kMacSize)
		return Fail(ErrorKind::WrongPasswordOrCorrupted, std::string("file is truncated: ") + kCorrupted);
	const std::uint64_t totalPlain = payload - blockCount * crypto::kMacSize;

	std::FILE *in = OpenForRead(containerPath);
	if (in == nullptr)
		return Fail(ErrorKind::InvalidArchive, "failed to open file: " + PathToUtf8(containerPath));
	FileCloser inCloser(in);

	HeaderBytes headerBytes{};
	if (std::fread(headerBytes.data(), 1, headerBytes.size(), in) != headerBytes.size())
		return Fail(ErrorKind::InvalidArchive, "failed to read container header");

	crypto::Key key{};
	KeyWiper wiper{key};
	if (!crypto::DeriveKey(password, header.salt, header.kdf, key))
		return Fail(ErrorKind::EncryptionFailed, "key derivation failed (out of memory)");

	std::FILE *outFile = OpenForWrite(zipPath, /*ownerOnly=*/true);
	if (outFile == nullptr)
		return Fail(ErrorKind::EncryptionFailed, "failed to create output file: " + PathToUtf8(zipPath));
	FileCloser outCloser(outFile);

	ProgressThrottle progress(onProgress, totalPlain);
	progress.Report(0, true);

	std::vector<std::uint8_t> buffer(header.chunkSize);
	std::uint64_t processed = 0;
	for (std::uint64_t blockIndex = 0; blockIndex < blockCount; ++blockIndex) {
		const bool isFinal = blockIndex + 1 == blockCount;
		const std::size_t cipherLen =
			static_cast<std::size_t>((isFinal ? lastBlock : fullBlock) - crypto::kMacSize);

		crypto::Mac mac{};
		if (std::fread(buffer.data(), 1, cipherLen, in) != cipherLen ||
		    std::fread(mac.data(), 1, mac.size(), in) != mac.size())
			return Finish(Fail(ErrorKind::WrongPasswordOrCorrupted, std::string("read error: ") + kCorrupted),
				      outCloser, zipPath);

		const BlockAad aad = MakeAad(headerBytes, isFinal);
		if (!crypto::AeadDecrypt(key, BlockNonce(header, blockIndex), aad.data(), aad.size(), buffer.data(),
					 cipherLen, mac, buffer.data()))
			return Finish(Fail(ErrorKind::WrongPasswordOrCorrupted, kCorrupted), outCloser, zipPath);

		if (std::fwrite(buffer.data(), 1, cipherLen, outFile) != cipherLen)
			return Finish(Fail(ErrorKind::EncryptionFailed, "failed to write decrypted output"), outCloser, zipPath);

		processed += cipherLen;
		progress.Report(processed, isFinal);
	}

	crypto::SecureWipe(buffer.data(), buffer.size());
	return Finish(Ok(), outCloser, zipPath);
}

} // namespace obs_backuper
