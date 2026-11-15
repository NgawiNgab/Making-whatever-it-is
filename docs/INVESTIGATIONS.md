# Investigation Requests

Rules this file obeys (project instruction §3, §7):

- The agent never claims to have run Zaphkiel. Every result in `evidence/` is
  user-produced and is filed with the run that produced it.
- One investigation answers one question. No broad fishing sweeps.
- Every request states exactly what will change in the code or the plan
  depending on the outcome.
- VAs in results are converted to segment-relative RVAs before being used
  (`docs/CROSS_VERSION.md` §6.1).

Numbering: `T-Pn` (pipeline/general) and `T-CAMn` (camera feature). Evidence
lives in `evidence/<ID>/`.

**Active:** [`T-CAM1`](investigations/T-CAM1.md) — camera turn path and rotation
storage, for `docs/features/CAMERA_FACING.md`. Issued 2026-11-15.

The items below are the earlier general-purpose queue. They are kept for the
record; T-CAM1 supersedes them as the next run, and T-P2.5's hook-length check is
folded into T-CAM1 step 2.

---

## T-P1 — Confirm the library instance and mapping facts

**Status: READY — issuance deferred at the user's request (2026-11-15).**
Target build is now fixed (1.26.62 arm64), so this is no longer blocked on a
decision; it is queued. It still must run, because a target *version* is not a
target *binary instance*: the same version string can be built differently, and
every address and offset we later use is bound to one library image.

**Objective.** Pin down the user's exact `libminecraftpe.so`: size, segment table,
text bounds, and whether RTTI/`.dynsym` are readable. Then establish whether the
user's existing 1.26.62 evidence (Seed-Taker `ST-001`, which recorded
`342,231,392` bytes / 7 segments / text `0x0-0x13686ff0`) describes **the same
image**. If it does, that prior evidence becomes a candidate source we can
re-verify cheaply instead of re-deriving from scratch.

**Current evidence.** Target version 1.26.62 (user, 2026-11-15). Reference band
`1.26.6X.X` (F-002). Prior user evidence on a 1.26.62 image (`F-020`, `F-024`,
`F-026`, `F-027`) — not yet proven to be the same image.

**Unknowns.** Library size/segments for *this device now*; identity vs. the prior
image; RTTI reachability; `.dynsym` presence; the exact mangled RTTI form of the
first types we need.

**Commands.**

`zaphkiel_config.txt` additions for this run:

```
dump_rtti = true
dump_dynsym = true
separate_dump_files = true
```

`zInput.txt` (5 targets, in this order):

```
findclass<ClientInstance>
findclass<MinecraftGame>
strfn<"1.26
hier<ClientInstance>
findclass<AppPlatform>
```

Purpose of each:

| Target | Why exactly this |
|---|---|
| `findclass<ClientInstance>` | Confirms RTTI is readable and gives the flat vs. nested mangled form for the type we will need first (D1 route anchor). |
| `findclass<MinecraftGame>` | Same, for the app-root object; also exposes whether the build renamed/moved it. |
| `strfn<"1.26` | The game's version string is constructed near the version-reporting path; a prefix xref also tells us whether the string is split/obfuscated in this build. |
| `hier<ClientInstance>` | Base-class chain → tells us which vtable indices come from base classes (needed before any slot work). |
| `findclass<AppPlatform>` | Independent second data point for RTTI reachability, in a different subsystem. |

**Expected output.** `zaphkiel_out.txt` (main), `rtti_dump.txt`, `dynsym_dump.txt`.
The first lines of `zaphkiel_out.txt` must contain the `lib:` path, byte count,
segment count, and the segment table — that header alone is most of T-P1's value.

**Decision criteria.**

- If the header shows **no** segment table / a different library path than the
  game's real `libminecraftpe.so` → stop; fix the mapping before anything else.
- **Identity vs. prior evidence:** compare byte count + segment table against
  `342,231,392` B / 7 segments (text `0x0-0x13686ff0`). Match on all three ⇒ the
  prior image is *probably* the same and its findings become cheap candidates for
  re-verification (still re-verified, never inherited). Any mismatch ⇒ prior
  addresses are unusable; treat that evidence as reference-only.
- If `findclass<>` returns matches in the expected count range and `hier<>`
  returns a non-empty base chain → **D1 route is usable**; proceed to T-P2.
- If `findclass<>` returns 0 for all five → RTTI is not readable this run
  (strip page or timing). Then the whole design shifts to D2-only and the
  feature set must be re-scoped; this is a blocking finding, not a retry prompt.
- If `findclass<ClientInstance>` returns nested forms only (`N...E`), record the
  exact mangled string — `resolveVtableFunction` accepts the stored name string
  (`preloader-android/src/pl/memory/Vtable.cpp:137-162`), so we need the literal
  bytes it searches `.rodata` for.

