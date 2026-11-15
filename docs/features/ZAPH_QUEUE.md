# Zaphkiel requests

I cannot run Zaphkiel — it needs your device. This file is the exact text to paste
into `zInput.txt`.

Target: **libminecraftpe.so 1.26.62.1, arm64-v8a.**

---

## Where we are

| Round | Asked for | Result |
|---|---|---|
| 1 | `findclass<GameMode>`, `vfc`, `fullvf` | Found the function and its signature. No vtable — `GameMode` is abstract. |
| 2 | `h<GameMode>`, `string:`, `findclass<sendTryDestroyBlock>` | Nothing new. Same two typeinfo addresses as round 1, just a different ASLR base (`0xbad2000`). |

Two of round 2's three lines were my mistakes, and I should have known better:

- **`h<GameMode>` needs a header.** `h<>` parses a file from `headers/`; the docs say
  so. I never supplied one, so "no struct found" was guaranteed.
- **`string:` is not a documented command.** I half-remembered the README's formatting
  and wrote it as if it resolved anything. The output's `String not found` is about
  right.

The third line did answer something useful: there is **no function symbol** for
`_sendTryDestroyBlock` itself. Only the lambda's typeinfo exists. So there is no name
to look up — which is why a string xref was never going to work.

## What we know for certain

```mangled: ZN8GameMode20_sendTryDestroyBlockERK8BlockPoshE
  GameMode::_sendTryDestroyBlock(BlockPos const&, unsigned char)
```

One call site: the lambda at `ZN8GameMode20_sendTryDestroyBlockERK8BlockPoshE3$_0`,
which takes no captures and returns `bool(optional<ItemStack>&)`. So the call passes
a local stack `BlockPos` and a one-byte face.

---

## Request 3 — the signature (paste this)

```
sig<FF 83 02 D1 FD 7B 04 A9 F5 7B 05 A9 F4 4F 06 A9 FD 03 01 91>
sig<FF 43 02 D1 FD 7B 04 A9 F5 7B 05 A9 F4 4F 06 A9 FD 03 01 91>
sig<FF 43 03 D1 FD 7B 04 A9 F5 7B 05 A9 F4 4F 06 A9 FD 03 01 91>
sig<FF 83 03 D1 FD 7B 04 A9 F5 7B 05 A9 F4 4F 06 A9 FD 03 01 91>
```

Paste all four. I would rather have four cheap scans than guess at one stack frame size.

**Why these.** From the lambda's signature the function takes 3 arguments, so it saves
`x19-x22` and `x29/x30` (5 register pairs) — that is the `FD 7B 04 A9 F5 7B 05 A9 F4 4F
06 A9` sequence, which is very distinctive. The differences between the four lines are
the stack allocation and the frame-pointer offset, which depend on how many bytes of
locals the function needs. Whichever line returns `matches: 1` is the function.

**What I am looking for in `zaphkiel_out.txt`:** `matches: 1` and a `func @ 0x...`
address. The `fn=` line also gives me the parameters, which cross-checks the mangling.

**Why this instead of a vtable slot or a string xref:** the first round showed
Zaphkiel's `sig<>` works reliably — all seven signatures it generated came back
`matches: 1`. This is the same mechanism. A vtable slot cannot work (no vtable), and
a string xref cannot work (no function symbol).

**If all four return `matches: 0`:** add these two, which vary the register cutoff in
case the compiler saved fewer pairs:

```
sig<FD 7B 04 A9 F5 7B 05 A9 F4 4F 06 A9 FD 03 01 91 . . 5A D0 3B D5>
sig<FD 7B 04 A9 F5 7B 05 A9 F4 4F 06 A9 FD 03 01 91>
```

`. .` is a two-byte wildcard here — if that syntax is rejected, drop that line and
send the plain one only.

### Optional, only if you want the inventory work too

```
entt<ActorEquipmentComponent>
sig<FF 43 01 D1 FD 7B 02 A9 F6 57 03 A9 F4 4F 04 A9 FD 03 00 91 . . . . 5A D0 3B D5>
```

The `entt<>` line has worked before (the docs have a verified example), and equipment
is what decides whether the bot holds a pickaxe — the difference between mining iron
ore and mining air.

---

## Honest limits

Even with the address in hand, I cannot confirm the call works until it runs on your
device. What I can do is write it so it fails loudly and safely: resolve the signature
at load, log the address, refuse to hook if it does not resolve, and unhook cleanly.
Then your log tells us whether it worked, instead of the game dying.

I have been wrong twice in this file already. Both times the fix was to stop guessing
at tool syntax and use the mechanism that has actually produced results — `sig<>`.
