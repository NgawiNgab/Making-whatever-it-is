# Reference: baritone-android

Source: unknown-provided archive (`source_code.zip`), received 2026-11-15.
Vendored copies of its mod layer are in `reference/baritone-android/` so the
specifics survive independent of the archive.

**This document is analysis only. No code has been written from it yet.**

Target recorded in its own files: **Minecraft Bedrock 1.26.62.1**, arm64-v8a —
the same build this project targets. That makes it far stronger evidence than
BedrockTools, which targets a version family rather than this exact build.

## What it is

A native LeviLauncher mod that reimplements Baritone (a Java pathfinder) in C++.
It hooks the game, reads pathfinding state out of the entity system, and writes
player rotation/input to steer the bot.

Two build paths are present, and both matter:

| Path | File | Toolchain |
|---|---|---|
| CMake | `CMakeLists.txt`, `src/android/CMakeLists.txt` | CMake + NDK, `baritone_package` target zips the levipack |
| script | `build.sh` | plain `aarch64-linux-android-clang++` loops over sources |

`build.sh` is notable: it needs no build system at all. It compiles every `.cpp`
with explicit `-I` paths and links with
`-Wl,--unresolved-symbols=ignore-all -lc++_shared -ldl -lm`. That flag is why
`pl::*` symbols resolve at runtime against the already-loaded preloader.

## The finding that matters most

**It does not chase offsets to find the player. It reads the EnTT registry.**

`src/android/src/sdk/Game.{h,cpp}` wraps `entt::basic_registry` and exposes:

```
GameRegistry
  storage(uint32_t typeHash) -> GameSparseSet*
  tryGet<T>(EntityId)        -> T*
  localPlayerEntity()        -> optional<EntityId>   // first entity with LocalPlayerComponent
  actorFor(EntityId)         -> void*                // via ActorOwnerComponent
```

Rotation comes from a component, not a pointer chain:

```cpp
struct ActorRotationComponent : GameComponent {
    static constexpr uint32_t kTypeHash = 0x75DF36B7;
    Vec2 rotation;     // x = pitch, y = yaw
    Vec2 rotationOld;
};
static_assert(sizeof(ActorRotationComponent) == 0x10);
```

Also present: `ActorHeadRotationComponent` (`0xBABE7211`), `MobBodyRotationComponent`
(`0xD7F64BBA`).

### Why this invalidates what I built

BedrockTools gets the player by calling a `getLocalPlayer` signature on a captured
`ClientInstance`. That is one valid route, and I copied it. This project takes a
different route — `LocalPlayerComponent` + `ActorOwnerComponent` in the registry —
and it is the one that matches how this game version actually stores the data
(the client is EnTT/ECS driven, which this project's own earlier analysis,
F-CAM-10, already recorded).

The registry is captured from a hook on
`ClientInputUpdateSystemInternal` (`hooks/GameHooks.cpp`), not from
`ClientInstanceUpdate`.

## Signatures recovered from 1.26.62.1

From `src/android/src/memory/Signatures.h`, whose header states every pattern was
"checked offline against the .text segment and matches exactly one function":

| Name | Relevance to the camera feature |
|---|---|
| `ActorSetRotation` | **This is the write path.** Sets actor rotation the same way `/tp ... facing` does. |
| `InputHandler_tick` | Input hook; carries `InputHandler*` and `IClientInstance*`. |
| `InputEventQueue_enqueueDirection` | Synthetic movement/turn input. |
| `ClientInputUpdateSystemInternal_tick` | Where the registry pointer comes from. |
| `BlockSourceGetBlock`, `IsSolidBlockingBlock`, `GetBiome` | Not needed for camera. |

`ActorSetRotation` is the single most useful thing in this archive: the project
has been trying to derive a yaw write path from disassembly, and here is a
verified one for the exact build.

## Mod structure (what a working mod looks like)

```
src/android/
  CMakeLists.txt, manifest.json.in     package + manifest generation
  src/main.cpp                          PL_REGISTER_MOD(...) only
  src/BaritoneMod.{h,cpp}               lifecycle: load/enable/disable/unload
  src/hooks/GameHooks.{h,cpp}           all hooking, all signature resolution
  src/memory/Signatures.h               every byte pattern, one place
  src/sdk/                              offsets + EnTT registry + component structs
  src/platform/                         runtime state, logging, input, rendering
  src/compat/LibCxxCompat.cpp           libc++ shims for the NDK target
```

Three conventions worth copying directly:

1. **`PL_REGISTER_MOD` lives in `main.cpp` and nothing else lives there.** My
   version had a `Runtime` singleton that did everything; theirs separates the
   registration from the lifecycle from the hooks from the SDK.
2. **Every game constant is in one header** (`sdk/Offsets.h`, with a comment per
   offset saying what it is), and every signature in another
   (`memory/Signatures.h`). Not per-version data files — one header, version-named
   comments.
3. **`MOD_*` CMake cache variables** feed `manifest.json.in` via `configure_file`,
   so name/version/entry/icon are build parameters, and the `baritone_package`
   target produces a correctly-laid-out levipack. No Python packager.

## Known unknowns in this archive

- No `preloader-android` copy beyond headers — `preloader-android/` exists in the
  archive but only carries `include/pl` (verify when building).
- `LibCxxCompat.cpp` exists because of a specific libc++ mismatch; its contents
  need reading before assuming it is needed here.
- The archive contains no license file. Because of that, only the mod layer has
  been vendored into `reference/`, and the vendored copies should be treated as
  reading material, not as code to ship. Decide this before reusing any file
  verbatim.

## Open questions before writing code

1. Camera feature: read rotation via the registry (`ActorRotationComponent`) or via
   the pointer chain I had? Registry is better evidenced for 1.26.62.1.
2. Write rotation via `ActorSetRotation`, or via `InputEventQueue_enqueueDirection`
   like their input override? Writing the component directly is what their turn
   path avoids.
3. Do we adopt their CMake + `build.sh` structure outright, rather than
   reconstructing an xmake build?
