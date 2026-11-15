# Cross-Version Engineering

How this repository is built so that a Minecraft Bedrock update is a *data*
event, not a code rewrite. Everything here is derived from measured properties of
the references (`docs/REFERENCE_FINDINGS.md`), not from preference.

## 1. The problem, measured

BedrockTools supports one game build at a time and pays a full manual re-tune per
build. Measured on its own `26.52 → 26.60` support commit (F-008):

- **~66% of the byte-signature table is modified** (100 of 152 rows).
- **~22% of the offset constants change** (47 of 218).
- Zero of that is version-dispatched in code; the table is simply replaced (F-007).

Two conclusions follow, and they are not negotiable:

1. **Version-specific data is volatile.** It must live in data files keyed by
   build, never inline in logic.
2. **Volatility is not uniform.** Different facts decay at different rates. Our
   architecture should spend its safety budget where decay is fastest.

## 2. Decay classes

Every game fact we depend on is classified, and the class decides the strategy.

| Class | What it is | Decay | Strategy |
|---|---|---|---|
| **D1 Stable identity** | Itanium RTTI type names (`9LevelData`, `15StartGamePacket`), mangled symbol names present in `.dynsym`, vtable *slot* meanings per type | Low — survives builds unless the class is refactored | Resolve by **name + slot** (`resolveVtableFunction`), never by address. Self-identifying: a hit proves *what* it is, not just *where*. |
| **D2 Unstable code** | Function bodies, byte patterns, `bl` targets | High — re-tuned almost every build | Resolve by signature, but require **uniqueness** and a **structural self-check** (below). |
| **D3 Unstable layout** | Struct offsets, vtable *indices* for classes that gained/lost virtuals, component slots | High | Never read blind. Reach the field through a **D1/D2-resolved accessor**, or validate the layout at runtime before first use. |
| **D4 Protocol/behaviour** | Packet ids and payload layout, serialization | Medium, and drifts independently of the binary | Treat as its own versioned data set; cross-check against Zaphkiel evidence for the exact build. |

Rule of thumb: **prefer D1, tolerate D2, gate D3, version D4.** Where the
reference does the opposite — a bare vtable index like
`ClientInstanceGetLocalPlayer = 31` (`include/bedrocktools/sdk/offsets/Core.hpp:24`)
read through an unchecked `virtualCall` (F-013) — we do not copy it.

## 3. Resolution must be observable

The reference cannot tell you why it failed (F-014), resolves duplicates to the
first hit silently (F-010), and treats partial failure as success (F-011, F-012).
That is the difference between "the mod doesn't work" and "the mod knows which
fact it is missing". Our contract:

```
resolve  ->  { address, confidence, provenance } for every fact
gate     ->  a feature runs only if every fact it declares is AVAILABLE
report   ->  a machine-readable resolution report is always written
```

Non-negotiable behaviours, each one answering an observed reference weakness:

- **R-1 Uniqueness.** A signature that matches more than once is *rejected*, not
  first-matched (contrast F-010). The resolver counts matches; it does not stop
  at the first.
- **R-2 Structural self-check.** After resolution, the resolver re-reads the
  function it found and compares cheap, build-independent characteristics against
  what the signature *expected* (prologue shape, presence of a sentinel constant,
  callee count class, terminal `ret`/`b` shape). A hit that fails the check is
  downgraded to a candidate, not accepted.
- **R-3 Total failure is loud.** `resolveAll` in the reference returns true if
  *any* row resolved (F-011). Ours returns a per-fact status and never lets a
  partially resolved table present itself as healthy.
- **R-4 Provenance.** Every resolved address carries its pattern/RTTI, the match
  count, and the segment-relative RVA, so a report can be diffed across builds
  and across users.
- **R-5 Fail closed, visibly.** A feature whose facts are missing is disabled
  with the reason surfaced (log + status), never silently no-op and never
  crash-on-deref.

## 4. Signatures are a measurement, not a guess

A byte pattern is a *claim about uniqueness in one specific binary*. The claim is
testable before it ships, using the tool that generated it.

Checklist a signature must pass before it enters a build's data file:

1. **Uniqueness measured.** Run `sig<pattern>` against the exact
   `libminecraftpe.so`. Any count ≠ 1 disqualifies it (R-1). A count of 1 in the
   wrong place is still wrong, so:
2. **Identity verified at the landing site.** Run `fn<0xADDR>` at the resolved
   address and confirm the disassembly is the expected function — not a thunk,
   not a tail of a neighbour, not a data region.
3. **Anchor quality.** The specified bytes must form a contiguous run of at least
   8 fully-specified bytes, ideally in the function body rather than the standard
   prologue. Rationale: preloader anchors on the longest fully-specified run,
   capped at 8 bytes, and `??` bytes never anchor
   (F-019; `preloader-android/src/pl/memory/Signature.cpp:23,147-190`).
