// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "ZipArchive.h"

#include <cstdint>
#include <cstring>

namespace obs_backuper {

namespace {

std::FILE *OpenNative(const std::filesystem::path &path, const wchar_t *wideMode, const char *narrowMode)
{
#if defined(_WIN32)
	(void)narrowMode;
	return _wfopen(path.c_str(), wideMode);
#else
	(void)wideMode;
	return std::fopen(path.c_str(), narrowMode);
#endif
}

std::string LastMinizError(mz_zip_archive &archive)
{
	return mz_zip_get_error_string(mz_zip_get_last_error(&archive));
}

std::int64_t Ftell64(std::FILE *file)
{
#if defined(_WIN32)
	return _ftelli64(file);
#else
	return ftello(file);
#endif
}

void Fseek64(std::FILE *file, std::int64_t offset, int origin)
{
#if defined(_WIN32)
	_fseeki64(file, offset, origin);
#else
	fseeko(file, offset, origin);
#endif
}

} // namespace

ZipArchive::ZipArchive(std::filesystem::path zipPath) : zipPath_(std::move(zipPath)) {}

ZipArchive::~ZipArchive()
{
	if (opened_ && !closed_) {
		mz_zip_writer_end(&archive_);
		if (file_)
			std::fclose(file_);
	}
}

bool ZipArchive::Open(std::string &errorMessage)
{
	std::memset(&archive_, 0, sizeof(archive_));

	file_ = OpenNative(zipPath_, L"wb", "wb");
	if (!file_) {
		errorMessage = "failed to create archive file: " + zipPath_.string();
		return false;
	}

	if (!mz_zip_writer_init_cfile(&archive_, file_, 0)) {
		errorMessage = LastMinizError(archive_);
		std::fclose(file_);
		file_ = nullptr;
		return false;
	}

	opened_ = true;
	return true;
}

bool ZipArchive::AddFile(const std::filesystem::path &absoluteSourcePath, const std::filesystem::path &archiveRelativePath,
			  std::string &errorMessage)
{
	std::FILE *source = OpenNative(absoluteSourcePath, L"rb", "rb");
	if (!source) {
		errorMessage = "failed to open source file: " + absoluteSourcePath.string();
		return false;
	}

	Fseek64(source, 0, SEEK_END);
	const std::int64_t size = Ftell64(source);
	Fseek64(source, 0, SEEK_SET);

	if (size < 0) {
		std::fclose(source);
		errorMessage = "failed to determine file size: " + absoluteSourcePath.string();
		return false;
	}

	const std::string archiveName = archiveRelativePath.generic_u8string();
	const mz_bool ok = mz_zip_writer_add_cfile(&archive_, archiveName.c_str(), source, static_cast<mz_uint64>(size),
						    nullptr, nullptr, 0, MZ_DEFAULT_LEVEL, nullptr, 0, nullptr, 0);
	std::fclose(source);

	if (!ok) {
		errorMessage = LastMinizError(archive_);
		return false;
	}
	return true;
}

bool ZipArchive::AddData(const std::string &data, const std::filesystem::path &archiveRelativePath, std::string &errorMessage)
{
	const std::string archiveName = archiveRelativePath.generic_u8string();
	if (!mz_zip_writer_add_mem(&archive_, archiveName.c_str(), data.data(), data.size(), MZ_DEFAULT_LEVEL)) {
		errorMessage = LastMinizError(archive_);
		return false;
	}
	return true;
}

ZipReader::ZipReader(std::filesystem::path zipPath) : zipPath_(std::move(zipPath)) {}

ZipReader::~ZipReader()
{
	Close();
}

bool ZipReader::Open(std::string &errorMessage)
{
	std::memset(&archive_, 0, sizeof(archive_));

	file_ = OpenNative(zipPath_, L"rb", "rb");
	if (!file_) {
		errorMessage = "failed to open archive file: " + zipPath_.string();
		return false;
	}

	Fseek64(file_, 0, SEEK_END);
	const std::int64_t size = Ftell64(file_);
	Fseek64(file_, 0, SEEK_SET);

	if (size < 0) {
		errorMessage = "failed to determine archive size: " + zipPath_.string();
		std::fclose(file_);
		file_ = nullptr;
		return false;
	}

	if (!mz_zip_reader_init_cfile(&archive_, file_, static_cast<mz_uint64>(size), 0)) {
		errorMessage = LastMinizError(archive_);
		std::fclose(file_);
		file_ = nullptr;
		return false;
	}

	opened_ = true;
	return true;
}

std::vector<ZipReader::Entry> ZipReader::ListEntries() const
{
	std::vector<Entry> entries;
	if (!opened_)
		return entries;

	const mz_uint count = mz_zip_reader_get_num_files(&archive_);
	for (mz_uint i = 0; i < count; ++i) {
		if (mz_zip_reader_is_file_a_directory(&archive_, i))
			continue;

		mz_zip_archive_file_stat stat{};
		if (!mz_zip_reader_file_stat(&archive_, i, &stat))
			continue;

		// stat.m_filename is UTF-8 (that's how AddFile/AddData store it) --
		// u8path() decodes it into a proper native path (e.g. UTF-16 on
		// Windows) instead of going through the narrow "native" encoding,
		// which would mangle non-ASCII names there.
		entries.push_back({std::filesystem::u8path(stat.m_filename), static_cast<std::uintmax_t>(stat.m_uncomp_size)});
	}
	return entries;
}

bool ZipReader::ReadEntryToString(const std::string &archiveRelativePath, std::string &outData,
				   std::string &errorMessage) const
{
	if (!opened_) {
		errorMessage = "archive is not open";
		return false;
	}

	size_t size = 0;
	void *data = mz_zip_reader_extract_file_to_heap(&archive_, archiveRelativePath.c_str(), &size, 0);
	if (!data) {
		errorMessage = LastMinizError(archive_);
		return false;
	}

	outData.assign(static_cast<const char *>(data), size);
	mz_free(data);
	return true;
}

bool ZipReader::ExtractEntryToFile(const std::string &archiveRelativePath, const std::filesystem::path &destinationPath,
				    std::string &errorMessage) const
{
	if (!opened_) {
		errorMessage = "archive is not open";
		return false;
	}

	std::error_code ec;
	std::filesystem::create_directories(destinationPath.parent_path(), ec);

	std::FILE *destination = OpenNative(destinationPath, L"wb", "wb");
	if (!destination) {
		errorMessage = "failed to open destination file for writing: " + destinationPath.string();
		return false;
	}

	const mz_bool ok = mz_zip_reader_extract_file_to_cfile(&archive_, archiveRelativePath.c_str(), destination, 0);
	std::fclose(destination);

	if (!ok) {
		errorMessage = LastMinizError(archive_);
		return false;
	}
	return true;
}

void ZipReader::Close()
{
	if (closed_)
		return;

	if (opened_)
		mz_zip_reader_end(&archive_);

	if (file_) {
		std::fclose(file_);
		file_ = nullptr;
	}

	closed_ = true;
	opened_ = false;
}

bool ZipArchive::Close(std::string &errorMessage)
{
	if (closed_)
		return true;

	bool ok = true;
	if (opened_ && !mz_zip_writer_finalize_archive(&archive_)) {
		errorMessage = LastMinizError(archive_);
		ok = false;
	}

	if (opened_)
		mz_zip_writer_end(&archive_);

	if (file_) {
		std::fclose(file_);
		file_ = nullptr;
	}

	closed_ = true;
	opened_ = false;
	return ok;
}

} // namespace obs_backuper
