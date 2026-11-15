# T-CAM2 Phase A — readout scaffold

This is the first thing that runs on-device. Its only job is to make the game's yaw
convention **measurable** instead of assumed.

Nothing in this build snaps the camera. The snap logic is not written yet, on
purpose: it needs the four cardinal readings this scaffold produces.

## What it installs

| Component | Hook / data | Gated on | Fails how |
|---|---|---|---|
| Fact resolution | 2 signatures from `data/1.26.62.cpp` | image present | facts go `MISSING`/`AMBIGUOUS`/`REJECTED`; nothing is installed |
| Turn hook | `LocalPlayerApplyTurnDelta` @ rva `0xaf60090` | fact `AVAILABLE` **and** `minSize > 12` | hook not installed; readout says `fact <state>` |
| Frame clock | `eglSwapBuffers` (libEGL, public symbol) | symbol found | no refresh; startup log still complete |
| Readout | none — draws only | module toggled on | — |

Exactly **one** game-address hook exists (the turn path). The frame clock hooks a
public EGL export, so it needs no signature and survives a Minecraft update.

## Why the turn hook is worth installing before the feature exists

`0xaf60090` is the only fact in this build with full verification behind it
(1408 bytes, 176 instructions, single `ret`, canary). Hooking it produces a live
counter that answers the question Phase A cannot otherwise answer:

> Does the resolved address actually fire when the player looks around?

A counter that stays at zero while the player turns the camera means the address is
wrong, regardless of how good the signature looked. A counter that climbs proves
the turn path is live — and gives a timestamp for every cardinal reading.

## The on-screen readout

Four lines, refreshed at ~10 Hz:

```
Camera Facing
hook: installed
turn calls: 1234
yaw/pitch: no verified accessor in build data
```

The last line is deliberately stubborn. `RotationSource` returns an invalid reading
and names the reason until `data/1.26.62.cpp` carries an `OffsetEntry` with id
`TurnComponentRotation` and a non-empty `reachVia`. That entry only gets written
after a runtime measurement. **A missing number is reported as missing, never as 0.**

## Measuring the convention (the actual Phase A procedure)

1. Install the pack, launch, enable **Camera Facing** in the mod menu.
2. Confirm `turn calls` increments when you move the view. If it does not, stop —
   the address is wrong and every later step is meaningless.
3. Face due north, press **Mark reading**, screenshot the overlay.
4. Repeat for east, south, west.

Four screenshots at known facings pin down yaw origin, sign and units without
guessing. That is the input to the next commit.

## How the yaw reading gets filled in

When the accessor is known, add it to the data file:

```cpp
constexpr OffsetEntry kOffsets[] = {
    {"TurnComponentRotation", "actor", /*delta*/ 0x50, /*reachVia*/ "verified path..."},
};
```

`RotationSource::configure` then flips `ready()` to true, the readout prints real
values instead of the reason, and the log records them. No other file changes.

## What is honest about this build

- **Compiles against the SDK API**: all 10 game-facing files type-check against the
  real `pl::*` headers (`scripts/check_sdk_api.sh`). Verified here.
- **Host tests pass**: 47 checks over parser, resolver rules and report writer
  (`scripts/run_host_tests.sh`). Verified here.
- **Not built for Android**: no NDK in this environment, so there is no `.so`, and
  the artifact has never been loaded by the preloader.
- **Not run on a device**: every claim about in-game behaviour above is a
  prediction, not an observation.

## Failure modes to expect first

| Symptom | Likely cause | Where to look |
|---|---|---|
| `hook: fact MISSING` | pattern no longer matches the installed build | `resolution-report.json` |
| `hook: fact AMBIGUOUS` | pattern matches more than once | report lists `matches` |
| `hook: fact REJECTED` | matched but the prefix or `ret` check failed | report `detail` |
| `hook: target shorter than patch width` | `minSize` missing or ≤ 12 | data file entry |
| `hook: installed`, `turn calls: 0` | address is not the turn path | F-CAM-06/09 — still unresolved |
| readout never appears | module toggled off, or gate closed | logcat `CameraFacing` |

## Report

`config/resolution-report.json` is written at startup. It carries, per fact: state,
RVA, match count, and the structural check result. Live addresses are never written —
only RVAs, so a report stays meaningful across runs.
