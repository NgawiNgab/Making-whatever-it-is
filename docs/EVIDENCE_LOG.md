# Evidence Log

Classifications: **CONFIRMED** / **INFERRED** / **HYPOTHESIS** / **UNKNOWN**
(project instruction §4). No finding enters this log without a source.

## Target binding

Target fixed by the user on 2026-11-15: **MCPE 1.26.62, arm64-v8a** — the same
build as their other native-mod projects.

| Project | Declared / observed target | Source |
|---|---|---|
| BedrockTools (reference) | `1.26.6X.X` (band) | `levimod.json` |
| **This project (target)** | **1.26.62** — user-stated 2026-11-15 | `docs/PROJECT_CHARTER.md` §2 |
| User's other project (Seed-Taker) | `1.26.62`, `libminecraftpe.so` 342,231,392 B, text `0x0-0x13686ff0`, 7 segments | its `evidence/ST-001/zaphkiel_out.txt` header; CRASH-001 records the device |

Targeting the same build as the user's existing work makes some of their prior
evidence **potentially** reusable. It does **not** make it verified: evidence is
bound to a specific library instance, which must be confirmed identical (size +
segment table + a hash if obtainable) before any address or offset crosses over
(`F-024`). Prior findings are not adopted by default.

Every offset/signature in this repository is **UNKNOWN for our target** until
re-verified against the user's exact binary. This is not caution for its own sake:
the reference rewrites ~66% of its signature table (100 of 152 rows) between two
adjacent builds (§F-008).

## Reference-derived facts

| ID | Finding | Class | Evidence / derivation | To confirm / disprove |
|----|---------|-------|-----------------------|------------------------|
| F-001 | Target platform is Android arm64-v8a, native mod loaded by LeviLauncher/preloader | CONFIRMED | BedrockTools `xmake.lua:11-12,31-35`; `README.md` requirements; Zaphkiel-RE `README.md` | N/A |
| F-002 | Reference declares game support as a band `1.26.6X.X`, not a build | CONFIRMED | BedrockTools `levimod.json` `minecraft_versions` | `T-P1` supplies the real build string |
| F-003 | Mod ABI is preloader's `PL_REGISTER_MOD` + `pl::mod::ModContext` lifecycle | CONFIRMED | BedrockTools `src/main.cpp:4-17`; preloader `include/pl/Mod.hpp` | N/A |
| F-004 | Game lib discovered via `dlopen("libminecraftpe.so", RTLD_NOW\|RTLD_NOLOAD)`, else a `dlopen` detour in `libdl.so`; no version check in that path | CONFIRMED | BedrockTools `src/core/Runtime.cpp:41-47,132-144,151-157` | Runtime timing on device (`T-P4`) |
| F-005 | Available memory primitives: `resolveSignature(s)`, `resolveVtableFunction`, `hook`/`unhook` chain, `read/writeBytes`, `revertPatch` | CONFIRMED | preloader `include/pl/memory/{Signature,Vtable,Hook,Patch}.hpp` | N/A |
| F-006 | `hook` is a priority-ordered chain over Gloss detours, keyed by target address | CONFIRMED | preloader `src/pl/memory/Hook.cpp:66-142`; `GlossHook*` at `:6,101-109` | N/A |
| F-007 | Reference has **no** version dispatch; one flat table serves one build | CONFIRMED | Tree-wide grep for version gating = empty; `Version.hpp:11` is the mod's own version; game version read only in `GameHooks.cpp:127-131` | N/A |
| F-008 | A build bump rewrites ~66% of signature rows and ~22% of offsets | CONFIRMED | Diff `6793c48`→`3de9a7d` ("26.60 support"): 100/152 `SignatureDefinition` rows, 47/218 `constexpr` offsets | Re-measure on the next bump to test the ratio |
| F-009 | Patterns wildcard register allocations and frame immediates, not just PC-relative operands | CONFIRMED | `Signatures.cpp:13` vs the `26.52` variant of the same `VersionString` row in the diff | N/A |
| F-010 | Signature resolution is by exact pattern string, returns the **first** match, no uniqueness check, no warning | CONFIRMED | BedrockTools `Signatures.cpp:168-191`; preloader `src/pl/memory/Signature.cpp` `scanPatternWindow`/`scanCompiledPatterns` (found-patterns leave the search set) | N/A (design constraint, see `docs/CROSS_VERSION.md` R-1) |
| F-011 | Two resolution routes (byte pattern or RTTI+slot); empty-both rows can never resolve; `resolveAll` returns true if **any** row resolved | CONFIRMED | `Signatures.hpp:168-173`; `Signatures.cpp:144,151,177-190` | N/A |
| F-012 | Hook installation failures are ignored per-hook; "installed" needs only 2 of the 23 `hookSignature` call sites | CONFIRMED | `GameHooks.cpp:72-79` (`hookSignature` returns bool, callers discard), `:312-334`, `:334` | N/A |
| F-013 | `virtualCall`/`field` perform unchecked dereference of attacker-controlled-slow indices/offsets | CONFIRMED | `include/bedrocktools/sdk/Memory.hpp:40-55` | N/A |
| F-014 | Reference has no self-diagnosis: no resolution report, no counts, no per-fact status | CONFIRMED | Tree review; `resolve(id)` returns `0` only (`Signatures.cpp:193-196`) | N/A |
| F-015 | Zaphkiel is docs-only (7 files) — cannot be linked, built, or invoked by us | CONFIRMED | Zaphkiel-RE tree; `README.md` | N/A |
| F-016 | Zaphkiel target grammar (`fn/add/scan/range/vft/strfn/sig/struct/xref_tree/fullf/fullvf/fc/vfc/hier/findclass/h`) plus `entt/entthash/vftdiag` from `zEnTT.md` | CONFIRMED | Zaphkiel-RE `README.md` "Target Syntax"; `zEnTT.md`; `zaphkiel_config.txt` comments | N/A |
| F-017 | Output artefacts and their device paths | CONFIRMED | Zaphkiel-RE `README.md` "Output Files"; `zaphkiel_config.txt` `out_dir` | N/A |
| F-018 | Zaphkiel's generated signature format is accepted by preloader's parser | CONFIRMED (format) | Zaphkiel-RE `README.md` "Signature Generator"; preloader `Signature.cpp:64-96` (`?`/`??`/partial masks) | End-to-end: take a generated sig, `sig<>` it back, then load it (`T-P3`) |
| F-019 | preloader anchors on the longest fully-specified byte run, capped at 8; `??` never anchors | CONFIRMED | preloader `Signature.cpp:23,147-190`; anchor use at `:313-320` | N/A |
| F-020 | Zaphkiel addresses are ASLR-slid live VAs, not reusable across runs | CONFIRMED | Seed-Taker `evidence/ST-001/zaphkiel_out.txt` header (segments + note); observed VA drift between its ST-001/ST-002 | N/A |
| F-021 | Reference provides no version registry, verification, disambiguation, gating, or compatibility reporting | CONFIRMED (absence) | F-007, F-010, F-012, F-014 | N/A |
| F-022 | Reference has no relationship to Zaphkiel (no reference to it in full history) | CONFIRMED | `grep -rni zaphkiel` over full cloned history = empty | N/A |
| F-023 | Correctness of any reference signature/offset for *our* build is unknown | UNKNOWN | Reference bands vs. our unknown target | `T-P1` + per-fact re-verification |

