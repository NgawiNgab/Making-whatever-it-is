# Specification

**Product:** native feature mod for Minecraft Bedrock on Android.
**Target:** `arm64-v8a`, MCPE **1.26.62**, LeviLauncher + preloader.
**Status of this document:** requirements. Feasibility of each item is a
separate, evidence-gated question (see `docs/IMPLEMENTATION_PLAN.md`).

Decisions recorded 2026-11-15: direction = **feature mod**; build = **1.26.62
arm64** (same build the user's other projects target).

## 1. What "feature mod" means here

A mod that adds in-game functionality to the player's experience — HUD overlays,
visual changes, player conveniences — delivered as a `.levipack` and toggled from
the LeviLauncher mod menu. Not a toolkit (that is what the internal layer is
*for*), not a single-purpose tool.

The reference's feature architecture is worth keeping: modules with lifecycle +
config, a registry, an event bus, a menu, and a per-frame draw path
(`docs/REFERENCE_FINDINGS.md` F-003). What we do not keep is its version
handling (§F-007..F-014).

## 2. Feature tiers — grouped by what they cost in binary evidence

The single most useful structural fact discovered: the launcher/preloader surface
is large enough that a meaningful feature set needs **no game-binary knowledge at
all**. Tiers exist so we can ship value before T-P1, and so each feature declares
which kinds of fact it depends on.

### Tier 0 — launcher-only ("no game facts")

Everything here runs on preloader APIs plus the host process itself.

| Capability | API | Notes |
|---|---|---|
| FPS / frame-time / clock HUD | `pl::modmenu::submitDrawCommands` | Frame timing measured at our own `eglSwapBuffers` detour |
| Text, rects, lines, circles, triangles, images | `DrawCommand` + `DrawCommandType` | `pl/ModMenu.hpp:179-191` |
| Custom fonts | `registerFont(fontId, path)` | Font shipped in the `.levipack` (`resources/minecraft.ttf`) |
| PNG/WebP/SVG images | `registerImage` | e.g. item icons, player heads |
| HUD layout editor, snapping, per-element position keys | `submitHudEditorElements` + `HudEditorElement` | Launcher-rendered editor |
| Module toggles + typed config UI (toggle/slider/radio/color/keybind/text/button) | `registerModule(ModuleInfo)`, `ConfigEntry` | Menu is launcher-owned |
| On-screen touch buttons | `registerButton(ButtonInfo)`, `ButtonBehavior` | Click/Hold/Toggle |
| Touch / key / mouse / text input | `pl::input::register*Callback` | Process-wide callbacks |

**Binary evidence required: none.** `eglSwapBuffers` is resolved by `dlsym` on
system `libEGL.so`, not from the game binary. Determinism risk here is
essentially zero.

### Tier 1 — game state, read-only

Requires resolved accessors and **validated** field reads (D1/D2/D3,
`docs/CROSS_VERSION.md` §2, §5). Examples: player position, rotation, speed,
health/armour, held item, dimension, world time/weather, in-game ping.
Each value must be reached through a resolved getter or a layout-validated read;
no bare offset constants.

### Tier 2 — game behaviour / render / packet

Requires hooks with the safety rules in §5. Examples: visual modifications,
packet-driven HUD data (chat/ping/scoreboard), interaction tweaks.

## 3. Functional requirements

**FR-1 Lifecycle.** The mod loads and unloads cleanly through
`PL_REGISTER_MOD` / `ModContext`; `disable` leaves no hook installed and no
draw command submitted. Unload must be safe even mid-frame.

**FR-2 Observability.** The mod always exposes, per feature: enabled state,
whether its required facts resolved, and a human-readable reason when disabled.
A `resolution-report.json` is written on every install attempt (R-1..R-5).

**FR-3 Fail closed, visibly.** A feature whose facts are missing is skipped and
reported; it must never dereference a null/unvalidated pointer, and must never
silently render nothing while claiming to be enabled.

**FR-4 Config is persistent and launcher-visible.** Every user-facing knob is a
`ConfigEntry` on the launcher menu, stored as JSON in `ModContext::configDir()`.
Config changes take effect without a restart.

**FR-5 No crash on unsupported build.** If the mod is loaded against a build with
no matching data file, it runs in "unsupported" mode: Tier-0 features work, every
game-dependent feature is disabled with a stated reason, nothing is hooked.

**FR-6 Frame budget.** Per-frame work is measured and kept off the render thread
where possible; the frame pump reports its own cost. A slow feature self-reports
rather than silently dropping frames.

## 4. Non-functional requirements

- **NFR-1** C++20, no RTTI in the mod itself (`-fno-rtti`, matching the
  reference's convention), no exceptions across the game boundary
  (`pl/xmake` flags; see reference `xmake.lua:33`).
- **NFR-2** No version-specific literal in `src/` — enforced by review and by a
  grep-based check in CI (`docs/CROSS_VERSION.md` D-10).
- **NFR-3** All game-interacting code sits behind one resolution layer; features
  never resolve signatures themselves.
- **NFR-4** Host-side unit tests for everything that does not need a device
  (config, registry, fact tables, report serialisation). Device-only behaviour is
  explicitly marked untested rather than claimed.
- **NFR-5** Builds via `xmake` with NDK r28c; CI builds on push.

## 5. Hook safety requirements (earned from a real crash)

The user's own crash evidence on this exact build (`Seed-Taker`
`evidence/CRASH-001`) is a SIGILL caused by an inline patch wider than the
function it targeted — the patch destroyed the *adjacent* function, which was
called later and was load-bearing. Rules that follow, and that we adopt:

- **HS-1** Never hook a function shorter than the patch the hook engine writes
  (the evidence reports a ≥12-byte inline patch against an 8-byte function).
  Measure the target's length from disassembly *before* hooking.
- **HS-2** Prefer hooking a function whose body is long and stable, or one that
  is called from a known call site, over hooking a short leaf getter.
- **HS-3** After installing, verify the target's bytes are what the disassembly
  said, and log the patched extent.
- **HS-4** A hook that cannot be verified is not installed (R-2 applies to hooks,
  not just signatures).

## 6. Out of scope

- Multiplayer advantage features (aim assistance, world/module automation that
  grants gameplay advantage) — not part of this spec and not to be built.
- Anything requiring a build we have no data file for.
- Server-side behaviour of any kind.
- Publishing, releases, or PR merges without explicit approval.

## 7. Open product question

Which Tier-0 features belong in the first release? They are free (no binary
evidence), so the choice is purely what the user wants to see on screen first. The
skeleton is designed to make adding a Tier-0 feature a ~1-file change.
