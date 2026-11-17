# Making-whatever-it-is

A reproducible workspace for a headless autonomous-mining engine and the
research material for an experimental Minecraft Bedrock 1.26.62.1 / arm64 mod.

> **Ringkasan (ID):** Bootstrap dependensi, toolchain C++23, dan regression
> tests kini dapat direproduksi tanpa sandbox yang persisten. Mod Android masih
> bersifat eksperimen: alamat fungsi penghancur blok dan pengujian di perangkat
> belum tersedia, sehingga klaim "berfungsi di game" tidak dibuat.

## Quick start

```bash
./scripts/bootstrap.sh     # pinned, SHA-256 verified dependencies + C++23 toolchain
./tests/run.sh             # C++ harnesses + Python regression tests
```

`tests/run.sh` calls bootstrap automatically when needed. See
[`docs/BOOTSTRAP.md`](docs/BOOTSTRAP.md) for offline/force modes and
[`docs/TESTING.md`](docs/TESTING.md) for the test inventory.

## What is ready

- Pinned bootstrap: EnTT 3.15.0, fmt 11.2.0, Zig 0.15.1, CMake 3.31.6.
- Host C++23 toolchain with `<format>` and a compiler probe.
- Five existing headless C++ harnesses plus bootstrap/measurement regression
  tests, all passing through `./tests/run.sh`.
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
anything derived from it.

## Documentation map

- `docs/BOOTSTRAP.md` — pinned dependencies and recovery procedures
- `docs/TESTING.md` — test coverage, false positives/negatives, limitations
- `docs/PERFORMANCE.md` — measured CPU/RAM/time/size and methodology
- `docs/REFERENCE_BARITONE.md` — analysis of the supplied archive
- `docs/features/AUTO_MINE.md` — mining feature state and the live-game blocker
- `docs/features/ZAPH_FINDINGS.md`, `ZAPH_QUEUE.md` — device-only signature work
- `THIRD_PARTY_NOTICES.md` — restored dependency licenses
