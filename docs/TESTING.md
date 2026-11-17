# Testing

Run everything after bootstrap:

```bash
./tests/run.sh
./tests/run.sh --clean           # discard the object cache
INCREMENTAL=0 ./tests/run.sh     # force a full core rebuild
python3 scripts/measure.py --label suite --output reports/metrics.json \
    --directory build --database build -- ./tests/run.sh
```

`tests/run.sh` builds the C++23 core and the host-testable mod layer, links each
harness separately, then runs the Python `unittest` suites. It exits non-zero if
any compilation, harness, or Python test fails.

## Inventory

| Test | Proves | Does not prove |
|---|---|---|
| `tests/mine_harness.cpp` | a sugarcane target is scanned, a path is produced, and `Input::ClickLeft` is emitted; `PlayerContext::startOrContinueBreaking` is not used | that a real game breaks a block |
| `tests/mine_cases_harness.cpp` | a buried iron-ore target causes movement and break requests; quantity-driven mining starts | exact vanilla drop/tool rules |
| `tests/tunnel_harness.cpp` | aim math, closed-loop convergence, raycast face/distance/occlusion, 1x2 tunnel emits clicks and does not walk through an unbroken wall | that a real game clears the wall |
| `tests/breaker_harness.cpp` | `BlockBreaker` stays unresolved by default, rejects empty/null/out-of-range addresses, accepts a plausible in-range address and flags it as hand-measured | that the accepted address is the real game function |
| `tests/mine_command_harness.cpp` | `#mine` text parsing accepts plain/namespaced/comma-separated names and rejects blank/quantity-only input without crashing | the parser's private queue contents; validity of unknown block ids |
| `tests/test_bootstrap.py` | pinned/allowlisted sources, checksum literals, prepared bootstrap check, C++23 compiler probe, incremental-cache guard coverage, measurement exit-code propagation | a fresh download on a machine with no network |
| `MeasureTests` in `tests/test_bootstrap.py` | CPU/wall/RSS reporting and failure propagation | performance of the game runtime |

The mod-layer tests link `tests/stubs/preloader_lite.cpp`, whose preloader hooks
are deliberate no-ops. They type-check the mod layer; they do not hook a live
process. `tests/stubs/android/log.h` and `tests/stubs/jni.h` are ABI-inadequate
host stubs and must never be shipped in an Android build.

## Known false positives and false negatives

- **False positive risk:** `ClickLeft` means the core requested a break, not that
  the game accepted one. `breaker_harness` exists specifically to stop an
  unresolved address from being treated as a working bridge.
- **False positive risk:** a test that only checks movement can pass while the
  bot walks into a wall. `tunnel_harness` asserts the player does not pass an
  unbroken wall, and `TestPlayer` refuses steps into solids.
- **False negative risk:** background path calculation may not finish in a
  zero-delay tick loop. Harnesses insert a few milliseconds per tick; the CLI
  uses a bounded tick loop and a configurable delay.
- **False negative risk:** stale objects can link with an old class layout and
  crash. The test runner fingerprints all source/header inputs, external
  headers, compiler version, flags, and `tests/run.sh` before reusing objects.
- **Not covered:** real Minecraft Bedrock 1.26.62.1, ARM64 hooking, EnTT
  registry layout at runtime, anti-cheat, multiplayer, inventory/container
  semantics, or the unresolved `GameMode::_sendTryDestroyBlock` address.

## Records

Raw measured test logs live in ignored `reports/*.log`; numeric summaries live in
ignored `reports/metrics.json`. Curated results and methodology are committed in
`docs/PERFORMANCE.md`. Do not edit a harness to make a failing check pass: fix
the implementation or record the limitation explicitly.
