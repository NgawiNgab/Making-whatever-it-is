# Investigation Requests

Rules this file obeys (project instruction §3, §7):

- The agent never claims to have run Zaphkiel. Every result in `evidence/` is
  user-produced and is filed with the run that produced it.
- One investigation answers one question. No broad fishing sweeps.
- Every request states exactly what will change in the code or the plan
  depending on the outcome.
- VAs in results are converted to segment-relative RVAs before being used
  (`docs/CROSS_VERSION.md` §6.1).

Numbering: `T-Pn` (this project). Evidence lives in `evidence/T-Pn/`.

---

## T-P1 — Establish the build identity and mapping facts

**Status: BLOCKED** — waiting for the user to confirm the target build and say
whether this project should target the same device/build as their existing work.
Issuing it blind risks a full run against a binary we cannot use.

**Objective.** Bind this project to a concrete `libminecraftpe.so`: its build
string, size, segment table, text range, and whether RTTI/dynamic symbols are
readable. Everything downstream is invalid without this.

**Current evidence.** Reference band `1.26.6X.X` (F-002). A *different* user
project reports `1.26.62` with `342,231,392` bytes, 7 segments
(`Seed-Taker/evidence/ST-001`). That is **not** evidence about our build (F-023).

**Unknowns.** Build string; exact library size; segment table; text bounds;
RTTI reachability; `.dynsym` presence; the game's own `ClientInstance`/version
symbol names as they appear in *this* binary.

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

## T-P3 — Feature-specific facts (not yet specified)

**Status: NOT SPECIFIED.** Cannot be written until `Q-005` is answered: the
investigation targets are a function of the feature, and the project has no
feature spec yet (`docs/PROJECT_CHARTER.md` §1).

When the feature is known, this investigation will be written in the same format:
one question, minimal commands, explicit decision criteria, and an explicit
statement of which decay class each fact belongs to.

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
