# Streamer (open.mp component)

A native [open.mp](https://open.mp) component that streams objects, pickups, checkpoints, race checkpoints, map icons, 3D text labels, actors, and areas. Based on the original [SA-MP Streamer Plugin](https://github.com/samp-incognito/samp-streamer-plugin) by Incognito, fully ported to the open.mp component model with SA-MP plugin compatibility removed.

## Status

- Entry point: `ComponentEntryPoint` returning a real `IComponent`. No SA-MP `Load`/`Unload`/`AmxLoad`/`ProcessTick` exports.
- All hot paths route through open.mp components: `IPlayer`, `IObjectsComponent` / `IPlayerObjectData`, `IPickupsComponent`, `IActorsComponent`, `ICheckpointsComponent` (+ `IPlayerCheckpointData`), `IPlayerTextLabelData`, `IVehiclesComponent`, `IClassesComponent`.
- No `sampgdk` amalgamation; no `samp-plugin-sdk`. Only Pawn VM headers live under `lib/pawn/`.
- Place the built `streamer.dll`/`streamer.so` in the server's `components/` directory. Do **not** use the legacy `plugins/` folder for this build.

## Build

Prerequisites: CMake ≥ 3.19, a 32-bit C++17 toolchain (MSVC Win32 or GCC `-m32`).

```
git submodule update --init --recursive
cmake -B build -A Win32 -DCMAKE_POLICY_VERSION_MINIMUM=3.5   # Windows
cmake --build build --config Release
```

On Linux/GCC drop `-A Win32` and set `-DCMAKE_CXX_FLAGS=-m32 -DCMAKE_C_FLAGS=-m32` via your toolchain file. The build output ends up at `build/bin/Release/streamer.dll` or the equivalent `streamer.so`.

`-DCMAKE_POLICY_VERSION_MINIMUM=3.5` is needed because one of open.mp SDK's vendored submodules (glm) uses an old `cmake_minimum_required`.

## Pawn API

The include file [`streamer.inc`](streamer.inc) and natives are unchanged — existing gamemodes written against Incognito's streamer keep working. Anything you relied on that ultimately called into SA-MP still works because the native invocation goes through open.mp's Pawn component.

## Documentation

Pawn-level documentation lives on the original [wiki](https://github.com/samp-incognito/samp-streamer-plugin/wiki); behaviour and defaults match, with two exceptions worth noting:

- Default `Streamer_GetVisibleItems(STREAMER_TYPE_OBJECT)` is now 1000 (was 500), matching the 0.3.7 per-player object pool. Call `Streamer_SetVisibleItems` if you want something different.
- Actors receive a periodic position re-sync (every ~3s) to work around ped-falls-through-floor when the client's map collision isn't ready yet.

## License

Apache 2.0 — see [LICENSE.md](LICENSE.md). Retains the original Incognito copyright on streamer logic; additional porting work is under the same license.
