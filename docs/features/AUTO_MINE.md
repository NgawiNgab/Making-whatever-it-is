# Feature: autonomous mining

**Goal.** Select a block type (sugarcane, iron ore, anything). The bot finds the
nearest instance, paths to it, mines it, and repeats. Hands-off.

Status: **nothing has been run in Minecraft.** The commands are wired, the aim and
raycast maths are written, and the bot's decisions are tested against a fake world.
The step that actually breaks a block is still missing: it needs one vtable slot
from Zaphkiel (`ZAPH_QUEUE.md`, Request 1). Until that lands this will walk to a
block and stop.

## What is wired (compiles, not run in game)

- `BaritoneRuntime::startMineFromText(blocks, quantity)` parses user input:
  `sugarcane`, `iron_ore`, `64 iron_ore`, `iron_ore,diamond_ore`,
  `minecraft:iron_ore`. A leading number is the quantity; 0 means "keep going".
- `BaritoneRuntime::startTunnelFromText(spec)` parses `64`, `64 north`,
  `north 64`, or nothing (= the way you are facing).
- Requests are queued and applied at the top of the next tick, **after** the world
  snapshot is published. `MineProcess::mine()` scans the snapshot when it starts,
  so applying it before the snapshot exists would scan nothing.
- Mod menu: `mine_blocks`, `mine_quantity`, `mine`, `tunnel_spec`, `tunnel`, and
  the existing `test_stop`.
- `TunnelProcess` (new): a 1 wide, 2 high corridor. Movement alone only clears the
  block at feet level, so the head block is broken by the process itself.

### Bugs found and fixed while doing this

1. **Mined blocks stayed in the snapshot.** `WorldTracker` cached block state ids
   keyed on the `Block*` that `BlockSource::getBlock` returned. That pointer can be
   reused for different blocks, so a mined ore stayed registered as ore forever and
   the bot would path back to a hole. Now keyed by block *name*, in
   `platform/WorldTrackerScan.{h,cpp}`, with a test.
2. **The camera pitch was never written.** `InputOverride::writeRotation` sets
   `lookDelta.x = 0` unconditionally, so only yaw was applied. Aiming at a block
   needs pitch; `platform/MineAim.h` now produces both axes.
3. **Raycast reported the exit face.** The first version returned the far side of
   the block, which would have put clicks on the wrong face. Fixed and tested; the
   `+Z` face, exact 0.5-block distance, and nearest-block occlusion are all
   asserted now.

## What still does not work — the blocker

**Breaking a block does nothing in the real game.**

The core decides to break (`Input::ClickLeft`), the tests confirm it decides. But
in the game that bit goes nowhere. Files and line numbers:

| Link | State | Evidence |
|---|---|---|
| processes set `Input::ClickLeft` | works | `MineProcess.cpp:362`, `Movement.cpp:508` |
| `InputOverride::apply` copies movement bits | works | `InputOverride.cpp:241-247` |
| `InputOverride::apply` copies `ClickLeft` | **absent** | only Forward/Back/Left/Right/Jump/Sneak/Sprint |
| `PlayerContext::startOrContinueBreaking` | **stub, `return false`** | `GamePlayerContext.cpp:193` |
| `stopBreaking` / `useItemOn` / `swingArm` | **empty** | `GamePlayerContext.cpp:197-201` |

So the bot will walk to the block, look at it, and stand there. That is exactly
what the current build does.

Note the harness result: `PlayerContext::startOrContinueBreaking calls: 0`. Break
requests go through `Input::ClickLeft`, **not** through `PlayerContext`. So
implementing those stubs would not fix it — the missing piece is turning
`ClickLeft` into an actual attack/break on the game side.

### Closing that gap needs a signature

There is no break/destroy signature in `memory/Signatures.h` (`ActorSetRotation`,
`InputHandler_tick`, `InputEventQueue_enqueueDirection`,
`ClientInputUpdateSystemInternal_tick`, three `BlockSource*`). BedrockTools hooks
its game-mode destroy function; this build has nothing equivalent recorded.

**So this needs a Zaphkiel run to find the destroy-block function for
1.26.62.1.** That is the one thing blocking real mining.

## Second gap: inventory is stubbed

Mining runs on pickaxes and `switchToBestToolFor` cannot select one:

```
GamePlayerContext::selectedSlot()  -> 0
GamePlayerContext::selectSlot(int) -> {}
GamePlayerContext::hotbar(int)     -> {}
GamePlayerContext::countItem(name) -> 0
```

Effects: no tool switching; `warnIfNoPickaxe` can never fire; and mining iron ore
by hand drops **nothing**, so the bot would collect air. `entities()` returning
`{}` also means no drop pickup.

Unlike the break hook, this needs **no new signature**: the components are already
mapped in `sdk/Components.h` (`RuntimeIDComponent`, `ActorOwnerComponent`,
`ActorGameTypeComponent`, and camera components). It needs someone to walk the
player's `PlayerInventory`/container data, which is ordinary component reading.

## Build order from here

| Stage | Work | Blocked on |
|---|---|---|
| A | **Break hook.** Find the destroy-block function for 1.26.62.1, hook it, apply `ClickLeft` | Zaphkiel signature |
| B | **Inventory read.** `selectedSlot`/`hotbar`/`countItem`/`selectSlot`; dropped-item `entities()` | Nothing known |
| C | ~~`#mine` command~~ | **Done** |
| D | Unloaded-chunk targets: `searchWorld` over the long-distance cache | Nothing known |

A alone turns "walks up and stares" into "walks up and mines". B makes what it
mines actually worth something.

## Risks

- **Automated mining is what anti-cheat looks for.** This is a personal
  single-player tool; do not use it on servers.
- **Per-block drop rules.** Sugarcane drops from the base break, iron ore needs a
  stone-tier pickaxe. `BlockFilter::drops` models this; stage B makes it real.
- **`exploreForBlocks` is on by default.** With no matching block in range, the bot
  goes to Y=-59 and branch-mines rather than giving up. On a phone that looks like
  the bot wandering off. Set `exploreForBlocks` off if the user only wants "mine
  what I can see".

## Open questions

1. Stage A needs a signature run — write the Zaphkiel request now, or do B first?
2. Which blocks to verify on device first? Iron ore is the honest test (fails loudly
   without a pickaxe); sugarcane is easiest to see.
3. The vendored `reference/` copy is read material. This feature edits it in place.
   If the user wants the mod layer to stay pristine, the `#mine` changes should move
   into a separate `src/` tree that calls `baritone::Baritone` as a library.
