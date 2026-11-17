# Continuing this work

This document exists so the work can be resumed from a fresh checkout with no prior
context. It states what is done, what is blocked, and what to do next.

## State (last updated 2026-11-17)

| Area | State |
|---|---|
| Bootstrap, toolchain, deps | working, pinned, SHA-256 verified, offline/scratch modes |
| Headless engine + CLI (`baritone-sim`) | working, measured, 125 harness checks |
| Regression suite | `./tests/run.sh` → `TESTS PASSED`, 6 harnesses + 61 Python tests |
| `tools/scenario-lint` | working, 18 tests, published as `scenario-lint-v1.0.0` |
| Android mod | **not device-verified**; no NDK, no device, no break-hook address |
| Docs | complete for everything above; limitations stated per feature |

Verify all of that in about a minute:

```bash
./tests/run.sh                       # expects TESTS PASSED
./scripts/build.sh && ./build/bin/baritone-sim run examples/*.json --batch-out /tmp/r
./build/bin/baritone-sim version
```

## Known blockers, with what each needs

1. **Break hook address.** `GameMode::_sendTryDestroyBlock(BlockPos const&, unsigned
   char)` is decoded but needs an address or byte signature from a device run
   (`docs/features/ZAPH_QUEUE.md`). Without it the bot walks to a block and stops.
2. **Device verification.** Nothing in `reference/` has been executed in Minecraft.
   No claim in this repository says otherwise; keep it that way.
3. **`reference/` license.** No license was supplied with the vendored tree, so its
   object code is not distributed. `baritone-sim` and the Android package stay local.
4. **Release asset upload.** `uploads.github.com` is unreachable from the sandbox used
   here, so `scenario-lint-v1.0.0` is published without its archive. From any machine
   with normal network access:
   ```bash
   ./tools/scenario-lint/build-release.sh
   gh release upload scenario-lint-v1.0.0 dist/scenario-lint.tar.gz
   ```
5. **Android build.** `reference/build.sh` refuses to run without `ANDROID_NDK_HOME`;
   an NDK and a device are required before any Android claim can be made.

## Suggested next steps, in order of value

1. **Attach the release asset** (one command, above) — cheapest real improvement.
2. **More scenario coverage rather than more features.** The engine is a test double
   for `MineProcess`/`TunnelProcess`; every new harness case that would have caught a
   device bug is worth more than a new flag. Directions, dimensions, and tool tiers are
   covered; unvisited blocks, water/lava, and container drops are not.
3. **`--expect` in `tests/run.sh`.** The suite runs examples by hand; using
   `--batch-out` plus `--expect` there would fail CI the moment an example changes
   behaviour. Low risk, real value.
4. **Storm/thunder, nether roof, and portal dimensions** are schema-valid (0/1/2) but
   nothing distinguishes their world height or ore distribution beyond what a scenario
   declares. Do not add dimension-specific world generation without a test that shows
   the difference.
5. **Do not** optimize the pathfinder. `docs/PERFORMANCE.md` records why: the cost is
   dominated by process startup and A* on a small world, with no profile pointing at a
   hot spot.

## House rules this work follows

- Every feature or fix lands with a regression test, and the suite counts in
  `README.md`/`docs/TESTING.md` are updated in the same commit.
- Measurements are recorded with the label they were taken under; old rows are not
  rewritten to match today's numbers.
- Claims that cannot be tested here (device behaviour, Android linking) are written as
  blockers, not as features.
- Commit after each stable phase and push to the working branch.
