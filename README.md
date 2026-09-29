# 🥐 OBS Bakeup

A plugin for [OBS Studio](https://obsproject.com/) (Windows and macOS) that lets you **back up and restore your entire OBS setup** with two buttons — profiles, scenes, hotkeys, plugin settings and more.

Open it from **Tools → OBS Bakeup**.

## ✨ Features

- 💾 **Create Backup** — pick a folder, and the plugin archives your whole OBS setup — settings **and installed plugins** — into a single file, with a progress bar and remaining-time estimate. The OBS version is part of the file name, e.g. `obs-backup_2026-09-29_1407_OBS-32.2.2.zip`.
- 📂 **Restore from Backup** — pick a backup file, review what it contains (creation date, OBS version, source OS) and restore your full configuration from it.
- 🔒 **Password protection** — optionally encrypt a backup (`.obsbak`, Argon2id + XChaCha20-Poly1305). Stream keys, tokens, file names and the manifest are unreadable without the password, and a strength hint helps you pick a good one. Plain `.zip` backups keep working as before.
  > ⚠️ There is no password recovery. If you forget the password, the backup cannot be restored.
- 🛟 **Safe restore** — before anything is touched, your current settings are saved automatically. If something fails half-way, the plugin rolls back on its own, including removing files the restore had already added.
- 🔔 **Update notice** — the dialog tells you when a newer release is available and links to the download page.
- 🌍 **Localized** — English and Russian, with friendly error messages and an optional "Show details" for technical information you can share when asking for help.

## 🧩 What goes into a backup

| Included ✅ | Not included ❌ |
| --- | --- |
| Global and user settings (`global.ini`, `user.ini`) | Logs |
| Profiles and scene collections (`basic/`) | Crash reports |
| Installed plugins (the `plugins/` folder) | Anything else OBS keeps internally (caches, temp files) |
| Plugin settings (`plugin_config/`, including e.g. `obs-browser` logins) | Plugins installed system-wide, outside your OBS user folder |
| Themes | Your own files outside the OBS folder: images, videos, audio, fonts, recordings and other media that scenes use |

> 🖼️ **Media files are not part of a backup.** Scenes refer to images, videos and other files by their path on disk, so after restoring on a new computer copy those files back to the same locations, or re-link them in the affected sources.

Every archive carries a `manifest.json` (plugin, OBS and OS versions, creation time), which is used to validate a backup and to warn you, for example, when it was made on a different operating system.

> 🎯 **Restoring is guaranteed only onto the same OBS version** the backup was made with — that is why the version is in the file name. Plugins are program code built for a specific OBS version and operating system, so restoring onto a different version or OS may leave some of them not working. It is your call which version to restore onto.

> 🔐 **Stream keys and account tokens are part of a backup, and so is executable plugin code.** Keep backup files somewhere safe, protect them with a password, and only restore backups you trust.

## 🔄 How restoring works

OBS writes its own in-memory copy of your scene collections back to disk when it exits, which would silently undo a restore made while OBS is running. To avoid that, the plugin:

1. validates the backup (and asks for the password if it is encrypted);
2. saves your current settings as a safety backup (the 5 most recent are kept in an `obs-backuper-safety` folder next to your OBS configuration);
3. extracts the backup to a staging folder;
4. offers to restart OBS, and applies the restore on the next launch, **before** OBS loads any scene collection.

Plugins get extra care because OBS may already be running their code: a plugin file is never overwritten in place (it is left alone when identical, otherwise replaced by writing a new file and swapping it in), and this plugin's own files are never touched by a restore. Restored plugins start working after OBS restarts.

You are told how the restore turned out once OBS is back up.

## 📦 Installation

Download the package for your operating system from the [Releases](https://github.com/yacasper/obs-bakeup/releases) page and install it the way you would any OBS plugin, then restart OBS. Requires **OBS Studio 31 or newer**: the plugin is built against OBS Studio 31 and tested with 32.2. Older versions are not supported.

## 🛠️ Building from source

The project is based on the [OBS plugin template](https://github.com/obsproject/obs-plugintemplate) and builds with CMake presets:

```sh
# macOS (universal)
cmake --preset macos
cmake --build --preset macos

# Windows (x64)
cmake --preset windows-x64
cmake --build --preset windows-x64
```

GitHub Actions builds both platforms on every push to `main`.

### 🧪 Tests

The core logic (archiving, restore, encryption, settings, update check, locales) has no OBS or Qt dependency and is covered by unit tests ([Catch2](https://github.com/catchorg/Catch2)). They are built together with the plugin (`ENABLE_BACKUPER_TESTS` is on by default) as the `obs-backuper-core-tests` target and run with `ctest` from the build directory.

## 🌐 Privacy and update check

When you first open the plugin's dialog in an OBS session, it makes **one** anonymous request to the GitHub Releases API (`api.github.com`) to find the latest release. Nothing about you or your setup is sent, and if the request fails (offline, rate limit) the plugin simply carries on without a notice. The plugin never downloads or installs anything by itself — the notice only links to the release page.

## 🚧 Status

Version 0.1.0 — all planned features are implemented (backup, restore, password protection, localization, update notice), but the plugin is still pre-release and has not been published yet.

## 📜 License

GPL-2.0-or-later — see [LICENSE](LICENSE). Required because the plugin links against `libobs`, which is GPL-2.0-or-later. Third-party components are listed in [NOTICE](NOTICE):

- [miniz](https://github.com/richgel999/miniz) (MIT) — ZIP archives
- [Monocypher](https://github.com/LoupVaillant/Monocypher) (BSD-2-Clause / CC0) — Argon2id and XChaCha20-Poly1305

## ©️ Name and attribution

The source code is free software: you may fork, modify and redistribute it under the terms of the GPL (see [NOTICE](NOTICE)). The GPL requires you to keep the copyright and license notices, mark your changes, and share the source of your version under the same license.

Independently of the code license, the names **"OBS Bakeup"** and **"Chill Pixel Bakery"** and the "Made by Chill Pixel Bakery" credit identify the original project and its author. Please do not use these names for a modified or forked version in a way that suggests it is the original or endorsed by the author, and when you publish a fork, link back to the original project: <https://github.com/yacasper/obs-bakeup>.
