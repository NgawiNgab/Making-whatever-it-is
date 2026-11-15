# What the Zaphkiel dump confirmed

Run against `libminecraftpe.so` 1.26.62.1 (342,231,392 bytes, 7 segments).
Files received: `zaphkiel_out.txt`, `zaphkiel_out-2.txt`, `zaphkiel_out.c`,
`rtti_dump.txt`, `dynsym_dump.txt`.

## The important part: the destroy-block function exists and is what I predicted

`findclass<GameMode>` surfaced a lambda whose mangled name spells out its enclosing
function:

```
ZN8GameMode20_sendTryDestroyBlockERK8BlockPoshE3$_0
  8GameMode               -> class GameMode
  20_sendTryDestroyBlock  -> method _sendTryDestroyBlock
  RK8BlockPos             -> BlockPos const&
  h                       -> unsigned char            <- the face
```

So the function is **`GameMode::_sendTryDestroyBlock(BlockPos const&, unsigned char)`**
and it is the client-side call that asks the game to destroy a block. That is
exactly the one link that was missing. It is not a guess: the parameter list came
out of the mangled name.

Note the leading underscore — `_sendTryDestroyBlock`, not `tryDestroyBlock`. Related
names in the same run: `GameModeExt::CommonGameModeMessenger::createBlockBreakCaptureScope`,
`IGameModeMessenger`, `IGameModeTimer`. That cluster is Bedrock's block-break
bookkeeping, and it confirms we are looking at the right subsystem.

## What the dump could not give us, and why

`GameMode` has **typeinfo but no vtable**:

```
VFC (pseudo-C): GameMode
  vtable not found for "GameMode"
  reason: typeinfo string exists, but no concrete vtable could be resolved -
          likely a pure interface / no _ZTV emitted for this type
```

`_sendTryDestroyBlock` is non-virtual, so it does not appear in any vtable. Both
facts point the same way: **the way to reach it is by address, not by slot.** More
precisely, by way of the lambda, whose code address lives in GameMode's `.data`.

So this run gives me the *name and signature*, and the next run gives me the
*address*. That is the whole gap.

## Also confirmed

- **`LocalPlayer` inherits `Player` inherits `Mob` inherits `Actor`** (`hier`). So the
  existing `offsets::Actor_*` constants apply to the local player unchanged, and
  `ActorRotationComponent` at `+0x238` is still valid. No change needed.
- The module loads at **two executable ranges**: `0x0-0x13686ff0` and
  `0x14318000-0x14319000`. Addresses above the image (e.g. the typeinfo at
  `0x72e6744790`) are **live runtime VAs** and must not be hardcoded; everything has
  to be resolved through `pl::memory::resolveSignature` at load time.

## One suspicious result

`zaphkiel_out.c` contains a pseudo-C dump of `LevelRendererPlayer::slot_16` and
`slot_17`. Neither `LevelRendererPlayer` nor `vfc`/`fullvf` for it appears in either
`zaphkiel_out.txt` or `zaphkiel_out-2.txt`, and both requested-class dumps failed.
Most likely it is left over from an earlier run. **I am not using it** — it is not
part of this analysis and I have no way to attribute it to this run.

## What this changes in the code

Nothing yet — the hook cannot be written without the address. But it does remove the
guesswork: no scanning for a byte pattern, no hunting a vtable slot. Find the lambda
at `GameMode::_sendTryDestroyBlock(BlockPos const&, unsigned char)::$_0`, read the
function pointer stored next to it, call it.

Once that lands, `platform/MineAim.h` and `platform/AimRay.h` supply the arguments:
block position from the raycast, face from the raycast's normal. Both are written
and tested; see `AUTO_MINE.md`.

---

## Round 2 (same request, second run): nothing new

`findclass<sendTryDestroyBlock>` returned the same two typeinfo addresses as round 1,
just under a different ASLR base:

| | round 1 | round 2 |
|---|---|---|
| outer `std::function` typeinfo | `0x72e67453c0` | `0x72dac733c0` |
| lambda typeinfo | `0x72e67453f0` | `0x72dac733f0` |

Same 0x30 spacing, identical low 12 bits, base shifted by `0xbad2000`. So: same
static object, as expected. **No function symbol for `_sendTryDestroyBlock` exists** —
it was inlined into its caller, which is routine for a function used at one call site.

Two lines of that request were wasted on my mistakes:
`h<GameMode>` needs a header file in `headers/` (there is none), and `string:` is not
a documented command. Both were avoidable.

**Conclusion: stop looking for this function by name.** Go by the prologue its
signature implies. Request 3 in `ZAPH_QUEUE.md`.
