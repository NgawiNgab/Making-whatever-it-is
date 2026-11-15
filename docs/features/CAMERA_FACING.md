# Camera Facing — feature brief

Working title: **Camera Facing** (snap the view to a chosen yaw/pitch).
Target: MCPE **1.26.62**, arm64-v8a, LeviLauncher/preloader.

This is the project's first real feature and the first one that needs game
binary evidence. Everything below separates what the reference *claims* from what
has been *verified on our build* — currently nothing is verified on our build.

---

## 1. Behaviour

**Primary (as requested).** Set the camera to a chosen facing:
`0 0 0 YAW` and the four cardinal directions (N/E/S/W), plus a "snap to nearest
cardinal" mode. Practically:

- a control that, when activated, makes the view face a selected yaw (and a fixed
  or configurable pitch);
- cardinal presets (0/90/180/270) and an arbitrary yaw value;
- smooth transition option vs. instant snap.

**Intended variants (choose one, they are not the same product):**

| Variant | Player body follows the camera? | Movement relative to? | Notes |
|---|---|---|---|
| **V1 — Turn the player** | yes: body + head + camera all face the target | new facing | Feels like the game's own `/tp`-style facing; affects where you walk and where you aim |
| **V2 — Camera only** | no: body keeps its facing | old facing | Pure view override; closer to a "cinematic" look |

**Recommendation: V1 first.** It is the smaller change, and it is the one the
reference has working code for (below). V2 requires an additional mechanism
(suppressing the game's own camera-to-body sync), so it is a second step, not a
first.

## 2. Design: what the camera actually is

Evidence-backed model of the camera path:

**F-030 — The turn input path is the reference's chosen interception point. CONFIRMED (reference source).**
`LocalPlayer` receives look deltas through a function resolved as
`SignatureId::LocalPlayerApplyTurnDelta` and invoked as
`void(*)(Player*, Vec2* delta)` where `Vec2` is `{float x; float y;}` —
`BedrockTools/src/modules/visual/cinematiccamera.cpp:15-31` and
`src/modules/visual/freelook.cpp:22`. The Cinematic Camera module works by
scaling that delta before forwarding it (`cinematiccamera.cpp:21-31,123-148`);
Freelook works by capturing/restoring player rotation around it
(`freelook.cpp:224-237`). **Two independent modules confirm the function's
signature and role**, which is much stronger than one.
Caveat: this is the *player's* turn path. Whether the camera reads the player's
rotation or gets a separate copy is a separate question (F-032).

**F-031 — Player rotation lives in a component reached indirectly. CONFIRMED (reference source, unverified offsets).**
`Actor::mActorRotationComponent = 0x238` is a *pointer* to a component whose first
`Vec2` is the rotation (`sdk/offsets/World.hpp:17`; `Actor::rotation()` at
`sdk/world/Actor.hpp:31-34` reads `field<Vec2>(component, 0)`). Freelook saves a
full 16 bytes from that component and restores it verbatim
(`freelook.cpp:126-132,163`) — suggesting the component holds more than yaw/pitch,
which matters if we ever write it directly.

**F-032 — A separate camera component exists, with its own orientation array. CONFIRMED (reference source).**
`CameraComponent::mOrientation = 0x28` holds 16 bytes that Freelook snapshots and
restores while the camera is overridden (`sdk/offsets/Render.hpp:142-144`;
`freelook.cpp:193-196,205-208,217-221`). It is written by the game's
`UpdatePlayerFromCamera` pass (`freelook.cpp:21,183-222`). Its exact semantic
(`{yaw, pitch, ?, ?}` vs. quaternion vs. something else) is **HYPOTHESIS** — the
reference never interprets it, only copies it.
**This is the field to investigate for V2, and a candidate for V1 if changing the
player rotation turns out not to move the view.**

**F-033 — Correctness of every offset/signature for 1.26.62 is UNKNOWN.**
The reference targets a `1.26.6X.X` band under a different owner/version line
(`levimod.json` v1.5.12). Our build is 1.26.62. `F-023`/`F-008` say plainly: a
band match is not evidence for a specific binary, and ~66% of that signature
table changes between adjacent builds. Nothing from the reference is used until
the investigation below proves it on our binary.

## 3. The known hard part: the game will fight back

Input on a mobile client is continuous — the look stick writes turn deltas every
frame. Setting the rotation once will be overwritten almost immediately. The
reference's own answer to this is a *session* pattern: while the module is active
it keeps restoring the captured rotation on every pre-tick and tick
(`freelook.cpp:126-141,224-237`; subscriptions at `freelook.cpp:353-359`).

So the feature is not "write two floats". It is:

1. **apply** the target orientation,
2. **hold** it against the input path for as long as it is engaged,
3. **release** cleanly (no residual override, no drift, no stuck rotation).

For V1 the natural hold point is the same function the reference uses: intercept
the turn delta and drive it toward the target, or suppress it while engaged. Which
of those is correct depends on how the game applies the delta — the investigation
below settles it.

## 4. What must be proven before any code (evidence gates)

| Gate | Question | Blocks |
|---|---|---|
| G-1 | Which function is `ApplyTurnDelta` on **our** binary, and is its signature unique? | everything |
| G-2 | What is `delta`'s semantic on our binary — per-frame rotation rate, absolute offset, or raw stick value? Sign of each axis? Units (degrees vs radians)? | target-driven hold math |
| G-3 | Where does the player's rotation live on our binary: accessor vs. pointer-to-component + component layout? | V1 apply |
| G-4 | Does the camera follow player rotation, or read its own component? What is `CameraComponent::mOrientation` for us? | V1 hold + all of V2 |
| G-5 | Which value is yaw, which is pitch, and what are the wrap/convention limits (−180..180 vs 0..360)? | cardinal presets correctness |
| G-6 | How long is the target function (HS-1: never patch wider than the function)? | hook safety |

G-5 matters more than it looks: a "face north" that is 180° off is worse than
nothing, and the reference never pins the convention down. It must come from
evidence, not from a guess about Minecraft's coordinate system.

## 5. Explicit risks

- **R-a** If the turn path is *not* where the view is set (e.g. the camera is
  derived per-frame from `InputComponent::cameraOrientation`, which Freelook also
  touches at `freelook.cpp:40-48,136-141`), V1 via turn-delta will not hold. The
  investigation covers this by checking the camera-read path too, so we learn it
  before writing the hook, not after.
- **R-b** Writing rotation directly (V1 alternative) risks desync between body,
  head and camera, and fights the tick loop. Prefer the delta path unless
  evidence favours the direct write.
- **R-c** Patch-width crash class (`F-026`): a short target function must not be
  hooked.
- **R-d** Mobile input is a continuous writer; a hold implementation that runs on
  the wrong thread or too late in the frame will visibly jitter.

## 6. Status

**Investigation `T-CAM1` (next document) is READY and is the gate for this
feature.** No code is written for it yet, by design: the only thing that can make
this feature safe is knowing, on our binary, which function receives the turn
delta and which field the camera reads — and that is exactly what the
investigation asks.
