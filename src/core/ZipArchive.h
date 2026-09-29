// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <miniz.h>
#include <miniz_zip.h>

namespace obs_backuper {

// Thin RAII wrapper over miniz (mz_zip_writer_*) for sequential ZIP archive
// writing. Does not depend on OBS/Qt — covered by unit tests.
//
// Files are read/written through FILE* (fopen/_wfopen) rather than miniz's
// path-as-char* API (mz_zip_writer_init_file/add_file) — this is the only way
// to correctly open a path with non-ASCII characters (e.g. a Cyrillic
// username in the destination folder path on Windows): std::filesystem::path
// stores the path as wchar_t on Windows, and we open it via _wfopen directly,
// without losing information by converting to a narrow string.
class ZipArchive {
public:
	explicit ZipArchive(std::filesystem::path zipPath);
	~ZipArchive();

	ZipArchive(const ZipArchive &) = delete;
	ZipArchive &operator=(const ZipArchive &) = delete;

	// Creates the archive file and initializes the ZIP writer.
	bool Open(std::string &errorMessage);

	// Adds a file from disk to the archive under archiveRelativePath
	// (separators are normalized to '/', the name is encoded as UTF-8).
	bool AddFile(const std::filesystem::path &absoluteSourcePath, const std::filesystem::path &archiveRelativePath,
		     std::string &errorMessage);

	// Adds in-memory data (used for manifest.json).
	bool AddData(const std::string &data, const std::filesystem::path &archiveRelativePath,
		     std::string &errorMessage);

	// Writes the central directory and closes the file. After this call
	// (whether it succeeds or not), the object can no longer be used to add
	// files.
	bool Close(std::string &errorMessage);

private:
	std::filesystem::path zipPath_;
	mz_zip_archive archive_{};
	std::FILE *file_ = nullptr;
	bool opened_ = false;
	bool closed_ = false;
};

// Thin RAII wrapper over miniz (mz_zip_reader_*) for reading a ZIP archive —
// used by RestoreManager (Stage 4). Mirrors ZipArchive's FILE*-based approach
// for the same non-ASCII-path reason (see the note above).
class ZipReader {
public:
	struct Entry {
		std::filesystem::path relativePath;
		std::uintmax_t uncompressedSize = 0;
	};

	explicit ZipReader(std::filesystem::path zipPath);
	~ZipReader();

	ZipReader(const ZipReader &) = delete;
	ZipReader &operator=(const ZipReader &) = delete;

	// Opens the archive file and initializes the ZIP reader.
	bool Open(std::string &errorMessage);

	// Lists every regular-file entry in the archive (directory entries, which
	// miniz represents as names ending in '/', are skipped -- our own writer
	// never adds them, but a foreign/hand-crafted archive might).
	std::vector<Entry> ListEntries() const;

	// Reads an entry fully into memory (used for manifest.json).
	bool ReadEntryToString(const std::string &archiveRelativePath, std::string &outData, std::string &errorMessage) const;

	// Extracts one entry to destinationPath, creating parent directories as
	// needed. Does not retry on a locked/unwritable destination file --
	// RestoreManager owns that policy (retrying a
	// file locked by another process on Windows).
	bool ExtractEntryToFile(const std::string &archiveRelativePath, const std::filesystem::path &destinationPath,
				 std::string &errorMessage) const;

	// Closes the underlying file. Safe to call multiple times.
	void Close();

private:
	std::filesystem::path zipPath_;
	mutable mz_zip_archive archive_{};
	std::FILE *file_ = nullptr;
	bool opened_ = false;
	bool closed_ = false;
};

} // namespace obs_backuper
