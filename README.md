# Making-whatever-it-is

A reproducible workspace for a headless autonomous-mining engine and the
research material for an experimental Minecraft Bedrock 1.26.62.1 / arm64 mod.

> **Ringkasan (ID):** Bootstrap dependensi, toolchain C++23, regression tests,
> dan **engine headless + CLI `baritone-sim`** kini berfungsi dan terukur tanpa
> perangkat. Skenario tambang/tunnel dijalankan di dunia in-memory dan menghasilkan
> laporan JSON yang dapat diaudit. Mod Android masih eksperimen: alamat fungsi
> penghancur blok dan pengujian di perangkat belum tersedia, sehingga klaim
> "berfungsi di game" tidak dibuat.

## Quick start

```bash
./scripts/bootstrap.sh     # pinned, SHA-256 verified dependencies + C++23 toolchain
./tests/run.sh             # 6 C++ harnesses + 24 Python regression tests
./scripts/build.sh         # CMake Release build -> build/bin/baritone-sim

./build/bin/baritone-sim run examples/mine_iron_ore.json
./build/bin/baritone-sim run examples/tunnel_east.json --compact
```

Result: `TESTS PASSED` (6 C++ harnesses, 24 Python tests) and a JSON report with
`status`, breaks, inventory, quality suspects, determinism and resource metrics.
See [`docs/ENGINE.md`](docs/ENGINE.md) for the scenario schema and report format.

`tests/run.sh` calls bootstrap automatically when needed. See
[`docs/BOOTSTRAP.md`](docs/BOOTSTRAP.md) for offline/force modes and
[`docs/TESTING.md`](docs/TESTING.md) for the test inventory.

## What is ready

- Pinned bootstrap: EnTT 3.15.0, fmt 11.2.0, Zig 0.15.1, CMake 3.31.6.
- Host C++23 toolchain with `<format>` and a compiler probe.
- **Headless engine + CLI** (`src/sim`, `src/cli`): deterministic in-memory
  mining and tunnelling through the vendored core, with a strict scenario schema
  and an auditable JSON result. Benchmarks: about 0.1 s and 13 MiB per run.
- Six C++ harnesses (including `tests/sim_harness.cpp`, 116 checks) plus 24
  Python regression tests (bootstrap, idempotence, offline, measurement, CLI end-to-end), all passing
  through `./tests/run.sh`.
- Core bugs found by those runs and fixed: `nearestHorizontal()` had north/south
  swapped against `Rotation.h`; tunnel worlds had no rock to dig.
- CPU, wall time, peak RSS, and artifact-size measurement via
  `scripts/measure.py`; curated results in
  [`docs/PERFORMANCE.md`](docs/PERFORMANCE.md).
- Explicit license scope for first-party code and the unlicensed `reference/`
  archive in [`docs/LEGAL_SCOPE.md`](docs/LEGAL_SCOPE.md).

## What is not ready

The Android mod is **not** device-verified. In particular:

- `GameMode::_sendTryDestroyBlock(BlockPos const&, unsigned char)` has a known
  mangled name but no verified address or byte signature; earlier proposed byte
  patterns were retracted in `docs/features/ZAPH_FINDINGS.md`.
- `Input::ClickLeft` is not yet a real game-side break in the vendored mod layer.
- Inventory reads, drop pickup, and live EnTT registry layout need a device.

`reference/` is reading material with no supplied license. It is compiled for
local regression testing only; see `docs/LEGAL_SCOPE.md` before distributing
anything derived from it. `baritone-sim` intentionally links that core, so it is a
local tool until the provenance question is settled.

## Documentation map

- `docs/BOOTSTRAP.md` — pinned dependencies and recovery procedures
- `docs/ENGINE.md` — headless engine: scenario schema, statuses, report format
- `docs/TESTING.md` — test coverage, false positives/negatives, limitations
- `docs/PERFORMANCE.md` — measured CPU/RAM/time/size and methodology
- `docs/REFERENCE_BARITONE.md` — analysis of the supplied archive
- `docs/features/AUTO_MINE.md` — mining feature state and the live-game blocker
- `docs/features/ZAPH_FINDINGS.md`, `ZAPH_QUEUE.md` — device-only signature work
- `THIRD_PARTY_NOTICES.md` — restored dependency licenses
