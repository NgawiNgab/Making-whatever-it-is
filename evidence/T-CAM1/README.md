# T-CAM1 evidence

- Date: 2026-11-15. Provided by: user (Zaphkiel run on-device), pasted in chat.
- Target: MCPE **1.26.62**, arm64.
  `lib: /data/data/org.levimc.launcher/files/minecraft_libs/com.mojang.minecraftpe/arm64/libminecraftpe.so`
  `loaded 342231392 bytes`, **7 segments** — byte-for-byte the same header as the
  user's earlier 1.26.62 evidence (`342,231,392 B`, 7 segments, text
  `0x0-0x13686ff0`). **Library image: INFERRED same instance** (size + segment
  table match; a hash would make it CONFIRMED — see below).
- Run config: `dump_rtti=true`, `dump_dynsym=true`, `separate_dump_files=true`,
  `max_fn_insns=2000`.
- `zInput.txt` (11 targets): 5 recon (`findclass<LocalPlayer>`,
  `findclass<Camera>`, `findclass<CameraComponent>`, `hier<LocalPlayer>`,
  `hier<Camera>`) + 6 reference patterns via `sig<>`.
- Files: `zaphkiel_out.txt` (2663 lines), `rtti_dump.txt` (2001 lines),
  `dynsym_dump.txt` (38 lines) — verbatim copies.
- sha256: `zaphkiel_out` `5b16839c343a1a605037defad38799bf035c5bbe3890f52df4aad899282b3c14`,
  `rtti_dump` `10eb5608d3dc2c351bf13fe4e9f720bb6c511485fd831ea68d2196204893b85f`,
  `dynsym_dump` `119b0b1db32c52839e55d3feb1b3bc9d477837129cf7954332b8ffead10dbdb3`.

**Address space note (important).** The `sig<>`/`fn<>` addresses in
`zaphkiel_out.txt` are **file offsets / RVAs** (e.g. `0xaf60090`, `0xfdaff28`),
already inside the file-image space of segment 0 (`va=0x0, sz=0x13686ff0`).
The `findclass`/`rtti` addresses are **live ASLR-slid VAs** (`0x709a…`). Do not
mix the two. RVAs are stable across runs for a given image; VAs are not.

## `sig<>` results — the actual finding

| # | Reference ID | Pattern | Matches | Address (RVA) | Verdict |
|---|---|---|---|---|---|
| 1 | `LocalPlayerApplyTurnDelta` | 56 B (23 fixed) | **1** | `0xaf60090` | **VALID — real identity** |
| 2 | `ActorGetHeadRotation` | 168 B (78 fixed) | **1** | match `0xfdb02a8`, inside fn `0xfdaff28` (+0x380) | **REJECTED — mid-function** |
| 3 | `ActorSetHeadRotation` | 192 B (84 fixed) | **1** | match `0xfdb01e8`, inside fn `0xfdaff28` (+0x2c0) | **REJECTED — mid-function** |
| 4 | `UpdatePlayerFromCamera` | 36 B (12 fixed) | **1** | `0x8a42820` | Real function (1152 B, dense, no branch-to-mid) — **identity UNCONFIRMED** |
| 5 | `GetPerspective` | 48 B (24 fixed) | **1** | `0x8a3e9fc` | **REJECTED — 20-instr glue sequence** |
| 6 | `ClientInstanceGetLocalPlayer` | 116 B (42 fixed) | **1** | `0x9b7f6d0` | **INFERRED** (see F-CAM-08) |

Conclusion: **the reference's pattern table is not portable as a set.** Four of six
patterns matched "uniquely" yet three are not function identities at all — a
false-unique. Only pattern #1 survived every check.

## Structural verification of #1 (`0xaf60090`)

- **Extent:** `0xaf60090`–`0xaf60610`, **1408 bytes (176 instructions)**, single
  `ret` at `0xaf60610`. Not a thunk, not a tail fragment.
