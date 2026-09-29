// SPDX-License-Identifier: GPL-2.0-or-later
// OBS Bakeup -- https://github.com/yacasper/obs-bakeup
// Copyright (C) 2026 Acid Crusher <chillcody9@gmail.com>

#include "ErrorKind.h"

namespace obs_backuper {

const char *ErrorKindLocaleKey(ErrorKind kind)
{
	switch (kind) {
	case ErrorKind::DestinationNotWritable:
		return "Error.DestinationNotWritable";
	case ErrorKind::NotEnoughDiskSpace:
		return "Error.NotEnoughDiskSpace";
	case ErrorKind::ArchiveWriteFailed:
		return "Error.ArchiveWriteFailed";
	case ErrorKind::InvalidArchive:
		return "Error.InvalidArchive";
	case ErrorKind::UnsupportedVersion:
		return "Error.UnsupportedVersion";
	case ErrorKind::SafetyBackupFailed:
		return "Error.SafetyBackupFailed";
	case ErrorKind::RestoreExtractFailed:
		return "Error.RestoreExtractFailed";
	case ErrorKind::RestoreApplyFailed:
		return "Error.RestoreApplyFailed";
	case ErrorKind::WrongPasswordOrCorrupted:
		return "Error.WrongPasswordOrCorrupted";
	case ErrorKind::UnsupportedContainerVersion:
		return "Error.UnsupportedContainerVersion";
	case ErrorKind::EncryptionFailed:
		return "Error.EncryptionFailed";
	case ErrorKind::None:
	case ErrorKind::Unknown:
		break;
	}
	return "Error.Unknown";
}

} // namespace obs_backuper
