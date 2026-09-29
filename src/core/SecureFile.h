// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <cstdio>
#include <filesystem>
#include <string>

namespace obs_backuper {

// Small file helpers shared by the encryption code and the managers that
// produce temporary plaintext. Paths go through FILE* (_wfopen on Windows) for
// the same non-ASCII-path reason as ZipArchive.

// Opens `path` for binary reading. nullptr on failure.
std::FILE *OpenForRead(const std::filesystem::path &path);

// Creates (or truncates) `path` for binary writing. With ownerOnly, the file
// is created readable/writable by the current user only (mode 0600 on POSIX;
// on Windows the file inherits the ACL of its parent directory, which for the
// user-profile locations the plugin writes to already restricts it to the
// user). With exclusive, fails if the file already exists. nullptr on failure.
std::FILE *OpenForWrite(const std::filesystem::path &path, bool ownerOnly, bool exclusive = false);

// A not-yet-existing path "<dir>/<prefix><random hex><suffix>" (random part
// from the CSPRNG, so it cannot be predicted by another local user). The file
// is NOT created; open it with OpenForWrite(..., exclusive = true).
std::filesystem::path MakeRandomTempPath(const std::filesystem::path &dir, const std::string &prefix,
					 const std::string &suffix);

// Restricts a directory to the current user (mode 0700 on POSIX; a no-op on
// Windows, where the directory inherits its parent's ACL). Best effort --
// returns false if the permissions could not be changed.
bool RestrictDirectoryToOwner(const std::filesystem::path &dir);

// Deletes the file at scope exit unless Release()d -- the guarantee that a
// temporary plaintext archive never outlives the operation, whatever the
// outcome (early return, error, exception).
class ScopedFileDeleter {
public:
	ScopedFileDeleter() = default;
	explicit ScopedFileDeleter(std::filesystem::path path) : path_(std::move(path)) {}
	~ScopedFileDeleter() { Delete(); }

	ScopedFileDeleter(const ScopedFileDeleter &) = delete;
	ScopedFileDeleter &operator=(const ScopedFileDeleter &) = delete;

	void Reset(std::filesystem::path path)
	{
		Delete();
		path_ = std::move(path);
	}
	void Release() { path_.clear(); }
	const std::filesystem::path &path() const { return path_; }

private:
	void Delete();
	std::filesystem::path path_;
};

} // namespace obs_backuper
