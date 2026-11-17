# Third-party notices

The bootstrap script restores the following pinned dependencies into ignored
`third_party/` directories. It verifies each archive by SHA-256 before extraction.
These dependencies are not committed to this repository.

| Dependency | Version | Use | License | Source |
|---|---:|---|---|---|
| EnTT | 3.15.0 | ECS sparse-set types required by the vendored mod layer | MIT | `https://github.com/skypjack/entt` |
| fmt | 11.2.0 | Formatting headers required by the vendored preloader logger | MIT with optional exception | `https://github.com/fmtlib/fmt` |
| Zig | 0.15.1 | Reproducible Linux x86-64 C++23 compiler/libc++ toolchain for host tests | MIT | `https://github.com/ziglang/zig` (PyPI wheel) |
| CMake | 3.31.6 | Optional host/Android build configuration tool | BSD 3-Clause plus Apache 2.0 notices for bundled components | `https://github.com/Kitware/CMake` (PyPI wheel) |

The extracted archives include their own license files, for example
`third_party/entt/LICENSE`, `third_party/fmt/LICENSE`, and
`third_party/toolchain/ziglang/ziglang/LICENSE`. Those files are authoritative
for each dependency.

The `reference/` tree is not a restored dependency; it is user-supplied source
with no license file. See `docs/LEGAL_SCOPE.md`.
