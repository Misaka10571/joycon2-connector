## Project shape

- Windows-only C++20 app using MSVC, WinRT BLE, D3D11/ImGui, and ViGEmBus to expose Switch 2 controllers as virtual DS4 or Xbox 360 devices.
- Maintained app code is under `joycon2_connector/`. Root `common.lua`, `command_handler.lua`, and `input_handler.lua` are gitignored Wireshark dissectors, not build inputs.
- There are no tests, lint/format targets, or CI; a Release build is the verification step.
- `src/imgui/`, `include/ViGEm/`, and `lib/ViGEmClient.lib` are vendored; avoid editing them.

## Build

Run from the repository root:

```sh
cmake -S joycon2_connector -B joycon2_connector/build_check -G "Visual Studio 17 2022" -A x64
cmake --build joycon2_connector/build_check --config Release
# output: joycon2_connector/build_check/Release/joycon2_connector.exe
```

- Requires VS 2022 with MSVC and a Windows 10/11 SDK. Running the executable requires ViGEmBus to be installed before launch.
- Use the ignored `build_check/` tree in this checkout: `build/CMakeCache.txt` records the old `D:/projects/python/joycon2-pc/...` path. Do not edit cache files to repair a path mismatch.
- Ignore `.vscode/settings.json`; its CMake source directory is the stale `D:/projects/python/joycon2-pc/testapp` path.
- App-owned compiled sources are only `src/App.cpp`, `src/JoyConDecoder.cpp`, and `src/Logger.cpp`. Add any new app `.cpp` to `APP_SOURCES` in `CMakeLists.txt`; other app modules are header-only.

## Architecture

- `src/App.cpp` contains `WinMain` and the D3D11/ImGui UI loop. Preserve startup order: `Logger::Init()`, `std::set_terminate`, then `winrt::init_apartment()` before any WinRT/BLE use.
- Input flows through `DeviceManager` (scan/GATT) -> `PlayerManager` (subscriptions/lifecycle) -> `JoyConDecoder` -> `ViGEmManager`.
- `DeviceManager.h` matches manufacturer ID `1363` plus prefix `01 00 03 7E`; input UUID is `ab7de9be-89fe-49ad-828f-118f09df7fd2`, write UUID is `649d4ac9-8eb7-4e6c-af44-1ea54fe5f005`, and scan timeout is 30 seconds.
- Single and dual players must remain heap allocated in `std::vector<std::unique_ptr<...>>`: GATT callbacks capture raw pointers to those objects. A vector of player objects would invalidate them on reallocation.
- Most decoder report generators require at least `0x3C` bytes; NSO GC requires `0x3E`. Xbox/XUSB output intentionally has no gyro or touchpad. Cross-check README's BLE offset table against `JoyConDecoder.cpp` before changing parsing.
- `ConfigManager.h` uses hand-written JSON. Adding or changing an `AppConfig` field requires updating both `ConfigToJSON` and `JSONToConfig`.

## Generated and runtime data

- `<build-dir>/generated/version.h` and `lang_data.h` are generated. Change the version in `project(... VERSION ...)`; change translations in `languages/*.json`, never generated headers.
- Language JSON must remain a flat object of string values and include `_locale` and `_display_name`. Adding a language file requires rerunning CMake configure because `file(GLOB)` runs at configure time.
- `joycon2_config.json` is relative to the process working directory, not the executable directory. Device settings are keyed by BLE address; `useXboxEmulation` selects DS4/X360 and `useRawVibration` selects raw `0x5N` packets versus `0x0A` firmware samples.
- Keep raw `0x5N` vibration as the default; `0x0A` samples can produce audible beeping on Pro 2 controllers.
- Case-insensitive `--debug` enables append-only, per-line-flushed logging and crash records at `<exe-dir>/joycon2_connector_debug.log`; without it no log file is created.

## Concurrency and lifecycle

- Concurrent work includes the detached BLE scan, GATT `ValueChanged`, dual merge, mouse interpolation, update checking, and ViGEm vibration callbacks. Preserve atomic/shared ownership used by those paths.
- `DeviceManager::StopScan()` deliberately detaches instead of joining an in-flight WinRT scan; do not block the UI thread waiting for it.
- Before erasing a player, stop/join its owned worker threads and unregister its ViGEm notification; only then `RemoveTarget` and destroy the callback's `VibrationContext`.
- Do not block ViGEm vibration or GATT notification callbacks. `SendGenericCommand` waits on WinRT and sleeps 35 ms; callback paths must use the `*Async` wrappers.
- Hardware pairing test sequence matters: power the controller off, start scanning, then hold its pairing button to power it on. Repeated failures trigger a controller-side cooldown.

## Product constraints

- ViGEmBus supports only DS4 and Xbox 360 targets. Gyro/touchpad data is available only in DS4 mode because XInput cannot carry it.
- Release links in `UpdateChecker.h` and `UI_Pages.h` still use `Misaka10571/joycon2-connector`, while this repository's remote is `Misaka10571/joycon2-pc`; reconcile all API and browser URLs together when touching update logic.