- **Canary:** `mrs x22, tpidr_el0` → `ldr x8, [x22, #0x28]` prologue, matching
  epilogue `cmp/b.ne 0xaf6066c` + `__stack_chk_fail`-style call. Genuine function.
- **Arg handling:** `mov x19, x0` (this), `mov x20, x1` (delta ptr) — the
  reference's `(Player*, Vec2*)` shape is **corroborated**.
- **Fields it touches (this+…):** `0x10`, `0x18`, `0xca8` (→ vcall `+0x5f0`),
  and `this+0x240` is passed to `0xe82ffac`/`0xe82ffb0` — the **exact same pair**
  `ClientInstanceGetLocalPlayer` (`0x9b7f6d0`) calls with `this+0x240`.
  Independent corroboration that `+0x240` is the local-player slot.
- **EnTT component hashes requested** (32-bit `movz/movk` pairs):
  `0x2c74480`, `0x018b1887`, `0x1615ae60`, `0x75df36b7`.
- **Calls:** → `0xfdb02a8` (the getter at +0x380 of `0xfdaff28`) and
  `0xfdb01e8` (the setter at +0x2c0), plus EnTT `view`/`try_get` helpers
  `0x9c55004`, `0x9c70780`, `0x9c57e68`, `0x8a6337c`, `0x8a65cf8`.
- **Critical structural consequence:** because `+0xfdb02a8` and `+0xfdb01e8` are
  called **intra-function**, they are almost certainly **inlined into**
  `0xaf60090` at 1.26.62. Hooking the entry of `0xfdaff28` would **not**
  intercept these calls.

## Turn math inside `0xaf60090` (what the deltas actually mean)

```
0xaf603ec  bl 0xfdb02a8                  ; read current pair
0xaf603f0  ldr s2, [x20, #0x4]            ; delta.y
0xaf603f8  fadd s1, s1, s2                ; current.? + delta.y
0xaf60414  bl 0xe830314                   ; wrap helper
0xaf60404  ldr s1, [x20, #0x4]
0xaf60408  fadd s1, s8, s1                ; current.? + delta.y
0xaf60414  bl 0xe830314                   ; wrap helper
0xaf604b4  ldp s0, s1, [x20]              ; delta.x, delta.y
0xaf604b8  fmul s0,s0,s0 / fmul s1,s1,s1  ; squared magnitude
0xaf604c4  movi v1.2s, #0x34, lsl #0x18   ; materialised constant
0xaf604c8  fcmpe s0, s1 / b.le            ; |delta|² vs constant gate
0xaf604e0  fneg s0, s0                    ; NEGATED one component
0xaf605cc  str  s8, [x8, #0x4]            ; write resulting angle into component
```

Readings: delta is **additive per call** (a turn *rate*, not an absolute angle);
the wrap helper is called after each axis; one axis is explicitly **negated**
before use — i.e. **the two axes do not share a sign/meaning convention**; the
stored result lands at **`component+0x4`**, so the pair is not at `+0x0`.

## What the RTTI/enumeration did *not* give

- `rtti_dump.txt` is a **2000-entry truncated live scan** (`[rtti] found 2000`) of
  `Xbl*`/system classes — it contains **no** `basic_storage`, no `entt`, and no
  game actor classes. It cannot be used for component discovery. (`findclass<>`
  uses a different, complete path, which *does* expose `basic_storage` names.)
- **No type name containing `Rotation` or `StateVector`** appears in the searchable
  RTTI set → the rotation store is not reachable by name. Evidence points to the
  pair living inside the EnTT component whose hash is `0x2c74480`
  (storages requested at `this+0x10`/`+0x18`).
- `dynsym_dump.txt`: **0 exported symbols** — `mapped 0`, `[dynsym] 0 exported`.
  So dynamic-symbol resolution is NOT available on this build; only RTTI names and
  byte patterns are.
