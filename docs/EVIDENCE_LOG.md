# Evidence Log

Classifications: **CONFIRMED** / **INFERRED** / **HYPOTHESIS** / **UNKNOWN**
(project instruction §4). No finding enters this log without a source.

## Target binding — the blocking unknown

Both reference projects are pinned to *other* builds than the one declared by our
references, and we do not yet have a build identity for the user's device:

| Project | Declared / observed target | Source |
|---|---|---|
| BedrockTools (reference) | `1.26.6X.X` (band) | `levimod.json` |
| User's earlier projects (Seed-Taker) | `1.26.62`, `libminecraftpe.so` 342,231,392 B, text `0x0-0x13686ff0`, 7 segments | Seed-Taker `evidence/ST-001/zaphkiel_out.txt` header |
| **This project (target)** | **UNKNOWN — `T-P1` must fill this in** | — |

Every offset/signature in this repository is therefore **UNKNOWN for our target**
until it is re-verified against the user's exact binary. This is not caution for
its own sake: the same reference rewrites ~66% of its signature table (100 of 152 rows) between two
adjacent builds (§F-008).

## Reference-derived facts

| ID | Finding | Class | Evidence / derivation | To confirm / disprove |
|----|---------|-------|-----------------------|------------------------|
| F-001 | Target platform is Android arm64-v8a, native mod loaded by LeviLauncher/preloader | CONFIRMED | BedrockTools `xmake.lua:11-12,31-35`; `README.md` requirements; Zaphkiel-RE `README.md` | N/A |
| F-002 | Reference declares game support as a band `1.26.6X.X`, not a build | CONFIRMED | BedrockTools `levimod.json` `minecraft_versions` | `T-P1` supplies the real build string |
| F-003 | Mod ABI is preloader's `PL_REGISTER_MOD` + `pl::mod::ModContext` lifecycle | CONFIRMED | BedrockTools `src/main.cpp:4-17`; preloader `include/pl/Mod.hpp` | N/A |
| F-004 | Game lib discovered via `dlopen("libminecraftpe.so", RTLD_NOW\|RTLD_NOLOAD)`, else a `dlopen` detour in `libdl.so`; no version check in that path | CONFIRMED | BedrockTools `src/core/Runtime.cpp:41-47,132-144,151-157` | Runtime timing on device (`T-P4`) |
| F-005 | Available memory primitives: `resolveSignature(s)`, `resolveVtableFunction`, `hook`/`unhook` chain, `read/writeBytes`, `revertPatch` | CONFIRMED | preloader `include/pl/memory/{Signature,Vtable,Hook,Patch}.hpp` | N/A |
| F-006 | `hook` is a priority-ordered chain over Gloss detours, keyed by target address | CONFIRMED | preloader `src/pl/memory/Hook.cpp:66-142`; `GlossHook*` at `:6,101-109` | N/A |
| F-007 | Reference has **no** version dispatch; one flat table serves one build | CONFIRMED | Tree-wide grep for version gating = empty; `Version.hpp:11` is the mod's own version; game version read only in `GameHooks.cpp:127-131` | N/A |
| F-008 | A build bump rewrites ~66% of signature rows and ~22% of offsets | CONFIRMED | Diff `6793c48`→`3de9a7d` ("26.60 support"): 100/152 `SignatureDefinition` rows, 47/218 `constexpr` offsets | Re-measure on the next bump to test the ratio |
| F-009 | Patterns wildcard register allocations and frame immediates, not just PC-relative operands | CONFIRMED | `Signatures.cpp:13` vs the `26.52` variant of the same `VersionString` row in the diff | N/A |
| F-010 | Signature resolution is by exact pattern string, returns the **first** match, no uniqueness check, no warning | CONFIRMED | BedrockTools `Signatures.cpp:168-191`; preloader `src/pl/memory/Signature.cpp` `scanPatternWindow`/`scanCompiledPatterns` (found-patterns leave the search set) | N/A (design constraint, see `docs/CROSS_VERSION.md` R-1) |
| F-011 | Two resolution routes (byte pattern or RTTI+slot); empty-both rows can never resolve; `resolveAll` returns true if **any** row resolved | CONFIRMED | `Signatures.hpp:168-173`; `Signatures.cpp:144,151,177-190` | N/A |
| F-012 | Hook installation failures are ignored per-hook; "installed" needs only 2 of the 23 `hookSignature` call sites | CONFIRMED | `GameHooks.cpp:72-79` (`hookSignature` returns bool, callers discard), `:312-334`, `:334` | N/A |
| F-013 | `virtualCall`/`field` perform unchecked dereference of attacker-controlled-slow indices/offsets | CONFIRMED | `include/bedrocktools/sdk/Memory.hpp:40-55` | N/A |
| F-014 | Reference has no self-diagnosis: no resolution report, no counts, no per-fact status | CONFIRMED | Tree review; `resolve(id)` returns `0` only (`Signatures.cpp:193-196`) | N/A |
| F-015 | Zaphkiel is docs-only (7 files) — cannot be linked, built, or invoked by us | CONFIRMED | Zaphkiel-RE tree; `README.md` | N/A |
| F-016 | Zaphkiel target grammar (`fn/add/scan/range/vft/strfn/sig/struct/xref_tree/fullf/fullvf/fc/vfc/hier/findclass/h`) plus `entt/entthash/vftdiag` from `zEnTT.md` | CONFIRMED | Zaphkiel-RE `README.md` "Target Syntax"; `zEnTT.md`; `zaphkiel_config.txt` comments | N/A |
| F-017 | Output artefacts and their device paths | CONFIRMED | Zaphkiel-RE `README.md` "Output Files"; `zaphkiel_config.txt` `out_dir` | N/A |
| F-018 | Zaphkiel's generated signature format is accepted by preloader's parser | CONFIRMED (format) | Zaphkiel-RE `README.md` "Signature Generator"; preloader `Signature.cpp:64-96` (`?`/`??`/partial masks) | End-to-end: take a generated sig, `sig<>` it back, then load it (`T-P3`) |
| F-019 | preloader anchors on the longest fully-specified byte run, capped at 8; `??` never anchors | CONFIRMED | preloader `Signature.cpp:23,147-190`; anchor use at `:313-320` | N/A |
| F-020 | Zaphkiel addresses are ASLR-slid live VAs, not reusable across runs | CONFIRMED | Seed-Taker `evidence/ST-001/zaphkiel_out.txt` header (segments + note); observed VA drift between its ST-001/ST-002 | N/A |
| F-021 | Reference provides no version registry, verification, disambiguation, gating, or compatibility reporting | CONFIRMED (absence) | F-007, F-010, F-012, F-014 | N/A |
| F-022 | Reference has no relationship to Zaphkiel (no reference to it in full history) | CONFIRMED | `grep -rni zaphkiel` over full cloned history = empty | N/A |
| F-023 | Correctness of any reference signature/offset for *our* build is unknown | UNKNOWN | Reference bands vs. our unknown target | `T-P1` + per-fact re-verification |

