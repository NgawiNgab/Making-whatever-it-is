# Making-whatever-it-is

A **feature mod** for **Minecraft Bedrock 1.26.62 on Android `arm64-v8a`**
(LeviLauncher/preloader), built under a strict reference-and-evidence discipline.

Status: **turn path confirmed on 1.26.62; one field measurement left.**
First feature: **Camera Facing** — snap the view to a chosen yaw, cardinal
directions included: [`docs/features/CAMERA_FACING.md`](docs/features/CAMERA_FACING.md).

T-CAM1 is complete (`evidence/T-CAM1/`): the look/turn function is a unique,
1408-byte real function at RVA `0xaf60090`, and its delta is **additive per call**
with one axis negated. Three of the other five reference patterns turned out to be
false uniques and were rejected. Remaining: a live angle readout to pin the yaw
convention — [`docs/investigations/T-CAM2.md`](docs/investigations/T-CAM2.md).

## Documents

| Document | What it holds |
|---|---|
| [`docs/features/CAMERA_FACING.md`](docs/features/CAMERA_FACING.md) | **The first feature**: behaviour, camera model, evidence gates, risks |
| [`docs/investigations/T-CAM2.md`](docs/investigations/T-CAM2.md) | **Next step**: live angle readout that pins the yaw convention |
| [`evidence/T-CAM1/`](evidence/T-CAM1/) | The T-CAM1 run: verbatim outputs + a README of verified findings |
| [`docs/SPEC.md`](docs/SPEC.md) | What the mod does: requirements, feature tiers, hook-safety rules |
| [`docs/IMPLEMENTATION_PLAN.md`](docs/IMPLEMENTATION_PLAN.md) | Phases 0–4, exit criteria, what each phase needs from the user |
| [`docs/CROSS_VERSION.md`](docs/CROSS_VERSION.md) | How a game update becomes a *data* change, not a rewrite |
| [`docs/REFERENCE_FINDINGS.md`](docs/REFERENCE_FINDINGS.md) | BedrockTools + Zaphkiel, every claim cited to path and line |
| [`docs/EVIDENCE_LOG.md`](docs/EVIDENCE_LOG.md) | Classified knowledge: CONFIRMED / INFERRED / HYPOTHESIS / UNKNOWN |
| [`docs/INVESTIGATIONS.md`](docs/INVESTIGATIONS.md) | Zaphkiel requests with exact commands and decision criteria |
| [`docs/PROJECT_CHARTER.md`](docs/PROJECT_CHARTER.md) | The two decisions, the ten settled architecture rules, scope |

## The three findings that shape the design

1. **The reference is single-build by construction.** BedrockTools has no version
   dispatch anywhere; on its own `26.52 → 26.60` support commit, **100 of 152
   signature rows** and **47 of 218 offset constants** changed. So version-coupled
   facts here live in per-build data files, checked at runtime, never in `src/`.
2. **Resolution must prove itself.** The reference takes the *first* signature
   match, has no uniqueness check, reports success if *any* row resolved, and
   reads fields/vtable slots through unchecked dereferences. Here a signature must
   be unique, land on the function it claims, and pass a structural self-check —
   otherwise the feature that needed it is disabled with a stated reason.
3. **The launcher already gives us the whole UI.** `pl::modmenu` provides typed
   module config, text/rect/line/circle/image drawing, a drag-and-drop HUD editor,
   fonts, images and on-screen buttons. **Tier-0 features (HUD/UI/input) therefore
   need no game-binary facts at all** — which is why this project can ship
   something real before any reverse engineering.

## Environment reality

No line of this project has been compiled or run. This workspace has no Android
NDK, no xmake, and no target device. Build and runtime results will be reported
only from actual logs — from CI for the build, and from your device for anything
that touches the game.

## Next step

The turn function is found and is safe to hook. What remains is **which number
means which direction** — a measurement, not more disassembly:
[`docs/investigations/T-CAM2.md`](docs/investigations/T-CAM2.md) Phase A logs the
live angle pair at four known facings and settles the convention from data.
