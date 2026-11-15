# Zaphkiel requests

I cannot run Zaphkiel — it needs your device. This file is the exact text to paste
into `zInput.txt`.

Target: **libminecraftpe.so 1.26.62.1, arm64-v8a.**

---

## Where we are

| Round | Asked for | Result |
|---|---|---|
| 1 | `findclass<GameMode>`, `vfc`, `fullvf` | Found the function and its signature. No vtable — `GameMode` is abstract. |
| 2 | `h<GameMode>`, `string:`, `findclass<sendTryDestroyBlock>` | Nothing new. And no function symbol exists: `_sendTryDestroyBlock` was inlined. |
| 3 | four prologue signatures | **All four `matches: 0`.** My bytes were wrong. |

### Why round 3 failed, precisely

I wrote those four patterns out instead of reading them from the binary. Decoding
what I sent against a real, verified signature from the same image:

```
MY GUESS                REAL (BlockSourceGetBlock)
sub sp, sp, #0xa0       sub sp, sp, #0x40
0xA9047BFD              0xA9017BFD     stp x29, x30, [sp, #0x10]
0xA9057BF5              0xA90257F6     stp x22, x23, [sp, #0x20]
0xA9064FF4              0xA9034FF3     stp x20, x21, [sp, #0x30]
0x910103FD              0x910043FD     add x29, sp, #0x10
```

Every stack offset I used is exactly 2× the real one, and `F5 7B` saves `x21,x22` —
a pair neither verified signature saves. I invented a prologue and presented it as
if it came from the binary. Four scans, zero information. That is three rounds
wasted on my guesses, so this file changes approach below.

## What is actually established, and it is enough to matter

```
mangled: ZN8GameMode20_sendTryDestroyBlockERK8BlockPoshE
  GameMode::_sendTryDestroyBlock(BlockPos const&, unsigned char)
```

- Non-virtual, `GameMode` abstract with no vtable → must be called by address.
- Inlined into its sole caller → no function symbol to find.
- The caller builds a `std::function<bool(optional<ItemStack>&)>` from a captureless
  lambda, so that lambda's code address is stored in `.data`. The call to
  `_sendTryDestroyBlock` is in the function that constructs it.

That last point is a concrete, searchable structure — not a guess.

---

## Request 4 — one structural search

Start with option A. If it errors, use option B.

### Option A

```
sig<9F 3D 03 D5 . . . . . . 9F 3D 03 D5>
sig<3F 3D 03 D5 . . . . . . 3F 3D 03 D5>
```

### Option B (if `.` is rejected, use `??`)

```
sig<9F 3D 03 D5 ?? ?? ?? ?? ?? ?? 9F 3D 03 D5>
sig<3F 3D 03 D5 ?? ?? ?? ?? ?? ?? 3F 3D 03 D5>
```

**Why these bytes are real, unlike last time.** `9F 3D 03 D5` is `nop` and
`3F 3D 03 D5` is `bti c`. Both appear verbatim, already decoded, in the
`zaphkiel_out.c` pseudo-C from the very first run — in the epilogue of a real
function. They did not come out of my head.

**What I expect.** Clang pads each outlined `std::function` thunk with `nop`s or a
`bti c` and the standard 12-byte outline header. Two such thunks back to back are
8-16 bytes apart, so the gap between two markers is small and the pair is rare.
With a handful of matches I can identify the one whose caller builds a
`bool(optional<ItemStack>&)` function — which is the caller of
`_sendTryDestroyBlock`.

**What I need back:** `zaphkiel_out.txt`. Even `matches: 3` with addresses is
useful; the dump prints each match address.

### If that also comes back empty

Then I am out of structural guesses that do not rest on my own assumptions, and the
honest thing is to ask you for a targeted dump instead:

```
xref_tree<0x72dac733f0>
```

using the **live** lambda typeinfo address from round 2 (re-read it first — it moves
every launch; `findclass<sendTryDestroyBlock>` prints it).

That asks Zaphkiel to walk callers, which is the one job I cannot approximate from a
signature. It is a single line and it does not depend on any guess of mine.

---

## Meanwhile: the code is ready and fails safe

`platform/BlockBreaker.{h,cpp}` is written and tested:

- `resolve(signature, module)` — device path, resolves at load.
- `resolveAbsolute(address, imageStart, range)` — fallback for a hand-measured
  address. Refuses anything outside the module's executable range, and logs a
  warning every time it is used, because it breaks on any game update.
- `call(x, y, z, face)` — refuses when unresolved, and counts refusals so the log
  says why instead of the game dying.

Twelve tests cover the refusal paths. They assert that a null address, an address
past the end of `.text`, and an address below the image are all rejected — so if
this ever ships with a wrong number, it does nothing rather than crash.

If you measure the address yourself in IDA, send it and I will wire it through
`resolveAbsolute` tonight; the offset from image base is all I need.

---

## Track record, so you can weigh my guesses

| Round | Who was wrong | What |
|---|---|---|
| 1 | me | Proposed a vtable slot; `GameMode` has no vtable. |
| 2 | me | `h<GameMode>` needs a header I never supplied; `string:` is not a command. |
| 3 | me | Invented a prologue, down to the register pairs. |

What has actually produced results: `findclass<>` (round 1, gave the signature) and
the real bytes inside your own reference source. Everything else has been me
guessing at tool syntax.
