# T-CAM1 — Camera turn path and rotation storage

**Investigation ID:** T-CAM1
**Status:** READY — can be combined with T-P1 into a single run (see last section).
**Feature:** `docs/features/CAMERA_FACING.md` (Camera Facing, V1).
**Decay classes involved:** D2 (byte patterns), D1 (RTTI/vtable), D3 (offsets).

## Objective

On **our** `libminecraftpe.so` (MCPE 1.26.62, arm64), establish:

1. which function receives the local look/turn delta, and that its byte pattern is
   **unique** in this binary (gate G-1);
2. how that function's two floats are interpreted — rate vs. absolute, units,
   sign, wrap (G-2, G-5);
3. where the player's yaw/pitch are stored and how the camera reads its view
   (G-3, G-4);
4. how long the target function is, so the hook patch cannot spill into a
   neighbour (G-6, `F-026`).

## Current evidence

- Mechanism and calling convention as recorded in `docs/features/CAMERA_FACING.md`
  (F-030..F-032), **all from the reference, none verified on our binary**.
- The reference's own byte patterns for the six relevant functions
  (`BedrockTools/src/core/memory/Signatures.cpp` lines 38, 40, 75, 152, 153, 154 —
  v1.5.12, a `1.26.6X.X` band, a *different* version line from ours).
- Their wildcarded shape was also observed to change between the reference's own
  builds (F-009), so these are **candidates to test, not values to trust**.

## Unknowns

- Which of the reference's six patterns still match on 1.26.62, and with what
  match **count** (count ≠ 1 disqualifies a pattern for use, F-010/R-1).
- The identity of the function each pattern lands on.
- Delta semantics, units, sign and wrap convention (G-2, G-5).
- Whether the camera derives its view from the player rotation or from its own
  component (G-4).
- Function lengths for hook safety (G-6).

## Commands

### Step 1 — `zInput.txt`

Copy the whole block.

```
findclass<LocalPlayer>
findclass<Camera>
findclass<CameraComponent>
hier<LocalPlayer>
hier<Camera>
sig<?? ?? ?? D1 ?? ?? ?? FD ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? 91 56 D0 3B D5 F3 03 00 AA F4 03 01 AA ?? ?? ?? F9 ?? ?? ?? F8 ?? ?? ?? F9 ?? ?? ?? F9>
sig<?? ?? ?? F9 ?? ?? ?? 52 00 E4 00 2F ?? ?? ?? 72 ?? ?? ?? A9 08 01 0B CB ?? ?? ?? D3 ?? ?? ?? 51 0C 01 0A 8A ?? ?? ?? B9 6B 0D 0C 8B ?? ?? ?? F9 ?? ?? ?? F9 ?? ?? ?? B1 ?? ?? ?? 54 8B 15 0B 8B ?? ?? ?? B9 BF 01 0A 6B ?? ?? ?? 54 ?? ?? ?? F9 7F 01 09 EB ?? ?? ?? 54 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? A9 ?? ?? ?? D3 8C 01 0A CB 7F 0D 8C EB ?? ?? ?? 54 ?? ?? ?? F8 ?? ?? ?? B4 ?? ?? ?? 92 ?? ?? ?? 12 ?? ?? ?? 92 ?? ?? ?? B8 ?? ?? ?? 32 48 01 08 4A 1F 01 0B 6B ?? ?? ?? 54 ?? ?? ?? D3 ?? ?? ?? F9 01 E4 00 2F>
sig<?? ?? ?? F9 ?? ?? ?? 52 ?? ?? ?? 72 ?? ?? ?? A9 08 01 0B CB ?? ?? ?? D3 ?? ?? ?? 51 0C 01 0A 8A ?? ?? ?? B9 6B 0D 0C 8B ?? ?? ?? F9 ?? ?? ?? F9 ?? ?? ?? B1 ?? ?? ?? 54 8B 15 0B 8B ?? ?? ?? B9 BF 01 0A 6B ?? ?? ?? 54 ?? ?? ?? F9 7F 01 09 EB ?? ?? ?? 54 ?? ?? ?? F9 ?? ?? ?? B4 ?? ?? ?? A9 ?? ?? ?? D3 8C 01 0A CB 7F 0D 8C EB ?? ?? ?? 54 ?? ?? ?? F8 ?? ?? ?? B4 ?? ?? ?? 92 ?? ?? ?? 12 ?? ?? ?? 92 ?? ?? ?? B8 ?? ?? ?? 32 48 01 08 4A 1F 01 0B 6B ?? ?? ?? 54 ?? ?? ?? D3 ?? ?? ?? F9 ?? ?? ?? 92 ?? ?? ?? F8 ?? ?? ?? B4 ?? ?? ?? 92 ?? ?? ?? 92 08 0D 09 8B ?? ?? ?? 2D C0 03 5F D6>
sig<?? ?? ?? D1 ?? ?? ?? FD ?? ?? ?? A9 ?? ?? ?? F9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? 91 54 D0 3B D5 ?? ?? ?? 52>
sig<?? ?? ?? A9 FD 03 00 91 ?? ?? ?? F9 ?? ?? ?? F9 ?? ?? ?? F9 00 01 3F D6 ?? ?? ?? F9 ?? ?? ?? F9 ?? ?? ?? A8 20 00 1F D6 ?? ?? ?? A9 FD 03 00 91>
sig<?? ?? ?? D1 ?? ?? ?? A9 ?? ?? ?? F9 ?? ?? ?? 91 53 D0 3B D5 E8 03 00 AA ?? ?? ?? 91 ?? ?? ?? F9 ?? ?? ?? 91 ?? ?? ?? F8 ?? ?? ?? ?? ?? ?? ?? 91 ?? ?? ?? ?? ?? ?? ?? 36 ?? ?? ?? 91 ?? ?? ?? 52 ?? ?? ?? ?? ?? ?? ?? ?? E0 03 1F AA ?? ?? ?? F9 ?? ?? ?? F8 1F 01 09 EB ?? ?? ?? 54 ?? ?? ?? A9 ?? ?? ?? F9 ?? ?? ?? 91 C0 03 5F D6 ?? ?? ?? ?? C0 03 5F D6>
```

