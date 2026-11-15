# Implementation Plan

Companion to `docs/SPEC.md` (what) and `docs/CROSS_VERSION.md` (why this shape).
Ordered so that each step is independently useful and nothing waits on evidence it
does not need.

## Ordering principle

The mod must be able to **come up, show its state, and prove it is on the right
build** before it is allowed to touch the game. So: launcher-only skeleton first
(no facts), then the evidence-gated layers behind a gate that stays closed until
facts exist.

## Phase 0 — Project scaffold (no game facts)

**Objective.** A loadable `.levipack` mod with lifecycle, config, menu, and an
empty feature set that reports its own status.

Deliverables:

```
xmake.lua                       android arm64 shared lib + levipack step
levimod.json                    declares target build 1.26.62 explicitly
scripts/package_levipack.py     manifest + .so + font -> .levipack
include/<ns>/Version.hpp        identity, parsed by packager
include/<ns>/Export.hpp
include/<ns>/{events,sdk,memory}/  same layering as the reference (F-003)
src/main.cpp                    PL_REGISTER_MOD -> Runtime
src/core/Runtime.*              load/enable/disable/unload, dlopen watch
src/core/GameHooks.*            EGL frame pump + install/uninstall of game hooks
src/core/Facts.*                the resolution layer (D-2..D-5)
src/modules/Module.hpp          lifecycle + declared fact dependencies
src/modules/ModuleRegistry.*    ownership, fan-out, per-frame draw
src/config/ConfigManager.*      debounced JSON store in configDir()
src/launcher/ModuleMenu.*       pl::modmenu registration + config mapping
tests/                          host-side unit tests (NFR-4)
```

Exit criteria: builds on CI; mod appears in the launcher menu; a dummy module
toggles and persists; unload is clean. **No game symbol is touched.**

## Phase 1 — The facts layer (the part the reference lacks)

**Objective.** Implement R-1..R-5 so that later features cannot make the
reference's mistakes.

Deliverables:

- `Fact` descriptor: `{id, kind (Signature|RttiSlot|Offset), data,
  structural_check, required_by}`.
- `Facts` registry: resolves all facts once, at a controlled point, records
  `{state, rva, matches, check_result}` per fact.
- **Uniqueness by counting**, not first-hit (R-1). This needs a matcher that
  counts all occurrences; preloader's `resolveSignatures` stops at the first
  (`docs/REFERENCE_FINDINGS.md` F-010), so the counter is our own code over
  `readBytes`/module regions, with the preloader scanner used only for the
  final address confirmation.
- **Structural self-check** (R-2): compare the resolved function's prologue and
  a small number of build-independent properties against the data file.
- **Report writer** (R-4/R-5): `resolution-report.json` + a log line per fact.
- **Build identity** (FR-5): data-file selection by build string; no match ⇒
  unsupported mode, gate closed.

Exit criteria: with a two-fact test data file, a deliberately duplicated pattern
is reported `AMBIGUOUS` and not installed; a wrong-build file yields
`UNSUPPORTED`; the report matches the log.

## Phase 2 — First Tier-0 features

**Objective.** Real user-visible value with zero binary evidence.

Candidate set (pick with the user, §7 of the spec): FPS/frame-time HUD,
clock/session timer, coordinates-style readout from *launcher-provided* data only,
config-driven HUD layout via the editor.

Exit criteria: on-device, each feature renders, toggles, persists, and appears as
a movable element in the HUD editor. Frame cost reported.

## Phase 3 — Tier 1: first game-state read

**Objective.** Prove the whole evidence chain on the smallest possible fact.

Sequence: issue the investigation for one getter → classification → data file
entry → facts layer resolution → feature reads through the resolved accessor →
report shows `AVAILABLE` with match count 1 and a passing structural check.

This is the first phase that needs Zaphkiel output, and it is deliberately a
*single* fact. If the chain holds for one, it holds for the rest.

## Phase 4 — Tier 2 + scale-out

Hooks with HS-1..HS-4 enforced; packet-driven features; per-feature fact
dependency declarations so an update disables exactly what broke.

## What each phase needs from the user

| Phase | Needs |
|---|---|
| 0 | Nothing. Build/CI only (CI needs the repo's Actions; local build needs NDK r28c + xmake) |
| 1 | Nothing new; uses a synthetic test data file |
| 2 | On-device install of a test `.levipack`; confirmation of which Tier-0 features you want |
| 3 | **A Zaphkiel run** (`T-P1` + one targeted follow-up). This is the first hard dependency |
| 4 | Further targeted Zaphkiel runs, one per fact group |

## Risks and mitigations

| Risk | Mitigation |
|---|---|
| A resolved pattern is unique but wrong (F-019 anchor weakness) | R-2 structural check; identity verified at the landing site during investigation, not at runtime |
| Hook patch is wider than the target (real crash on this build, HS-1) | Measure target length before hooking; prefer long bodies |
| Offset drifts silently (D3) | Reach fields via resolved accessors; validate layouts before use |
| Feature works today, breaks after a game update | Per-feature fact declarations ⇒ post-update report names the broken facts; no code change needed |
| Launcher/preloader API differs from what we inferred | F-002/F-005 are header-confirmed against a pinned preloader commit; re-verify on build breakage |

## Explicitly not claimed

No line of this project has been compiled or run. There is no NDK, no xmake, and
no device in this environment. Phase 0's exit criteria are CI-verifiable; Phase 2+
exit criteria require the user's device. Any statement of a passing build will be
made only from an actual build log.
