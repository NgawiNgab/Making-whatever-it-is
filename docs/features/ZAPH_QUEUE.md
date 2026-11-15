# Zaphkiel requests

I cannot run Zaphkiel — it needs your device. This file is the exact text to paste
into `zInput.txt`, plus what I need back and what I will do with it.

Target: **libminecraftpe.so 1.26.62.1, arm64-v8a.**

Paste the block into
`/storage/emulated/0/Android/media/org.levimc.launcher/kurumi/zInput.txt`, run
Zaphkiel once, and give me back `zaphkiel_out.txt` (and `zaphkiel_out.c` if it
appears).

> **Round 1 is done.** It found the function and its exact signature, but not its
> address — see `ZAPH_FINDINGS.md`. Round 2 below is what closes that.

---

## Request 2 — the address of the destroy call (the remaining blocker)

Round 1 confirmed the function is
`GameMode::_sendTryDestroyBlock(BlockPos const&, unsigned char)`. It is non-virtual,
and `GameMode` has no vtable, so it cannot be found by slot. It can be found through
the lambda that lives inside it, whose typeinfo string we already have:

```
h<GameMode>
string: "ZN8GameMode20_sendTryDestroyBlockERK8BlockPoshE3$_0"
findclass<sendTryDestroyBlock>
```

In that order — the first two are the ones that matter.

**What I need back:** `zaphkiel_out.txt`.

**What I am looking for in it:** a string xref to
`ZN8GameMode20_sendTryDestroyBlockERK8BlockPoshE3$_0`. Because that symbol names an
anonymous-namespace lambda, the only places it can appear in `.data` are the
function pointer of the `std::function` built from it, or its typeinfo. Either one
lands within a few instructions of the code that calls `_sendTryDestroyBlock`, and
`strfn<...>` decompiles the enclosing function for me.

**How I will use it:** I will not call `_sendTryDestroyBlock` directly. I will hook
`GameMode`'s block-break path and drive it with the arguments my aim code already
computes — `BlockPos` from the raycast, face from the raycast normal. Both are
written and tested (`platform/AimRay.h`, `platform/MineAim.h`).

**Fallback if the xref finds nothing:** `fullvf<...>` on a concrete game-mode
subclass would give the virtual `startDestroyBlock` wrapper instead. I cannot name
that subclass — `findclass<GameMode>` returned 99 matches and none was a concrete
subclass. If `findclass<ClientGameMode>` or `findclass<ServerGameMode>` returns
something in round 2, that is the one.

### Round 1, kept for the record

```
findclass<GameMode>
findclass<ClientPlayer>
findclass<Player>
vfc<GameMode>
fullvf<GameMode>
```

Result: `GameMode` resolved to typeinfo only, vtable not found. Productive anyway —
that is how `_sendTryDestroyBlock` was identified.

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
| Break hook | **blocked on Request 2** (address of `_sendTryDestroyBlock`) |
| Inventory reads | blocked on Request 3 (or a different approach) |

## Honest limits of what I can check

Everything above that is "tested" was tested against a hand-built world and a fake
player — see `AUTO_MINE.md`. I have never run this game. When you send the
Zaphkiel output I can write the hook and it will compile, but whether the
vtable slot is right, whether the face convention matches, and whether the click
lands has to be confirmed on your device from the log.
