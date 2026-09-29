# OBS Bakeup

A plugin for OBS Studio (Windows first, macOS second) that lets you back up and restore your entire OBS setup with two buttons.

## Features

- **Create Backup** — pick a folder, and the plugin archives all OBS settings: profiles, scene collections, plugin configs, hotkeys, and more, with a progress bar.
- **Password protection** — optionally encrypt a backup (`.obsbak`, Argon2id + XChaCha20-Poly1305): stream keys, tokens, file names and the manifest are unreadable without the password. There is no password recovery — if you forget it, the backup cannot be restored. Plain `.zip` backups keep working as before.
- **Restore Backup** — pick a previously created backup file, and the plugin restores your full OBS configuration from it, with a progress bar.

## Status

In development. Not yet ready for use.

## License

GPL-2.0-or-later — see [LICENSE](LICENSE). Required because the plugin links against `libobs`, which is GPL-2.0-or-later.

## Name and attribution

The source code is free software: you may fork, modify and redistribute it under the terms of the GPL (see [NOTICE](NOTICE)). The GPL requires you to keep the copyright and license notices, mark your changes, and share the source of your version under the same license.

Independently of the code license, the names **"OBS Bakeup"** and **"Chill Pixel Bakery"** and the "Made by Chill Pixel Bakery" credit identify the original project and its author. Please do not use these names for a modified or forked version in a way that suggests it is the original or endorsed by the author, and when you publish a fork, link back to the original project: <https://github.com/yacasper/obs-bakeup>.
