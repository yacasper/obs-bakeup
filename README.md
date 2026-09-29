# 🥐 OBS Bakeup

A plugin for [OBS Studio](https://obsproject.com/) (Windows and macOS) that lets you **back up and restore your entire OBS setup** with two buttons — profiles, scenes, hotkeys, plugin settings and more.

Open it from **Tools → OBS Bakeup**.

## ✨ Features

- 💾 **Create Backup** — pick a folder, and the plugin archives your OBS configuration into a single file, with a progress bar and remaining-time estimate.
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
| Plugin configs (`plugin_config/`, including e.g. `obs-browser` logins) | Anything else OBS keeps internally (plugins, caches, temp files) |
| Themes | |

Every archive carries a `manifest.json` (plugin, OBS and OS versions, creation time), which is used to validate a backup and to warn you, for example, when it was made on a different operating system.

> 🔐 **Stream keys and account tokens are part of a backup.** Keep backup files somewhere safe, or protect them with a password.

## 🔄 How restoring works

OBS writes its own in-memory copy of your scene collections back to disk when it exits, which would silently undo a restore made while OBS is running. To avoid that, the plugin:

1. validates the backup (and asks for the password if it is encrypted);
2. saves your current settings as a safety backup (the 5 most recent are kept in an `obs-backuper-safety` folder next to your OBS configuration);
3. extracts the backup to a staging folder;
4. offers to restart OBS, and applies the restore on the next launch, **before** OBS loads any scene collection.

You are told how the restore turned out once OBS is back up.

## 📦 Installation

Download the package for your operating system from the [Releases](https://github.com/yacasper/obs-bakeup/releases) page and install it the way you would any OBS plugin, then restart OBS. The plugin is built against OBS Studio 31 and tested with 32.2. It only uses long-standing OBS APIs and Qt 6, so OBS Studio 28 or newer (the first Qt 6 release) should work, but older versions are untested.

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

The core logic (archiving, restore, encryption, settings, update check, locales) has no OBS or Qt dependency and is covered by unit tests ([Catch2](https://github.com/catchorg/Catch2)):

They are built together with the plugin (`ENABLE_BACKUPER_TESTS` is on by default) as the `obs-backuper-core-tests` target and run with `ctest` from the build directory.

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
