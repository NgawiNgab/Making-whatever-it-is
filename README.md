# Making-whatever-it-is

A **feature mod** for **Minecraft Bedrock 1.26.62 on Android `arm64-v8a`**
(LeviLauncher/preloader), built under a strict reference-and-evidence discipline.

Status: **first feature specified, awaiting one Zaphkiel run.**
The first feature is **Camera Facing** — snap the view to a chosen yaw, cardinal
directions included: [`docs/features/CAMERA_FACING.md`](docs/features/CAMERA_FACING.md).
The investigation that unblocks it is written and ready to run:
[`docs/investigations/T-CAM1.md`](docs/investigations/T-CAM1.md).

## Documents

| Document | What it holds |
|---|---|
| [`docs/features/CAMERA_FACING.md`](docs/features/CAMERA_FACING.md) | **The first feature**: behaviour, camera model, evidence gates, risks |
| [`docs/investigations/T-CAM1.md`](docs/investigations/T-CAM1.md) | **The run to execute**: exact `zInput.txt`, purpose per line, decision criteria |
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

Run **T-CAM1** ([`docs/investigations/T-CAM1.md`](docs/investigations/T-CAM1.md))
and send back `zaphkiel_out.txt`, `rtti_dump.txt`, `dynsym_dump.txt`. That single
run also satisfies T-P1's library-identity check, so it is one round trip.

What it answers: whether the reference's turn-delta signature is still unique on
1.26.62, which function it lands on, how that function interprets its two floats,
and whether the camera reads the player's rotation or its own component — the last
one decides whether "camera only, body unchanged" is reachable.
