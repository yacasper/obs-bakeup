// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "SecureFile.h"

#include "Crypto.h"

#include <array>
#include <system_error>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace obs_backuper {

std::FILE *OpenForRead(const std::filesystem::path &path)
{
#if defined(_WIN32)
	return _wfopen(path.c_str(), L"rb");
#else
	return std::fopen(path.c_str(), "rb");
#endif
}

std::FILE *OpenForWrite(const std::filesystem::path &path, bool ownerOnly, bool exclusive)
{
#if defined(_WIN32)
	(void)ownerOnly; // inherited ACL, see the header
	return _wfopen(path.c_str(), exclusive ? L"wbx" : L"wb");
#else
	int flags = O_WRONLY | O_CREAT | (exclusive ? O_EXCL : O_TRUNC);
#ifdef O_CLOEXEC
	flags |= O_CLOEXEC;
#endif
	const mode_t mode = ownerOnly ? (S_IRUSR | S_IWUSR) : (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	const int fd = ::open(path.c_str(), flags, mode);
	if (fd < 0)
		return nullptr;
	std::FILE *file = ::fdopen(fd, "wb");
	if (file == nullptr)
		::close(fd);
	return file;
#endif
}

std::filesystem::path MakeRandomTempPath(const std::filesystem::path &dir, const std::string &prefix,
					 const std::string &suffix)
{
	std::array<std::uint8_t, 12> random{};
	crypto::RandomBytes(random.data(), random.size());

	static const char kHex[] = "0123456789abcdef";
	std::string hex;
	for (const std::uint8_t byte : random) {
		hex.push_back(kHex[byte >> 4]);
		hex.push_back(kHex[byte & 0x0f]);
	}
	return dir / (prefix + hex + suffix);
}

bool RestrictDirectoryToOwner(const std::filesystem::path &dir)
{
#if defined(_WIN32)
	(void)dir;
	return true;
#else
	std::error_code ec;
	std::filesystem::permissions(dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);
	return !ec;
#endif
}

void ScopedFileDeleter::Delete()
{
	if (path_.empty())
		return;
	std::error_code ec;
	std::filesystem::remove(path_, ec);
	path_.clear();
}

} // namespace obs_backuper
