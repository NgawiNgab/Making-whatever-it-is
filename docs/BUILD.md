# Building

Target: **Minecraft Bedrock 1.26.62**, Android **arm64-v8a**, NDK **r28c**, xmake **v3.1.2**.

## Host (no NDK needed)

This builds and runs the parts that do not touch the game: pattern parser, fact
resolver, gate rule, report writer. It is the verification available on any Linux
box.

```bash
xmake f -p linux -a x86_64 -m release -y
xmake build -y camface_tests
xmake run -y camface_tests
```

Equivalent without xmake:

```bash
./scripts/run_host_tests.sh        # compile + run
./scripts/check_sdk_api.sh         # type-check SDK-facing sources (needs headers)
./scripts/check_no_build_literals.sh
```

`scripts/check_sdk_api.sh` needs the preloader headers. It looks in
`$PRELOADER_INCLUDE`, then `/tmp/preloader/include`, then `.deps/`. If none exist
it **skips rather than fails**, so a fresh clone is not blocked by a missing SDK.

## Android

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r28c
xmake f -p android -a arm64-v8a --ndk="$ANDROID_NDK_HOME" -m release -y
xmake build -y CameraFacing
```

The build produces `build/android/arm64-v8a/release/libCameraFacing.so`, then runs
`scripts/package_levipack.py` in an `after_build` hook to produce
`CameraFacing.levipack`. The packager reads the version from
`include/camface/Version.hpp`, so the `.so`, the manifest and the header cannot
disagree.

## What the Android build needs, and why each piece is there

| Requirement | Why |
|---|---|
| NDK r28c | reference toolchain for the target; no Exynos/16 KB page surprises |
| `package("preloader")` local definition | `preloader` is **not** in xmake-repo; declaring it with plain `add_requires` fails with `package(preloader) not found` |
| `fmt` | `pl/Logger.hpp` formats through `fmt::vformat`; it is an SDK implementation detail |
| `--ndk=` | without it xmake reports `no any matched platform for this package(preloader)`, which points at the package instead of the missing toolchain |

### Why the requires are guarded by `is_plat("android")`

`add_requires` at root scope is resolved for **every** target in the project. Putting
`preloader` (Android-only, `on_install("android", ...)`) or `fmt` (which pulls
cmake+ninja) at root scope makes `xmake build camface_tests` fail on a Linux host
with `unsupported on linux/x86_64` — a target that never touches either package.

`add_requires` also cannot be called *inside* a `target()` block (`requires() cannot
be called in target()`), so the guard has to be at root scope. That is why the file
reads:

```lua
if is_plat("android") then
    add_requires("preloader")
    add_requires("fmt")
end
```

## Status of the Android build

**It has never been built.** The environment that wrote this had no NDK, and the
NDK could not be downloaded. Everything in this file about the Android path is
derived from the reference build plus the documented failures above — not from a
successful run.

First real build is expected to surface, in order:

1. package-resolution errors for `preloader` (its CMake install needs a JDK for JNI
   headers), then
2. link errors if `pl::modmenu` symbols are not exported by the built SDK, then
3. compile errors in code that the host stubs satisfied but the real headers reject.

## xmake itself

xmake v3.1.2. Binary release assets are served from `release-assets.githubusercontent.com`,
so `curl` on the release tarball can fail in restricted networks; building from a
git clone **with submodules** works because tbox/luajit are submodules. A plain
source tarball from codeload does *not* work (`configure` fails: missing
`core/src/tbox/tbox/src/xmake.sh`).
