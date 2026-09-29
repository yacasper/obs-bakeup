# 🥐 OBS Bakeup

A plugin for [OBS Studio](https://obsproject.com/) (Windows and macOS) that lets you **back up and restore your entire OBS setup** with two buttons — profiles, scenes, hotkeys, plugin settings and more.

Open it from **Tools → OBS Bakeup**.

## ✨ Features

- 💾 **Create Backup** — pick a folder, and the plugin archives your whole OBS setup — settings **and installed plugins** — into a single file, with a progress bar and remaining-time estimate. The OBS version is part of the file name, e.g. `obs-backup_2026-09-29_1407_OBS-32.2.2.zip`.
- 📂 **Restore from Backup** — pick a backup file, review what it contains (creation date, OBS version, source OS) and restore your full configuration from it. After one restart OBS comes up on the restored profile and scene collection, with the same canvas size, custom docks and plugins. A backup made with a normal OBS restores into a portable one and the other way round.
- 🔒 **Password protection** — optionally encrypt a backup (`.obsbak`). Stream keys, tokens and file names are unreadable without the password, and a strength hint helps you pick a good one. Plain `.zip` backups work too.
  > ⚠️ There is no password recovery. If you forget the password, the backup cannot be restored.
- 🛟 **Safe restore** — before anything is touched, your current settings are saved automatically. If something fails half-way, the plugin rolls back on its own, including removing files the restore had already added.
- 🔔 **Update notice** — the dialog tells you when a newer release is available and links to the download page.
- 🌍 **Localized** — English and Russian, with friendly error messages and an optional "Show details" you can share when asking for help.

## 🧩 What goes into a backup

| Included ✅ | Not included ❌ |
| --- | --- |
| Global and user settings (`global.ini`, `user.ini`), including custom browser docks and the dock layout | Logs |
| Profiles and scene collections (`basic/`): canvas size, hotkeys, stream keys and service settings, scenes and sources | Crash reports |
| Installed plugins: on macOS the `plugins/` folder of your OBS folder **and** the plugins installed for all users (`/Library/Application Support/obs-studio/plugins`); on Windows `%ProgramData%\obs-studio\plugins` (`<OBS folder>\plugins` in portable mode) **and** third-party plugins installed the classic way into OBS's program folder (`obs-plugins` and `data\obs-plugins`, next to `obs64.exe`) | Anything else OBS keeps internally (caches, temp files) |
| Plugin settings (`plugin_config/`, including e.g. `obs-browser` logins; not the browser's disk caches, which it rebuilds itself) | The plugins and helper files that ship with OBS itself (`obs-ffmpeg`, `win-capture`, `obs-browser` and so on): they belong to one exact OBS version |
| Themes | Your own files outside the OBS folder: images, videos, audio, fonts, recordings and other media that scenes use |

> 🖼️ **Media files are not part of a backup.** Scenes refer to images, videos and other files by their path on disk, so after restoring on a new computer copy those files back to the same locations, or re-link them in the affected sources.

Before restoring, the plugin shows when the backup was made, with which OBS version and on which operating system, and warns you if it came from a different system.

> 🎯 **Restoring is guaranteed only onto the same OBS version** the backup was made with — that is why the version is in the file name. Plugins are program code built for a specific OBS version and operating system, so restoring onto a different version or OS may leave some of them not working. It is your call which version to restore onto.

> 🔐 **Stream keys and account tokens are part of a backup, and so is executable plugin code.** Keep backup files somewhere safe, protect them with a password, and only restore backups you trust.

## 🔄 How restoring works

1. The plugin checks the backup (and asks for the password if it is encrypted).
2. It saves your current settings as a safety backup first. The 5 most recent are kept in an `obs-backuper-safety` folder next to your OBS configuration.
3. It prepares the restore and offers to restart OBS.
4. The restore is applied as OBS closes. When OBS starts again it comes up with the restored profile and scene collection, canvas size, docks and plugins already in place. Once it is back, a message tells you how the restore went.

If something goes wrong half-way, your previous settings are put back automatically.

> ⚠️ **Some plugin folders are system folders** — on Windows `%ProgramData%\obs-studio\plugins` and OBS's program folder (usually under `C:\Program Files`), on macOS `/Library/Application Support/obs-studio/plugins` — and may need administrator rights to write to. If the system refuses some plugin files, the rest of the restore still goes through and you are told how many plugin files could not be installed; run OBS with administrator rights and restore again to install them.

A few things worth knowing:

- A backup made with a normal OBS restores into a portable one, and the other way round.
- The embedded browser's temporary cache is not backed up (OBS rebuilds it), but your browser logins are. If Windows still holds one of the browser's files while OBS closes, that file is left as it was and the rest is restored.
- If OBS is killed instead of being closed normally, the restore is applied on the next start, and one more restart is needed for OBS to pick up the restored profiles.

## 📦 Installation

Download the package for your operating system from the [Releases](https://github.com/yacasper/obs-bakeup/releases) page, then restart OBS. Requires **OBS Studio 31 or newer** (tested with 32.2).

- **macOS** — open the installer package and follow the steps.
- **Windows** — there is no installer. Close OBS, then extract the whole contents of the `.zip` into your OBS folder (usually `C:\Program Files\obs-studio`; for a portable OBS, the folder holding `bin`), merging it with the existing `obs-plugins` and `data` folders. Windows asks for administrator rights to write there. The archive is laid out like the OBS folder itself, so nothing needs to be moved around afterwards.

## 🌐 Privacy and update check

When you first open the plugin's dialog in an OBS session, it makes **one** anonymous request to the GitHub Releases API (`api.github.com`) to find the latest release. Nothing about you or your setup is sent, and if the request fails (offline, rate limit) the plugin simply carries on without a notice. The plugin never downloads or installs anything by itself — the notice only links to the release page.

## 🚧 Status

Version 1.0.0. Backup and restore are checked end to end on **Windows** (normal and portable OBS 32.2, repeated restores included) and on **macOS** (OBS 32.2, password-protected backup restored twice). macOS packages are not signed yet, so macOS may ask you to right-click the installer and choose **Open**.

## 💜 Support the project

OBS Bakeup is free and open source. If it saved you a stream setup and you would like to say thanks, you can [**support the project**](https://destream.net/live/Chillcody).

## 📜 License

GPL-2.0-or-later — see [LICENSE](LICENSE). Required because the plugin links against `libobs`, which is GPL-2.0-or-later. Third-party components are listed in [NOTICE](NOTICE):

- [miniz](https://github.com/richgel999/miniz) (MIT) — ZIP archives
- [Monocypher](https://github.com/LoupVaillant/Monocypher) (BSD-2-Clause / CC0) — Argon2id and XChaCha20-Poly1305

## ©️ Name and attribution

The source code is free software: you may fork, modify and redistribute it under the terms of the GPL (see [NOTICE](NOTICE)). The GPL requires you to keep the copyright and license notices, mark your changes, and share the source of your version under the same license.

OBS Bakeup is made by **Chill Pixel Bakery** — videos about streaming, gadgets and games on [YouTube](https://www.youtube.com/@ChillPixelBakery).

Independently of the code license, the names **"OBS Bakeup"** and **"Chill Pixel Bakery"** and the "Made by Chill Pixel Bakery" credit identify the original project and its author. Please do not use these names for a modified or forked version in a way that suggests it is the original or endorsed by the author, and when you publish a fork, link back to the original project: <https://github.com/yacasper/obs-bakeup>.