## Open questions this project must answer

| ID | Question | Class | Next step |
|----|----------|-------|-----------|
| Q-001 | ~~What exact build are we on?~~ **ANSWERED 2026-11-15: 1.26.62 arm64** (user-stated). Library instance identity still open — needs size + segment table for the user's exact device | INFERRED (user statement; not yet binary-verified) | `T-P1` header confirms the instance |
| Q-002 | Is `libminecraftpe.so`'s RTTI/`.dynsym` reachable in the user's mapping (needed for the D1 route)? | UNKNOWN | `T-P1` (`findclass<>` + `dynsym` presence) |
| Q-003 | Does a generated signature survive round-trip: `sig<>` returns exactly 1 match at the same function? | UNKNOWN | `T-P2`/`T-P3` |
| Q-004 | Which single function is a safe "is the game alive and is this build ours" probe? | UNKNOWN | `T-P1` candidate selection → `T-P2` verification |
| Q-005 | ~~What should be built?~~ **ANSWERED 2026-11-15: a feature mod**, and the first feature is specified: **camera facing** (`docs/features/CAMERA_FACING.md`) | CONFIRMED (as requirement) | T-CAM1 |
| Q-006 | Is the camera driven by the player-rotation → turn-delta path, or by `InputComponent::cameraOrientation`, or by its own component? | UNKNOWN | T-CAM1 step 2 (this decides V1 feasibility) |
| Q-007 | Yaw convention on this build: degrees or radians, range/wrap, and which sign is which cardinal? | UNKNOWN | T-CAM1 step 2 (`fn<>` of the turn function) |

## Platform capabilities we build on (no game facts)

**F-024 — The launcher surface carries a complete HUD/UI toolkit. CONFIRMED.**
`pl::modmenu` exposes `registerModule` (typed `ConfigEntry` set: toggle/slider/
radio/color/keybind/text/button), `submitDrawCommands` with
`DrawCommandType {Text, Rect, Line, RectFilled, CircleFilled, TriangleFilled,
Image}`, `submitHudEditorElements` + `HudEditorElement` (per-element position keys,
snapping, grid), `registerFont`, `registerImage`, `registerButton` (+
`ButtonBehavior`, `registerButton`/`ButtonEvent`), `getHudSurfaceSize`
(`preloader-android/include/pl/ModMenu.hpp`: `HudEditorElement` 94-114, `ModuleInfo` 131-147, `ButtonInfo` 151-177, `DrawCommand` 179-194, entry points 197-246). `pl::input` adds touch/key/
mouse/text callbacks (`include/pl/Input.hpp`).
Consequence, and it shapes the whole plan: **Tier-0 features (HUD/UI/input) need
no game-binary facts at all.** Only game *state* does. See `docs/SPEC.md` §2.

