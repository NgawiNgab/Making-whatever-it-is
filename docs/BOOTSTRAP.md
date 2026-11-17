# Bootstrap

The repository deliberately does **not** commit dependency source trees, compiler
wheels, object files, or test binaries. A fresh checkout needs one command:

```bash
./scripts/bootstrap.sh
./tests/run.sh
```

`tests/run.sh` also invokes the bootstrap automatically when a required header
or `third_party/env.sh` is missing. Nothing in `third_party/` is persisted by Git.

## What bootstrap installs

| Component | Pin | Why |
|---|---:|---|
| EnTT | 3.15.0 | `sdk/Entity.h` specializes `entt::component_traits<Type, Entity>`; older 3.13 API only had `component_traits<Type>` and failed to compile |
| fmt | 11.2.0 | Header-only `FMT_STRING` consteval works with the pinned Clang 20 toolchain; 11.0.2/10.2.1 did not |
| Zig | 0.15.1 | Bundles Clang 20 + libc++ with C++23 `<format>`. The sandbox's g++ 12 has no `<format>` |
| CMake | 3.31.6 | Reproducible build configuration tool; the sandbox has no system CMake |

Every download is verified against a 64-character SHA-256 literal in
`scripts/bootstrap.sh` before extraction. Extraction stamps record the hash, so
a second run does not re-extract. The only hosts contacted are
`codeload.github.com` and `files.pythonhosted.org`.

## Commands

```bash
./scripts/bootstrap.sh              # fetch and extract, then run a compiler probe
./scripts/bootstrap.sh --check      # verify an existing installation, no network
./scripts/bootstrap.sh --offline    # use only third_party/cache
./scripts/bootstrap.sh --force      # re-extract even when stamps match
```

The script writes `third_party/env.sh`; source it if invoking a compiler outside
`tests/run.sh`:

```bash
source third_party/env.sh
"$ZIG" c++ --version
"$CMAKE" --version
```

The compiler probe compiles a tiny program using `<format>`, `<fmt/format.h>`,
and `<entt/entity/sparse_set.hpp>`, then executes it. A successful bootstrap ends
with `BOOTSTRAP_OK`.

## Host support

The pinned Zig and CMake wheels are Linux x86-64. On another host the script
fails with an explicit message rather than downloading an incompatible wheel.
A manually installed Clang/libc++ with C++23 `<format>` can be used by setting
`ZIG` to a compatible wrapper, but that path is not part of the verified
bootstrap. `uname -s` and `uname -m` are checked before any download.

## Caches

`ZIG_GLOBAL_CACHE_DIR` and `ZIG_LOCAL_CACHE_DIR` default to ignored directories
under `third_party/`. They can be overridden. A cold cache makes the first full
test compile substantially slower; see `docs/PERFORMANCE.md`. Removing
`third_party/` is safe: re-run `./scripts/bootstrap.sh`.

## Failure modes

- `SHA-256 mismatch`: the cached archive is corrupt or a pin changed; delete that
  one file from `third_party/cache/` and rerun.
- `offline bootstrap cannot fetch`: run without `--offline` once on a networked
  host, or copy `third_party/cache/` from a prepared machine.
- `C++23 toolchain lacks std::format`: an old system compiler was selected;
  rerun bootstrap and source `third_party/env.sh`.
- `pinned toolchain wheels target Linux x86_64`: use a Linux x86-64 host or a
  manual C++23 toolchain when building the Android target in CI.
