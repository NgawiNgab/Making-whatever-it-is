# T-CAM2 — Delta convention + live verification

**Investigation ID:** T-CAM2
**Status:** READY. Small, targeted follow-up to T-CAM1.
**Feature:** `docs/features/CAMERA_FACING.md` (V1).
**Predecessor:** `evidence/T-CAM1/` — turn path **confirmed** at RVA `0xaf60090`.

## What T-CAM1 already settled

- `LocalPlayerApplyTurnDelta` is a **real, unique** identity at RVA `0xaf60090`
  (1408-byte function, 176 instructions) — F-CAM-02/03.
- It does: read angle pair → add delta components → wrap → gate on `|delta|²` →
  negate one axis → write back at `component+0x4` — F-CAM-04/05.
- Three reference patterns are false-uniques and are **rejected** — F-CAM-06/07.
- The camera subsystem is EnTT/ECS with 66 named `basic_storage` types, including
  `MinecraftCamera::CameraComponent`, `…ActiveCameraComponent`,
  `…CurrentInputCameraComponent`, `…CameraDirectLookComponent`,
  `…CameraAimAssistRotationOverrideComponent` — F-CAM-10/11.

## What is still missing (and why it blocks code)

| # | Question | Why it blocks |
|---|---|---|
| A | **Yaw convention**: units (deg/rad), range/wrap, and which compass direction each of the four cardinal yaws corresponds to | "Face north" that is 180° off is worse than nothing (G-5) |
| B | Which of the two axes is **yaw** and which is **pitch**, and what the negation means | We must write the correct slot, with the correct sign (F-CAM-05) |
| C | Is the angle pair the **camera's** view too, or does the camera read its own `CameraComponent`? | Decides whether V1 "turn the player" automatically turns the view (Q-006) |
| D | Does `0xaf60090` run for the **local** player only (not remote/other players) | If it runs for every player, hooking it needs a target check or it will corrupt others |

## Design note — prefer measurement over more disassembly

A/B are **not** best answered by reading more instructions: the wrap helper
(`0xe830314`) and the constant at `0xaf604c4` only constrain the answer. The
decisive, cheap evidence is a **live memory read** at a known on-screen facing.

That requires one small codeable step first — see "Phase A" — but it needs only
facts we already have.

---

## Phase A — live angle readout (the decisive test)

**Objective.** Print the live angle pair while the player stands facing a known
direction, so the convention falls out of data instead of inference.

**Facts required (all confirmed or already resolved):**

- `ClientInstance` pointer: `+0x240` is the local-player slot — corroborated
  independently by the turn path and by the getter `0x9b7f6d0` (F-CAM-04/08).
- Rotation pair: reached through the EnTT component the turn path uses; the pair
  lives at **component+0x4**, and the component is keyed by hash `0x2c74480`
  (storages at `this+0x10` / `+0x18`).
- Accessor: calling the inlined pair read/write is not possible, but the
  **component pointer walk** is the same shape the reference uses
  (`freelook.cpp:50-91`) and can be re-implemented against F-CAM-04's offsets.

**Method.** Stand still facing a known way (e.g. due north by the in-game compass
or by placing/observing a known feature), read and log the pair; repeat for the
other three cardinals and for a +90° pitch (straight up / down). Four to six
readings settle A and B completely.

**Acceptance:** the logged pair changes monotonically as the player turns one
compass direction; the two components are separable as yaw vs pitch; the numbers
at the four cardinals differ by ~90° in the same units.

**Fallback if the component walk proves fragile:** `entt<ActorRotationComponent>`
and `entt<ActorStateVectorComponent>`-style runs to get the component's **size**
first (the `zEnTT` method), which makes the walk safe by construction.

## Phase B — confirm the camera follows (question C)

**Objective.** Decide whether V1 alone turns the view.

**Method.** With the pair being read live (Phase A), turn the player by an unusual
amount and check whether the rendered view changes with it. If yes → C answered
"yes" and V1 is a single write. If the view does not follow, the camera reads its
own component, and V2/camera-side work becomes necessary.

**No new Zaphkiel run is needed for B** beyond what Phase A produces; it is a
runtime observation on device.

## Phase C — only if needed: camera-side component

If C says the camera has its own orientation, run one focused `entt<>` on the
relevant camera component to get its size, then `struct<>` on the hashed offset
to find the orientation field. Do **not** run `fullvf<Camera*Component>` — these
are ECS structs, not vtables (F-CAM-06's failure mode came from guessing that a
pattern marked a function entry).

---

## Optional Zaphkiel-only sub-run (if the user prefers no code first)

If a live code path is not wanted yet, this can narrow A/B slightly:

```
fn<0xe830314>
fn<0xaf604c4>
vft<LocalPlayer><0>
```

- `fn<0xe830314>` — the wrap helper. Whether it is a `fmod`-style 360 wrap or a
  ±180 normalisation narrows the convention.
- `fn<0xaf604c4>` — the constant materialised at that address tells us the gate's
  scale (`movi v1.2s, #0x34, lsl #0x18` is a bit pattern, not a float literal).
- `vft<LocalPlayer><0>` — not needed for V1; include only if convenient.

**Honest assessment:** this sub-run can confirm a *range* but cannot settle which
compass direction is 0°. **Phase A is the only thing that settles A/B.** Recommend
Phase A.

## Decision criteria

| Outcome | Meaning | Next |
|---|---|---|
| Pair changes monotonically with turning; cardinals ~90° apart | Convention identified | Implement: target-driven delta on `0xaf60090`, hold while engaged, restore on release |
| Pair does not correlate with facing at all | The pair is not the view orientation (e.g. head vs body) | Re-read F-CAM-04: check whether `+0x4` write belongs to a different component than the read |
| View does not move when the pair changes | Camera reads its own component | Phase C, then design V1 as "player + camera component together" |
| `0xaf60090` also fires for non-local players | Hook needs a target filter | Add a local-player pointer compare before applying |

## Risk carried into implementation

`0xaf60090` is 1408 bytes, so the patch-width hazard (`F-026`, HS-1) is **not**
a concern here — it is one of the safest hook targets we could pick. The residual
risk is semantic, not structural: applying a delta to the wrong axis, or holding
for one frame too long and producing visible drift.

## Deliverable from this investigation

`evidence/T-CAM2/` — the runtime log of the live readings (values + the facing the
player was on for each), plus a short README recording the on-device setup. That
log, not prose, is what the implementation's constants will be derived from.