**F-025 — Font/image resources ship inside the `.levipack`. CONFIRMED.**
The packager writes `manifest.json`, the library, `icon.png`, and
`resources/minecraft.ttf` (`BedrockTools/scripts/package_levipack.py:33,58-62`);
the reference registers it with `pl::modmenu::registerFont("minecraft", font)`
(`src/modules/hud/debugmenu.cpp:293`).

**F-026 — Hook patch width can exceed the target function and corrupt its
neighbour. CONFIRMED (runtime crash on 1.26.62).**
User's tombstone on this build: SIGILL `ILL_ILLOPC`, crash at lib+`0x10b1da10`,
caused by an 8-byte getter hook whose ≥12-byte inline patch destroyed the
adjacent function `0x10b1da10`, later called by a ctor and load-bearing. Source:
Seed-Taker `evidence/CRASH-001/README.md` (+ `tombstone.txt`) and its `F-073`.
Same run proved the surrounding machinery worked (slide + body-verify + detour +
snapshot all functioned before the crash) — the failure was patch width, not
resolution. → requirements HS-1..HS-4 in `docs/SPEC.md` §5.

**F-027 — User's device/runtime context on this build. CONFIRMED (their evidence).**
Samsung SM-A566B, Android 17 (API 37), arm64, non-rooted, LeviLauncher as the host
process. Source: Seed-Taker `evidence/CRASH-001/README.md`. Relevant because
non-rooted + launcher-hosted is exactly the constraint set this project targets.

## Camera-facing feature (T-CAM1)

Findings for `docs/features/CAMERA_FACING.md`. All are reference-derived unless
marked otherwise: **none is verified on 1.26.62 yet.**

| ID | Finding | Class | Evidence / derivation | To confirm / disprove |
|----|---------|-------|-----------------------|------------------------|
| F-030 | The local look/turn path is a function taking `(Player*, Vec2* delta)`; two independent reference modules agree on the signature and role | CONFIRMED (reference source) | `BedrockTools/src/modules/visual/cinematiccamera.cpp:15-31` (scales the delta before forwarding), `freelook.cpp:22,224-237` (captures/restores around it) | T-CAM1 `sig<>` count + `fn<>` body on our binary |
| F-031 | Player rotation is a *pointer* to a component (`Actor::mActorRotationComponent = 0x238`); the component's first `Vec2` is rotation, and Freelook copies 16 bytes from it | CONFIRMED (reference source) / UNKNOWN (our build) | `sdk/offsets/World.hpp:17`; `sdk/world/Actor.hpp:31-34`; `freelook.cpp:126-132,163` (16-byte save) | T-CAM1 step 2: `struct<LocalPlayer><0x238>` |
| F-032 | A separate camera component exists (`CameraComponent::mOrientation = 0x28`, 16 bytes) written by the `UpdatePlayerFromCamera` pass; its semantic is never interpreted by the reference | CONFIRMED (existence) / HYPOTHESIS (`{yaw,pitch,?,?}`) | `sdk/offsets/Render.hpp:142-144`; `freelook.cpp:21,193-222`; `motionblur.cpp:539` | T-CAM1 step 2: disassemble the camera update; decide G-4 |
| F-033 | Every camera relevant reference offset/signature is **unknown for 1.26.62** | UNKNOWN | Reference targets a `1.26.6X.X` band on a different version line (F-002/F-023); F-008/F-009 measure the churn | T-CAM1 |
| F-034 | The game continuously overwrites rotation from input, so the feature must *hold* an orientation, not set it once | INFERRED (strong) | Reference's own session/restore-per-tick design (`freelook.cpp:126-141,224-237,353-359`); mobile look input is continuous by construction | T-CAM1 step 2: how the delta is applied |

## Investigations

- **T-P1** (READY, queued): library-instance confirmation. Satisfied
  automatically by any run whose `zaphkiel_out.txt` header is provided.
- **T-CAM1** (READY, **issued 2026-11-15**): camera turn path + rotation storage
  → `docs/investigations/T-CAM1.md`. First feature-specific run; unblocks
  `docs/features/CAMERA_FACING.md`.

The earlier queue items in `docs/INVESTIGATIONS.md` (`T-P2`, `T-P2.5`, `T-P3`)
are pipeline hardening, not prerequisites for the camera feature; T-CAM1 supersedes
them for now and T-P2.5's hook-length check is folded into T-CAM1 step 2 (G-6).

## Change history

- 2026-11-15: log created. Reference snapshots pinned (`docs/REFERENCE_FINDINGS.md`
  §"Snapshots inspected"). No binary evidence of our own yet.
