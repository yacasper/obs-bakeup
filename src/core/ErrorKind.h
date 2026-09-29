// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#pragma once

namespace obs_backuper {

// Machine-readable category of a failure, set next to the (English, technical)
// errorMessage in every outcome struct. The UI never shows errorMessage as the
// main text -- it maps the kind to a localized, non-technical sentence and
// offers the raw errorMessage only as expandable "details".
enum class ErrorKind {
	None,
	Unknown,
	DestinationNotWritable, // backup folder missing / not writable
	NotEnoughDiskSpace,
	ArchiveWriteFailed,     // creating / finalizing the backup archive failed
	InvalidArchive,         // not a zip, no or corrupt manifest.json
	UnsupportedVersion,     // backup made by a newer, incompatible plugin format
	SafetyBackupFailed,     // could not snapshot the current config before restoring
	RestoreExtractFailed,   // could not extract the archive (staging / direct)
	RestoreApplyFailed,     // could not copy the staged files onto the OBS config
	WrongPasswordOrCorrupted,     // encrypted backup: AEAD cannot tell a wrong password from damage
	UnsupportedContainerVersion,  // encrypted backup written by a newer plugin (format / KDF unknown here)
	EncryptionFailed,             // could not encrypt/decrypt (KDF out of memory, RNG, output write)
};

// Locale key ("Error.*") of the friendly message for a kind. Never null or
// empty; ErrorKind::None maps to the generic message as well, so a caller
// that forgot to set a kind still shows something sensible.
const char *ErrorKindLocaleKey(ErrorKind kind);

} // namespace obs_backuper