4. **Volatile operands excluded.** PC-relative instructions (ADRP/ADR/B/BL/
   B.cond/CBZ/CBNZ/TBZ/TBNZ/LDR-literal) must be wildcarded — Zaphkiel's
   generator does this automatically (F-018). Register allocations and stack
   frame sizes must *also* be wildcarded, because they demonstrably change
   between builds in the same function (F-009).
5. **Structural fingerprint recorded.** Store, next to the pattern, the cheap
   facts step 2 observed. That is what R-2 checks against at runtime.

Doctrine: **more wildcards + a strong anchor + a structural check beats a long
brittle byte-exact pattern.** A pattern that is 95% specified and re-tuned every
build is worth less than one that is 60% specified, unique, and self-checking.

## 5. Offsets: reach them through code, not arithmetic

`field<T>(obj, 0x1B8)` (reference: `include/bedrocktools/sdk/Memory.hpp:40-48`)
is an unchecked read (F-013) of a number whose decay class is D3. Instead:

1. **Resolve the accessor, not the field.** A getter is a *function*: it can be
   found by RTTI+slot (D1) or signature (D2), and it self-checks. Its existence
   also tells you the field's type.
2. **Confirm the slot by inspection.** `hier<Class>` gives the base-class chain
   (slot numbering depends on it), and `fullvf<Class><a,b>` shows what each slot
   actually does. Slot meaning is D1; slot *contents* still need a look.
3. **Validate before trusting.** Read the field once in a known-good frame and
   sanity-check it against an independently obtained value (finite floats, an
   in-range integer, a pointer inside a mapped region). Never let a raw offset
   be the first thing code does with an unknown object.
4. **Scope the object.** A field read is only meaningful on an object whose type
   was established (RTTI-backed vtable pointer matching the resolved typeinfo).
   "Plausible pointer" is not identity (see the project's evidence rules).

## 6. The Zaphkiel → code bridge

The loop is: agent specifies a question → user runs Zaphkiel → artefacts land in
`evidence/` → facts get classified in `docs/EVIDENCE_LOG.md` → verified facts
become a **version data file** → code reads data, never constants.

### 6.1 Making addresses reusable

Zaphkiel VAs are ASLR-slid and never reusable across runs (F-020). Convert every
address to a **segment-relative RVA** before it goes into a document, using the
segment table at the top of `zaphkiel_out.txt`:

```
for each segment s:  if (s.va <= rva_va < s.va + s.sz)
    file_offset = s.fo + (rva_va - s.va)
```

where `rva_va = live_va - slide`. Always record *which* build's segment table it
was computed against. Record the RVA and the function's shape; never the VA.

Example of the input this transforms (real header from a 1.26.62 run, 342,231,392
bytes, text `0x0-0x13686ff0`): six further segments follow, so a VA in
`.data.rel.ro` must be mapped through its own `fo`/`va` pair, not through `.text`.

### 6.2 Version data file

One file per supported build, keyed by a build identity, holding:

```
build      : { mc_version, lib_size, text_rva_range, sha256_if_known }
signatures : [ { id, pattern, structural_check, verified_by, verified_on } ]
rtti_slots : [ { id, typeinfo, slot, verified_by } ]
offsets    : [ { id, struct, delta, reach_via, validated } ]
```

Nothing in `src/` may contain a hex literal that belongs to a build. A code
review can enforce that mechanically.

### 6.3 The report closes the loop

Every run writes `resolution-report.json`: per fact `{id, state
(AVAILABLE|MISSING|AMBIGUOUS|REJECTED), address_rva, matches, check_result}`.
A user who reports a bug sends that file. A build bump is then a diff of reports
plus targeted Zaphkiel runs, not a fishing expedition.

## 7. Anti-patterns from the reference (do not copy)

| Anti-pattern | Where | Why it hurts |
|---|---|---|
| First-match signature resolution | F-010 | Wrong-but-plausible address, no warning |
| `resolveAll` returns "any resolved" | `Signatures.cpp:177-190` | Partial breakage reported as success |
| Empty-pattern row that can never resolve | `Signatures.cpp:151` | Dead entry, invisible |
| Unchecked `virtualCall` index | `Memory.hpp:50-55` | Arbitrary call on a re-indexed vtable |
| Unchecked offset read | `Memory.hpp:40-48` | Arbitrary read |
| Hooks installed with ignored return | `GameHooks.cpp:72-79,310-336` | Half-installed mod claims success |
| No version gate anywhere | F-007 | Silent misbehaviour on any other build |
| Constants inline in logic | `offsets/*.hpp` | Forces a compile per game build |

## 8. Non-goals

- We do not promise compatibility with an unknown future build. We promise to
  *detect* that we are unsupported and say so (R-5).
- We do not treat a BedrockTools pattern as evidence for a build it was not tuned
  for (F-023). Reuse requires re-verification on the user's exact binary.
- We do not add a runtime that tries signatures until something does not crash.
  Trial-and-error hooking at address-guess precision is how the reference's crash
  classes happen.