## Open questions this project must answer

| ID | Question | Class | Next step |
|----|----------|-------|-----------|
| Q-001 | What exact build is the user running (`build` string, `libminecraftpe.so` size, segment table, text range)? | UNKNOWN | `T-P1` |
| Q-002 | Is `libminecraftpe.so`'s RTTI/`.dynsym` reachable in the user's mapping (needed for the D1 route)? | UNKNOWN | `T-P1` (`findclass<>` + `dynsym` presence) |
| Q-003 | Does a generated signature survive round-trip: `sig<>` returns exactly 1 match at the same function? | UNKNOWN | `T-P2`/`T-P3` |
| Q-004 | Which single function is a safe "is the game alive and is this build ours" probe? | UNKNOWN | `T-P1` candidate selection → `T-P2` verification |
| Q-005 | What does the user actually want built (feature spec)? | UNKNOWN | Awaiting user. See `docs/PROJECT_CHARTER.md` §1 |

## Investigations

None issued yet. The first investigation (`T-P1`) is specified in
`docs/INVESTIGATIONS.md` and is **blocked on the user's confirmation of build
target and feature scope** — running it against the wrong build would produce
evidence we could never use.

## Change history

- 2026-11-15: log created. Reference snapshots pinned (`docs/REFERENCE_FINDINGS.md`
  §"Snapshots inspected"). No binary evidence of our own yet.