**Why each line:**

| Target | Purpose |
|---|---|
| `findclass<LocalPlayer>` | D1 route anchor: confirms RTTI readable and gives the exact mangled form for the type that owns the turn function. |
| `findclass<Camera>` | Does a polymorphic `Camera` exist in this build, or is the camera a raw component struct? Decides G-4's approach. |
| `findclass<CameraComponent>` | Whether `CameraComponent` is identifiable by name at all. |
| `hier<LocalPlayer>`, `hier<Camera>` | Base-class chains → which vtable slots are inherited, needed before any slot work. |
| `sig<>` × 6 (lines above) | The reference's patterns for `LocalPlayerApplyTurnDelta`, `ActorGetHeadRotation`, `ActorSetHeadRotation`, `UpdatePlayerFromCamera`, `GetPerspective`, `ClientInstanceGetLocalPlayer`. **The match count is the finding**, not the address. |

The `sig<>` lines were generated mechanically from the reference table (`?` → `??`
for target syntax); 56/168/192/36/48/116 bytes respectively. They are long because
the reference wildcards register allocations as well as PC-relative operands
(F-009) — that also means they can be *too* specific for a different build, which
is exactly what the match count measures.

### Step 2 — follow-up, using addresses from Step 1

Step 2 cannot be written until Step 1 returns addresses. It will be:

```
fn<0xADDR_OF_TURN_DELTA>
fn<0xADDR_OF_CAMERA_UPDATE>
fn<0xADDR_OF_HEADROT_GET>
struct<LocalPlayer><0x238>
```

Purpose: `fn<>` gives ground truth on the function body, **instruction count**
(G-6), and how the two float arguments are used (G-2/G-5); `struct<>` tests
whether the rotation pointer offset is still `0x238` on this build.
**Do not run step 2 until step 1's counts are known** — running it against a
non-unique pattern wastes a run and produces evidence we must discard.

### Config for this run

```
dump_rtti = true
dump_dynsym = true
separate_dump_files = true
max_fn_insns = 2000      /* step 2: the turn function is not tiny */
```

## Expected output

`zaphkiel_out.txt` (required), `rtti_dump.txt`, `dynsym_dump.txt`.
Send the files as-is; do not trim. Addresses are ASLR-slid — they will be
converted to segment-relative RVAs before use (`docs/CROSS_VERSION.md` §6.1).

## Decision criteria

| Outcome of the `sig<>` lines | Reading | Next |
|---|---|---|
| turn-delta count **= 1**, others mostly 1 | Patterns still valid on 1.26.62 | Step 2: `fn<>` the turn function → confirm delta semantics → G-6 length → implementation |
| turn-delta **= 1**, others 0 | Only the turn path carried over | Proceed on the turn path alone; re-derive the rest in a later focused run |
| count **≥ 2** | Pattern is not an identity on this build | **Reject it.** Step 2 becomes `fn<>` both hits + `sig_targets.txt` regeneration for a unique anchor |
| turn-delta **= 0** | Reference pattern tuned to another build | Pivot: locate the function by behaviour (`findclass<LocalPlayer>` + `struct<>`/xref), then generate our own pattern with `sig_targets.txt` |
| `findclass<>` all empty | RTTI not readable this run | RTTI route dead → everything becomes D2-only; report immediately, do not continue with offsets |

**What would disprove the plan:** if the camera is driven from
`InputComponent::cameraOrientation` rather than from the player rotation or a
camera component, then V1-by-turn-delta cannot hold the view, and the feature must
be re-designed around the input component. Step 2's disassembly of the turn
function and the camera update is what settles it.

## Can I skip T-P1?

T-P1 only confirms the library image (size, segments, text range) — facts that
`zaphkiel_out.txt`'s header carries automatically. **One run satisfies both.** Run
T-CAM1 with `dump_rtti`/`dump_dynsym` on; this project will log T-P1 as
satisfied-by-T-CAM1.

## What happens after this

If the turn-delta pattern is unique on our binary, the implementation is small:
resolve one function, hook it, drive or suppress the delta while engaged, restore
cleanly on release. The rest of the feature — cardinal presets, snap-to-nearest,
smoothing, config UI — is launcher-side work needing no further binary evidence.
