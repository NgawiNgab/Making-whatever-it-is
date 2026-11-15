# Reference Findings

The technical basis for everything else in this repository. Nothing here is
invented: every finding cites the exact file and symbol it came from, or is
explicitly marked as an assumption.

## Snapshots inspected

| Reference | Commit | Notes |
|---|---|---|
| [NgawiNgab/BedrockTools](https://github.com/NgawiNgab/BedrockTools) | `c03d3ae0c9022557de0acdf0d94e373e0b9e0579` | v1.5.12, 60 commits, MIT (`LICENSE:1`) |
| [NgawiNgab/Zaphkiel-RE](https://github.com/NgawiNgab/Zaphkiel-RE) | `ba566e6eea4b71c544edd21d9c79d988c5791508` | docs-only repo (7 files) |
| [LiteLDev/preloader-android](https://github.com/LiteLDev/preloader-android) | `cc08b40a31471d3e2dedb267f5f17b28da2b0604` | the runtime SDK BedrockTools builds on; needed to interpret it |

Reference freshness note: the project instructions point at `NgawiNgab/BedrockTools`.
The project instructions name `NgawiNgab` as the owner, and that fork is the one
BedrockTools' own `levimod.json` `homepage_url` now points to
(`levimod.json`, upstream `QYCottage/BedrockTools`) — i.e. the fork is the current
line. `README.md` still credits RadiantByte; `levimod.json` credits author `MrPokeG`.
Both facts are recorded without inference about who maintains what.

## 1. Environment and runtime contract

**F-001 — Target platform. CONFIRMED.**
BedrockTools is an Android `arm64-v8a` native mod loaded by LeviLauncher via
preloader. `xmake.lua:11-12,31-35` (`set_kind("shared")`, android-only flags
`-fPIC -O2`, `-Wl,-z,max-page-size=16384`), `README.md` "System Requirements"
(Android 9+, 64-bit ARM, LeviLauncher). Zaphkiel is likewise an Android/ARM64
injected `.so` (`Zaphkiel-RE/README.md` "A reverse engineering framework for
Android via LeviLauncher. Injected as `libZaphkiel.so`").

**F-002 — Supported game build. CONFIRMED (declared), not verified.**
`levimod.json` declares `"minecraft_versions": ["1.26.6X.X"]` — a version
*band*, not a build. This repository must not inherit it silently.

**F-003 — The mod ABI is preloader's, not BedrockTools'. CONFIRMED.**
Entry point is `PL_REGISTER_MOD` on a class with
`load/enable/disable/unload(pl::mod::ModContext&)`
(`src/main.cpp:4-17`). `ModContext` exposes `id()`, `javaVm()`, `configDir()`,
`resourceDir()`, `modRootPath()`, `logger()`
(`preloader-android/include/pl/Mod.hpp`, class `ModContext`).

**F-004 — Game library discovery pattern. CONFIRMED.**
BedrockTools `dlopen`s `libminecraftpe.so` with `RTLD_NOW | RTLD_NOLOAD`; if that
fails it hooks `dlopen` in `libdl.so` and waits for the library to appear:
`src/core/Runtime.cpp:41-47` (`dlopenDetour`), `:132-144` (`load`),
`:151-157` (`enable`). Re-entrancy is guarded by a `thread_local` flag
(`:26,33-39`). Note `:133` and `:152` are the only places that decide "is the
game up yet" — there is no version/digest check anywhere.

**F-005 — Memory primitives available to us. CONFIRMED.**
`pl::memory::resolveSignature(sig, module)` and
`resolveSignatures(span, module)` (`include/pl/memory/Signature.hpp:24-33`);
`resolveVtableFunction(typeInfoName, slot, module)` (`include/pl/memory/Vtable.hpp:21-37`);
`hook(target, detour, &original, priority)` / `unhook`
(`include/pl/memory/Hook.hpp:29-34`); `readBytes`/`writeBytes`/`revertPatch`
(`include/pl/memory/Patch.hpp:26-40`).

**F-006 — Hook backing implementation. CONFIRMED.**
`pl::memory::hook` is a *chain*: several mods can hook the same target; the map
key is the target address and priority orders the chain
(`preloader-android/src/pl/memory/Hook.cpp:66-142`). The actual patch is done by
third-party **Gloss** (`GlossHook`, `GlossHookReplaceNewFunc`,
`GlossHookDelete`, `Hook.cpp:6,29-36,60-62,101-109`). Consequence for us: a hook
can be shared with a different mod that resolved the *same address*, and
`unhook` only removes *our* detour from the chain (`Hook.cpp:114-142`).

## 2. How BedrockTools survives game updates

This is the single most important architectural finding, because it decides what
we can reuse and what we must not.

**F-007 — BedrockTools is single-version by construction, not multi-version. CONFIRMED.**
There is no version dispatch anywhere in the tree. A `grep` for
`#if`-style version gating across `src/` and `include/` returns nothing;
`Version.hpp:11` is the *mod's* own version string only. The one place the game
version is even read is `versionDetour` (`src/core/GameHooks.cpp:127-131`), which
captures the string for display and does nothing else with it.

**F-008 — A game update rewrites the two version-coupled data sets wholesale. CONFIRMED by diff.**
Measured on the `26.52` → `26.60` support commit (`6793c48` → `3de9a7d`,
`feat: 26.60 support`):

| Data set | File(s) | Entries | Changed between those two builds |
|---|---|---|---|
| Byte signatures | `src/core/memory/Signatures.cpp` | 152 `SignatureDefinition` rows | **-100 / +105 rows** |
| Struct/vtable offsets | `include/bedrocktools/sdk/offsets/*.hpp` | 218 `constexpr` | **47 changed** (Core 12, World 13, Render 15, Network 5, UI 1, Inventory 1, Skin 0) |

Interpretation: roughly two-thirds of the signature table is *rewritten* on a
version bump, and ~22% of the offsets change. This is the reference's own
maintenance cost per supported game build, paid by hand. **Any design that
assumes "resolve once, works forever" contradicts this evidence.**

**F-009 — Signatures are wildcarded, and that is deliberate. CONFIRMED.**
Patterns are space-separated hex with `?`/`??` wildcards, e.g.
`"FD ? ? A9 F3 ? ? F9 FD 03 00 91 ..."` (`Signatures.cpp:13`). Note that the
reference does **not** just wildcard PC-relative operands; it wildcards *register
allocations and stack-frame immediates* too (the `26.52`→`26.60` diff shows
`FD ? ? A9 F3 ? ? F9` becoming `? ? ? A9 ? ? ? F9` in the very same function).
So the patterns encode instruction *shape*, not a byte-exact trace — and they
still had to be re-tuned per build.

**F-010 — Resolution is by exact pattern string, first match, no uniqueness. CONFIRMED.**
`resolveAll` collects the pattern strings and calls
`pl::memory::resolveSignatures(patterns, "libminecraftpe.so")`, then looks each
address up *by the pattern string itself*
(`Signatures.cpp:168-191`, lookups at `:175` and `:184`). In preloader, the scan
returns the **first** match inside the first matching region and then stops
(`preloader-android/src/pl/memory/Signature.cpp`, `scanPatternWindow` returns on
first hit; `scanCompiledPatterns` clears a pattern from the search set once
found). **A pattern with more than one match is silently resolved to whichever
one the scan reaches first.** No warning is emitted. This is a real hazard we
must design around, not replicate.

**F-011 — Two resolution routes exist. CONFIRMED.**
`SignatureDefinition` carries `{id, pattern, rtti, vtableSlot}`
(`include/bedrocktools/memory/Signatures.hpp:168-173`). If `rtti` is non-empty
it goes through `resolveVtableFunction` (`Signatures.cpp:180-182`), otherwise
through the byte pattern (`:183-186`). The RTTI route is used for packet
handlers, e.g. `SignatureDefinition{SignatureId::PlayerListPacketRead, "", "16PlayerListPacket", 17}`
(`Signatures.cpp:144`).
Caveat observed: one row has **both** fields empty
(`ItemInHandRenderOffhandItem`, `Signatures.cpp:151`) and therefore can never
resolve; `resolveAll` still reports success if *any* other row resolved
(`:177-190`). Partial failure is invisible to callers of `resolveAll`.

**F-012 — Hooks install silently and individually. CONFIRMED.**
`hookSignature` returns `false` on failure and each call site ignores the result
(`src/core/GameHooks.cpp:72-79` and the `install()` block at `:310-336`). The only
gating is `installed = tickOriginal != nullptr && clientUpdateOriginal != nullptr`
(`:334`), so the mod can come up "installed" with most of its hooks missing.

**F-013 — Failure mode for offsets. CONFIRMED (by code reading).**
`virtualCall` dereferences `*(void***)instance` and then `table[index]` with no
bounds, table-pointer, or null check
(`include/bedrocktools/sdk/Memory.hpp:50-55`). `field<T>` is a raw
`*reinterpret_cast<T*>(base + offset)` (`:40-48`). A wrong vtable index on a game
update is therefore an arbitrary call, and a stale offset is an arbitrary read.
There is no validation layer between the offset constants and the game objects.

**F-014 — BedrockTools has no self-diagnosis. CONFIRMED.**
Nothing in the tree reports which signatures resolved, with how many matches, or
which offsets were exercised at runtime. `memory::resolve(id)` simply returns
`0` on failure (`Signatures.cpp:193-196`) and call sites treat `0` as "skip"
(e.g. `GameHooks.cpp:78-79`).

## 3. Zaphkiel's contract

**F-015 — Zaphkiel is external and user-operated only. CONFIRMED.**
It is a docs-only repository: `README.md`, `LICENSE.txt`, `SPDX.txt`,
`zInput.txt`, `zaphkiel_config.txt`, `sig_targets.txt`, `zEnTT.md` — 7 files, no
source. There is nothing for us to build against, link, or invoke. Every
Zaphkiel result must therefore arrive as an artefact the user produces on-device.

**F-016 — Target syntax. CONFIRMED.**
`README.md` "Target Syntax" defines: `fn<>`, `add<>`, `scan<>`, `range<>`,
`vft<Class><slot>`, `strfn<>`, `sig<>`, `struct<Class><offset>`, `xref_tree<>`,
`fullf<>`, `fullvf<>`, `fc<>`, `vfc<>`, `hier<>`, `findclass<>`, `h<>`.
`zEnTT.md` additionally documents `entt<>`, `entthash<>`, `vftdiag<>`.
`zaphkiel_config.txt` headers document the legacy forms `14ClientInstance` (RTTI
xref) and inline `vft<Class><slot><N>` param hints; `README.md` no longer lists
those two, so treat `zEnTT.md`/config comments as the authority for them being
accepted, and `README.md` as the authority for the current list.

**F-017 — Output artefacts. CONFIRMED.**
`zaphkiel_out.txt` (main), `zaphkiel_out.c` (`fc`/`vfc` pseudo-C),
`zaphkiel_fullout.txt`, `rtti_dump.txt`, `dynsym_dump.txt`, `vtable_dump.txt`,
`str.txt`, `live_strings.txt`, `signatures.txt`, `signatures.json`,
`{Header}_parse.txt`; all written to `out_dir` on the device
(`/storage/emulated/0/Android/media/org.levimc.launcher/kurumi/` by default).

**F-018 — Signature generator emits the format preloader parses. CONFIRMED (format level).**
Zaphkiel's generator outputs "IDA / code / mask" forms and auto-wildcards
PC-relative instructions (ADRP, ADR, BL, B, B.cond, CBZ/CBNZ, TBZ/TBNZ, LDR
literal) for cross-build stability (`README.md` "Signature Generator";
`sig_targets.txt` accepts `fn<>` / `add<>`). preloader's `parsePatternToken`
accepts `?` and `??` tokens plus partial-byte masks such as `?4`/`A?`
(`preloader-android/src/pl/memory/Signature.cpp:64-96`). The code form is
therefore directly consumable — **but see F-019.**

**F-019 — Anchor-selection caveat in preloader's scanner. CONFIRMED (mechanism).**
To scan fast, preloader picks the longest run of *fully-specified* bytes as its
anchor, capped at 8 bytes (`Signature.cpp:23,147-190`). Fully wildcarded
(`??`) bytes never qualify. If a pattern's specified bytes sit in a short run,
the anchor is weak and the first-hit-wins rule (F-010) applies with less
discrimination. More importantly: **a long signature is not automatically a safe
one** — safety comes from the specified bytes being unique in the binary, which
is a property we must measure, not assume.

**F-020 — Addresses are per-run and never reusable. CONFIRMED.**
Zaphkiel reports live VAs from an ASLR-slid mapping. Evidence from a real run:
`libminecraftpe.so` `342,231,392` bytes, 7 segments, text `0x0-0x13686ff0`
(the sibling Seed-Taker project's `evidence/ST-001/zaphkiel_out.txt`, header).
Recorded convention in that project: only raw RTTI/symbol strings and
segment-relative facts carry across runs; VAs do not. We adopt the same rule.

## 4. What the reference explicitly does *not* give us

**F-021. CONFIRMED (by absence, tree-wide).** BedrockTools contains no
version registry, no per-build offset tables, no signature verification, no
multi-candidate disambiguation, no capability/feature gating, and no
compatibility reporting. A `grep` for version gating returns nothing (F-007);
there is one flat table (F-008); resolution is first-match (F-010); hooks fail
silently (F-012). Any of those facilities we want, we design and build.

**F-022. CONFIRMED.** BedrockTools has no relationship to Zaphkiel: a
tree-wide `grep -rni "zaphkiel"` over the full history returns nothing. The
"resolve by signature → hook" loop and the "find the address" tool are
connected only by matching *output format* (F-018), not by design.

**F-023. UNKNOWN (and out of our hands).** Whether any given BedrockTools
signature or offset is correct for the build the user actually runs. Its
declared band is `1.26.6X.X`; a Zaphkiel run cited elsewhere in the user's
projects reports `1.26.62`. Matching the declared band is not evidence of
correctness for a specific binary — every value we reuse must be re-verified
against the user's exact `libminecraftpe.so`.
