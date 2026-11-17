# Testing

Run everything after bootstrap:

```bash
./tests/run.sh
./tests/run.sh --clean           # discard the object cache
INCREMENTAL=0 ./tests/run.sh     # force a full core rebuild
python3 scripts/measure.py --label suite --output reports/metrics.json \
    --directory build --database build -- ./tests/run.sh
```

`tests/run.sh` builds the C++23 core and the host-testable mod layer, builds the
first-party headless engine and CLI (`scripts/build_engine.sh`, no CMake required),
links each harness separately, then runs the Python `unittest` suites. It exits
non-zero if any compilation, harness, or Python test fails.

Current result (2026-11-17): **6 C++ harnesses passed, 24 Python tests passed,
`TESTS PASSED`**.

## Inventory

| Test | Proves | Does not prove |
|---|---|---|
| `tests/mine_harness.cpp` | a sugarcane target is scanned, a path is produced, and `Input::ClickLeft` is emitted; `PlayerContext::startOrContinueBreaking` is not used | that a real game breaks a block |
| `tests/mine_cases_harness.cpp` | a buried iron-ore target causes movement and break requests; quantity-driven mining starts | exact vanilla drop/tool rules |
| `tests/tunnel_harness.cpp` | aim math, closed-loop convergence, raycast face/distance/occlusion, 1x2 tunnel emits clicks and does not walk through an unbroken wall | that a real game clears the wall |
| `tests/breaker_harness.cpp` | `BlockBreaker` stays unresolved by default, rejects empty/null/out-of-range addresses, accepts a plausible in-range address and flags it as hand-measured | that the accepted address is the real game function |
| `tests/mine_command_harness.cpp` | `baritone::platform::parsing` turns `#mine`/`#tunnel` text into exact names, quantities, directions and lengths; separators, casing, negative/absent quantity, and conflicting directions are covered (20 checks) | that the game accepts the resulting block ids |
| `tests/sim_harness.cpp` | the headless engine end to end: strict JSON, scenario schema, yaw->compass direction, mining happy path, harvest levels for every tier, absent vs blocked vs tool_insufficient, quantity/inventory accounting, tunnel carving in four directions, nether/overworld dimension rules, determinism, result schema, invalid input (116 checks) | Minecraft itself, entities, or the game's break hook |
| `tests/test_cli.py` | the real `build/bin/baritone-sim` binary: version/help/exit codes, validate accepts every example and rejects malformed/invalid ones, run writes or prints valid JSON, compact mode is one line, identical runs agree on ticks/breaks/counts, nested `--output` directories are created, `tool_insufficient` is reported instead of faked | distribution/packaging |
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
  uses a bounded tick loop and a configurable delay, and the runner now waits for
  each plan (`determinism.plan_wait_timeouts`) instead of racing it. Without that
  wait, identical runs differed by a tick (`ticks 34` vs `35`), which is exactly
  the kind of flaky result this suite must not produce.
- **False positive risk (fixed):** `mine_command_harness` used to assert
  `check(true, ...)` for every case, so it passed while the parser could queue
  nothing at all. The parsing moved to `platform/CommandText.cpp` and each case now
  asserts exact output.
- **False positive risk (fixed):** the "stops after the requested quantity" case in
  `mine_cases_harness` called `mine(0, ...)` (keep going) and only checked movement.
  The fake player there reports `countItem() == 0`, so the quantity stop is not
  observable; that gap is stated in the harness and covered by
  `tests/sim_harness.cpp`, whose player counts drops.
- **Measurement caveat:** `scripts/build_engine.sh` reuses Zig's compiler cache.
  Deleting only `build/obj` and calling it a "cold" build reports about 1 s instead
  of the real 130 s; a cold measurement must also clear
  `third_party/zig-{global,local}-cache`.
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
