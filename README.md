# 🥐 OBS Bakeup

A plugin for [OBS Studio](https://obsproject.com/) (Windows and macOS) that lets you **back up and restore your entire OBS setup** with two buttons — profiles, scenes, hotkeys, plugin settings and more.

Open it from **Tools → OBS Bakeup**.

## ✨ Features

- 💾 **Create Backup** — pick a folder, and the plugin archives your whole OBS setup — settings **and installed plugins** — into a single file, with a progress bar and remaining-time estimate. The OBS version is part of the file name, e.g. `obs-backup_2026-09-29_1407_OBS-32.2.2.zip`.
- 📂 **Restore from Backup** — pick a backup file, review what it contains (creation date, OBS version, source OS) and restore your full configuration from it. After one restart OBS comes up on the restored profile and scene collection, with the same canvas size, custom docks and plugins. A backup made with a normal OBS restores into a portable one and the other way round.
- 🔒 **Password protection** — optionally encrypt a backup (`.obsbak`, Argon2id + XChaCha20-Poly1305). Stream keys, tokens, file names and the manifest are unreadable without the password, and a strength hint helps you pick a good one. Plain `.zip` backups keep working as before.
  > ⚠️ There is no password recovery. If you forget the password, the backup cannot be restored.
- 🛟 **Safe restore** — before anything is touched, your current settings are saved automatically. If something fails half-way, the plugin rolls back on its own, including removing files the restore had already added.
- 🔔 **Update notice** — the dialog tells you when a newer release is available and links to the download page.
- 🌍 **Localized** — English and Russian, with friendly error messages and an optional "Show details" for technical information you can share when asking for help.

## 🧩 What goes into a backup

| Included ✅ | Not included ❌ |
| --- | --- |
| Global and user settings (`global.ini`, `user.ini`), including custom browser docks and the dock layout | Logs |
| Profiles and scene collections (`basic/`): canvas size, hotkeys, stream keys and service settings, scenes and sources | Crash reports |
| Installed plugins: on macOS the `plugins/` folder of your OBS folder; on Windows `%ProgramData%\obs-studio\plugins` (`<OBS folder>\plugins` in portable mode) **and** third-party plugins installed the classic way into OBS's program folder (`obs-plugins` and `data\obs-plugins`, next to `obs64.exe`) | Anything else OBS keeps internally (caches, temp files) |
| Plugin settings (`plugin_config/`, including e.g. `obs-browser` logins; not the browser's disk caches, which it rebuilds itself) | The plugins and helper files that ship with OBS itself (`obs-ffmpeg`, `win-capture`, `obs-browser` and so on): they belong to one exact OBS version |
| Themes | Your own files outside the OBS folder: images, videos, audio, fonts, recordings and other media that scenes use |

> 🖼️ **Media files are not part of a backup.** Scenes refer to images, videos and other files by their path on disk, so after restoring on a new computer copy those files back to the same locations, or re-link them in the affected sources.

Every archive carries a `manifest.json` (plugin, OBS and OS versions, creation time), which is used to validate a backup and to warn you, for example, when it was made on a different operating system.

> 🎯 **Restoring is guaranteed only onto the same OBS version** the backup was made with — that is why the version is in the file name. Plugins are program code built for a specific OBS version and operating system, so restoring onto a different version or OS may leave some of them not working. It is your call which version to restore onto.

> 🔐 **Stream keys and account tokens are part of a backup, and so is executable plugin code.** Keep backup files somewhere safe, protect them with a password, and only restore backups you trust.

## 🔄 How restoring works

OBS reads its profiles, scene collections and settings into memory when it starts and writes that copy back to disk when it exits. A restore made while OBS is running would be silently undone, and one made a moment after start would leave OBS unaware of the restored profiles and collections. To avoid both, the plugin:

1. validates the backup (and asks for the password if it is encrypted);
2. saves your current settings as a safety backup (the 5 most recent are kept in an `obs-backuper-safety` folder next to your OBS configuration);
3. extracts the backup to a staging folder;
4. offers to restart OBS, and applies the restore **as OBS shuts down**, after OBS has written everything it is going to write. The next start then reads the restored files like any other start, with the right profile, scene collection, canvas size, docks and plugins already in place.

> ⚠️ **On Windows, plugins live in system folders** (`%ProgramData%\obs-studio\plugins` and OBS's program folder, usually under `C:\Program Files`), which may need administrator rights to write to. A backup made with a normal OBS also restores into a portable one and the other way round. If Windows refuses some plugin files, the restore still finishes — settings and every other plugin are restored — and you are told how many plugin files could not be installed; run OBS as administrator and restore again to install them.

Plugins get extra care because OBS may already be running their code. They are put in place right away, while OBS is still open, so a single restart is enough for OBS to find them. A plugin file is never overwritten in place (it is left alone when identical, otherwise replaced by writing a new file and swapping it in), and this plugin's own files are never touched by a restore. Executable files keep their execute permission.

You are told how the restore turned out once OBS is back up: as OBS is shutting down there is no window left to show it in, so the outcome is kept in a small file (`restore-result.ini` in the safety folder) that the next start reads and deletes.

If OBS is killed instead of being closed, the staged restore is still applied on the next start. That start already built its lists of profiles and collections, so the plugin switches to the restored ones where it can, and one more restart makes everything right.

Files the embedded browser (`obs-browser`) keeps open while OBS shuts down cannot be replaced by Windows. Its disk caches are therefore not backed up at all, and a file in its folder that is still locked is left as it was instead of failing the whole restore (the log says so). A locked file anywhere else fails the restore and rolls it back.

## 📦 Installation

Download the package for your operating system from the [Releases](https://github.com/yacasper/obs-bakeup/releases) page, then restart OBS. Requires **OBS Studio 31 or newer**: the plugin is built against OBS Studio 31 and tested with 32.2. Older versions are not supported.

- **macOS** — open the installer package and follow the steps.
- **Windows** — there is no installer. Close OBS, then extract the whole contents of the `.zip` into your OBS folder (usually `C:\Program Files\obs-studio`; for a portable OBS, the folder holding `bin`), merging it with the existing `obs-plugins` and `data` folders. Windows asks for administrator rights to write there. The archive is laid out like the OBS folder itself (`obs-plugins/64bit/obs-bakeup.dll`, `data/obs-plugins/obs-bakeup/`), so nothing needs to be moved around afterwards.

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

## 🧪 Tests

The core logic has no OBS or Qt dependency and is covered by unit tests ([Catch2](https://github.com/catchorg/Catch2)): archiving, restore and rollback, the shutdown-time restore flow and its stored outcome, choosing what to switch to after a restore, scene-file snapshots, file collection (including plugin folders and OBS-shipped plugins), UTF-8 handling of non-ASCII paths, encryption and the crypto primitives, password strength, settings, update check, locales and error reporting. They are built together with the plugin (`ENABLE_BACKUPER_TESTS` is on by default) as the `obs-backuper-core-tests` target and run with `ctest` from the build directory.

The thin layer that calls OBS and Qt (the dialogs, the worker threads, the frontend calls in `plugin-main.cpp`) cannot run without OBS. Everything decidable in it lives in the core and is tested there; the layer itself is compiled on every push by GitHub Actions and checked by hand against a real OBS.

## 🌐 Privacy and update check

When you first open the plugin's dialog in an OBS session, it makes **one** anonymous request to the GitHub Releases API (`api.github.com`) to find the latest release. Nothing about you or your setup is sent, and if the request fails (offline, rate limit) the plugin simply carries on without a notice. The plugin never downloads or installs anything by itself — the notice only links to the release page.

## 🚧 Status

Version 0.1.0 — all planned features are implemented (backup, restore, password protection, localization, update notice). Backup and restore have been checked end to end on **Windows** (normal and portable OBS 32.2, repeated restores included). **macOS** builds and passes the same unit tests, but a full restore has not been checked there by hand yet. The plugin is still pre-release and has not been published.

## 📜 License

GPL-2.0-or-later — see [LICENSE](LICENSE). Required because the plugin links against `libobs`, which is GPL-2.0-or-later. Third-party components are listed in [NOTICE](NOTICE):

- [miniz](https://github.com/richgel999/miniz) (MIT) — ZIP archives
- [Monocypher](https://github.com/LoupVaillant/Monocypher) (BSD-2-Clause / CC0) — Argon2id and XChaCha20-Poly1305

## ©️ Name and attribution

The source code is free software: you may fork, modify and redistribute it under the terms of the GPL (see [NOTICE](NOTICE)). The GPL requires you to keep the copyright and license notices, mark your changes, and share the source of your version under the same license.

Independently of the code license, the names **"OBS Bakeup"** and **"Chill Pixel Bakery"** and the "Made by Chill Pixel Bakery" credit identify the original project and its author. Please do not use these names for a modified or forked version in a way that suggests it is the original or endorsed by the author, and when you publish a fork, link back to the original project: <https://github.com/yacasper/obs-bakeup>.
