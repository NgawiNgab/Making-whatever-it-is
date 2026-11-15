# BedrockTools reference

Every claim here is cited to a path and line in
[`NgawiNgab/BedrockTools`](https://github.com/NgawiNgab/BedrockTools). BedrockTools
targets the same game version, so it is the strongest available evidence for
1.26.62 — but it is **a reference, not proof**. Each row says how far the evidence
actually goes.

## How the mod reads the player's yaw

This is the chain that the first device run was missing entirely. My scaffold could
hook the turn function but had no way to reach the player, so it could never show a
number.

| Step | Mechanism | Reference | Status here |
|---|---|---|---|
| obtain `ClientInstance*` | hook the game's update fn; it arrives as argument 0 | `src/core/GameHooks.cpp:242-249` (`clientUpdateDetour`), signature at `src/core/memory/Signatures.cpp:39` (`ClientInstanceUpdate`) | pattern copied verbatim |
| `ClientInstance*` → `Player*` | call the local-player getter (signature) | `include/bedrocktools/sdk/client/ClientInstance.hpp:24-28` | pattern copied verbatim |
| `Player*+0x238` → rotation component | `Actor::rotationComponent()` | `include/bedrocktools/sdk/offsets/World.hpp:17`, `sdk/world/Actor.hpp:20` | offset copied |
| component `+0` / `+4` → pitch / yaw | `Vec2 {float x, float y}`, `rotation().x` = pitch, `.y` = yaw | `sdk/Types.hpp:7`, `sdk/world/Actor.hpp:31-34`, `src/modules/hud/compass.cpp:13-16` | deref chain copied |
| when to read | per-actor tick, **after** the original | `src/core/GameHooks.cpp:133-140` (`tickDetour`), signature `NormalTick` at `Signatures.cpp:18` | pattern copied verbatim |

The component pointer is a **pointer field**, not inline floats. Reading it inline
(as I first assumed) yields garbage; `Actor::rotation()` dereferences it first, which
is what the code now does.

### What "copied verbatim" does and does not mean

It means the byte patterns and the offsets are transcribed, not invented. It does not
mean they are verified **on this build**. Two consequences:

- If a pattern does not match exactly once on 1.26.62, its fact resolves to
  `MISSING`/`AMBIGUOUS` and the affected hook is simply **not installed**. The readout
  then names the failing fact instead of drawing a fake value.
- If a pattern matches but the offset is wrong, the readout can print plausible
  garbage. `yaw/pitch` is therefore labelled by the measurement itself: face a known
  direction, press **Mark reading**, compare. That comparison is the verification.

## How the HUD is drawn

My first version submitted draw commands from a frame pump but never registered a
`HudEditorElement`, and never registered the module with the launcher's callbacks.
The reference does both, every frame:

| Behaviour | Reference |
|---|---|
| rebuild the command list each frame and submit it | `src/modules/hud/armorhud.cpp:823`, `arrowcounter.cpp:109` |
| submit editor elements in the same frame | `armorhud.cpp:593-614`, called from `onFrame` |
| clear the overlay on disable | `armorhud.cpp:548-552` |
| position persisted as `hudPosX`/`hudPosY` | `src/modules/hud/compass.hpp:22-23` |
| text is ARGB with alpha in the high byte | `arrowcounter.cpp:104` (`0xFF000000U \| rgb`) |

## Frame clock

The preloader SDK exposes no frame callback. BedrockTools hooks `eglSwapBuffers`
(`src/core/GameHooks.cpp:283-292`) and publishes its own `FrameEvent`. This mod does
the same hook but calls the module directly; `libEGL.so` needs no game signature.

## Build

See [`BUILD.md`](BUILD.md). The critical detail, learned the hard way: a package
must appear in **both** `add_requires` and the target's `add_packages`. Only the
latter puts its include dirs on the compile line.
