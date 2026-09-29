# Development

Notes for people building or changing OBS Bakeup. Users do not need any of this; see the [README](README.md).

## Building from source

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

## Tests

The core logic has no OBS or Qt dependency and is covered by unit tests ([Catch2](https://github.com/catchorg/Catch2)): archiving, restore and rollback, the shutdown-time restore flow and its stored outcome, choosing what to switch to after a restore, scene-file snapshots, file collection (including plugin folders and OBS-shipped plugins), UTF-8 handling of non-ASCII paths, encryption and the crypto primitives, password strength, settings, update check, locales and error reporting. They are built together with the plugin (`ENABLE_BACKUPER_TESTS` is on by default) as the `obs-backuper-core-tests` target and run with `ctest` from the build directory.

The thin layer that calls OBS and Qt (the dialogs, the worker threads, the frontend calls in `plugin-main.cpp`) cannot run without OBS. Everything decidable in it lives in the core and is tested there; the layer itself is compiled on every push by GitHub Actions and checked by hand against a real OBS.