**What is filed afterwards.** `evidence/T-P1/zaphkiel_out.txt` (verbatim),
`evidence/T-P1/rtti_dump.txt`, `evidence/T-P1/dynsym_dump.txt`,
`evidence/T-P1/README.md` (date, build string, lib size, segment table,
sha256 if the user can produce one). Findings F-1xx appended to
`docs/EVIDENCE_LOG.md`.

---

## T-P2 — Verify one anchor function end to end

**Status: QUEUED** (after T-P1). Depends on T-P1 naming a candidate function.

**Objective.** Take one concrete function and prove, with evidence, that we can
locate it *and* identify it. This validates the entire resolve→verify→hook
pipeline before any feature depends on it.

**Current evidence.** Reference patterns for the same conceptual targets exist
(`Signatures.cpp`), but none is evidence for our build (F-023), and first-match
resolution is a hazard (F-010).

**Unknowns.** Whether a candidate pattern is unique in *our* binary; whether the
address it lands on is the function head; whether the function's identity can be
confirmed structurally.

**Commands** (candidate comes from T-P1; the shape below is the template):

`sig_targets.txt`:

```
fn<0xANCHOR_VA>
```

`zInput.txt`:

```
fn<0xANCHOR_VA>
sig<AA BB ?? ... >
xref_tree<0xANCHOR_VA>
```

Purpose: `fn<>` establishes ground truth (function head, body, terminal shape);
`sig<>` measures **how many times** the pattern occurs (a count, not a hit);
`xref_tree<>` gives caller/callee shape so the structural check (R-2) has
something build-independent to compare against.

**Expected output.** `zaphkiel_out.txt`, `signatures.txt`, `signatures.json`
(the generator's own wildcarding is the reference for how much of the pattern
should be wildcarded — F-018/F-009).

**Decision criteria.**

- `sig<>` count **must be 1**. 0 → pattern tuned to another build. ≥2 → pattern
  is not an identity (this alone would have produced a silent wrong hook under
  the reference's design). Both outcomes go back to T-P1 data, not to guessing.
- The address `sig<>` reports must equal `0xANCHOR_VA`, not merely be "somewhere
  plausible".
- The generated signature must keep at least one contiguous run of ≥8
  fully-specified bytes outside the standard prologue (F-019), otherwise request
  a different anchor region from the same function.

**What is filed afterwards.** `evidence/T-P2/` with `zInput.txt`,
`sig_targets.txt`, all output files, and a `README.md` recording the RVA
conversion of every address.

---

## T-P2.5 — Hook-safety precondition (may merge into T-P2)

**Status: QUEUED.** Depends on T-P2 choosing an anchor.

**Objective.** Satisfy HS-1/HS-2 before we ever patch code: measure how long a
candidate hook target actually is, so the patch cannot spill into its neighbour.
This exists because of a real SIGILL on this build caused by an inline patch
wider than the function (`F-026`).

**Current evidence.** F-026 (crash at lib+`0x10b1da10`, ≥12-byte patch on an
8-byte function; neighbour was load-bearing).

**Unknowns.** Where function boundaries are for candidate hook targets, and
whether the hook engine's patch width is documented anywhere we can rely on.

**Commands** (per candidate, once T-P2 names it):

```
fn<0xANCHOR_VA>
range<0xANCHOR_VA,0xANCHOR_VA+0x40>
```

Purpose: `fn<>`/`range<>` delimit the body so we can count instructions between
entry and the terminating `ret`/`b` — that count is the hard upper bound on what a
patch may overwrite. A candidate whose body is shorter than a plausible patch is
**disqualified as a hook target**, regardless of how cleanly its signature
resolves.

**Decision criteria.** If body length ≤ the hook engine's minimum patch size ⇒ do
not hook it; choose a caller or a longer function instead. Record the measured
length next to the fact in the data file so the rule is auditable later.

**Expected output.** `zaphkiel_out.txt` with the disassembled body.

---

## T-P3 — Feature-specific facts (first Tier-1 feature)

**Status: NOT SPECIFIED.** Tier-0 features need no investigations at all
(`F-024`). The first Tier-3-style investigation is written once the user picks
which game-state readout they want first (`docs/SPEC.md` §7). It will follow the
same format: one question, minimal commands, explicit decision criteria, and an
explicit decay class (D1–D4) per fact.

---

## Standing rules for every request

1. State the decay class (D1–D4) of each fact being sought.
2. Never ask for a sweep when a `findclass<>` or `struct<>` will do (F-016).
3. Never ask for `full_decompile`/`fullvf<>` on a veneer or PLT slot — that
   explodes into linear disassembly of unrelated code.
4. Every request must say what evidence would **disprove** the current working
   hypothesis, not only what would confirm it.
5. Evidence is filed verbatim. Summaries go in `README.md` beside it, never
   instead of it.
