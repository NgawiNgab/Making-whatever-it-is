# Zaphkiel requests

I cannot run Zaphkiel — it needs your device. This file is the exact text to paste
into `zInput.txt`, plus what I need back and what I will do with it.

Target: **libminecraftpe.so 1.26.62.1, arm64-v8a.**

Paste the block below into
`/storage/emulated/0/Android/media/org.levimc.launcher/kurumi/zInput.txt`, run
Zaphkiel once, and give me back the files listed under each request.

---

## Request 1 — the break hook (this is the blocker)

Without it the bot walks to a block and stands there, because nothing turns
"break this" into a game call. Everything else for mining is written.

```
findclass<GameMode>
findclass<ClientPlayer>
findclass<Player>
vfc<GameMode>
fullvf<GameMode>
```

Optional, if `findclass<GameMode>` returns nothing useful:

```
findclass<GameModeBase>
vfc<GameModeBase>
fullvf<GameModeBase>
```

**What I need back:** `zaphkiel_out.txt` (or `zaphkiel_out.c` if `vfc`/`fullvf`
wrote pseudo-C) — specifically the vtable slot list for the game mode class.

**How I will use it:** Bedrock's destroy path lives on the game mode object, and
its signature is `startDestroyBlock(BlockPos const&, unsigned char face, bool&)`.
I will find that slot in the dump, hook it with `pl::memory::hook`, and drive it
from the aim code I already wrote (`platform/MineAim.h`, `platform/AimRay.h`).
The face and the hit point come from the raycast; those two pieces are already
written and tested.

**Why a vtable hook and not a byte signature:** `BlockSourceGetBlock` in
`memory/Signatures.h` is a 20-instruction pattern, which is fragile. A vtable slot
is an index, and I can resolve the class from RTTI at runtime — no pattern to break
when the game updates.

---

## Request 2 — confirming the player object (nice to have)

```
findclass<LocalPlayer>
hier<LocalPlayer>
struct<Actor><0x238>
```

**What I need back:** `struct` output — which functions read `Actor+0x238`.

**How I will use it:** the reference already writes player rotation through
`ActorRotationComponent` (`0x238` on the actor, confirmed two ways). This checks
whether the break call needs an `Actor*` or an `EntityContext`, which decides
whether my hook detours `GameMode` or wraps it.

---

## Request 3 — inventory (after 1, if you want mining to be useful)

```
entt<ActorEquipmentComponent>
entt<PlayerInventoryComponent>
findclass<Inventory>
vfc<Inventory><0,20>
```

**What I need back:** the decompile of the `get_at` slot for each component, and
the vtable range for `Inventory`.

**How I will use it:** `GamePlayerContext::selectedSlot/hotbar/countItem/selectSlot`
are all stubs returning 0 or nothing. Reading the equipment component is enough to
pick the right pickaxe, which is the difference between mining iron ore and mining
nothing (iron ore needs a stone pickaxe or it drops air).

---

## What is already done and does not need you

| Piece | State |
|---|---|
| `#mine` command → `MineProcess` | written, tested |
| Block search → path → break decisions | tested (fake world) |
| Chunk scan bug where mined blocks lingered | fixed, tested |
| Double-resolution aim maths | written, tested |
| Crosshair raycast (face + hit point) | written, tested |
| `#tunnel <length> [dir]`, 1 wide × 2 high | written, tested |
| Break hook | **blocked on Request 1** |
| Inventory reads | blocked on Request 3 (or a different approach) |

## Honest limits of what I can check

Everything above that is "tested" was tested against a hand-built world and a fake
player — see `AUTO_MINE.md`. I have never run this game. When you send the
Zaphkiel output I can write the hook and it will compile, but whether the
vtable slot is right, whether the face convention matches, and whether the click
lands has to be confirmed on your device from the log.
