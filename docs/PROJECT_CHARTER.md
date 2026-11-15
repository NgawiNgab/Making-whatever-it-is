# Project Charter

This repository started empty (one-line `README.md`, single commit `a4e543b`).
Before any source code exists, two things have to be decided, because both change
the structure of everything that follows. This file records what is decided, what
is assumed, and what is blocked.

## 1. What are we building? — OPEN

The project instructions define *how* to work (reference discipline, evidence
classification, the Zaphkiel loop) but not *what* the artifact should do. That is
deliberate: instructions are policy, the product is the user's call.

Until this is answered, writing game-facing code would be guessing at the
requirement. What exists instead is the part that does **not** depend on the
answer:

- `docs/REFERENCE_FINDINGS.md` — what the two references actually are, cited.
- `docs/CROSS_VERSION.md` — how we build so a game update is a data change.
- `docs/EVIDENCE_LOG.md` — classified state of knowledge, including what is
  unknown.
- `docs/INVESTIGATIONS.md` — the first Zaphkiel request, ready to issue.

**Needed from the user** (any one is enough to start):
1. A target build (`libminecraftpe.so` version), or confirmation to use the same
   build as their other projects.
2. A feature statement: what should the mod *do*, in one or two sentences.

The three directions differ enough structurally that picking wrong would waste
the work:

| Direction | What it implies | Main risk |
|---|---|---|
| **A. Feature mod** (a UI/player/visual feature) | Reuse the reference's feature architecture: module registry, event bus, config, HUD draw path | Needs D3 offsets early → highest re-verification cost per feature |
| **B. Toolkit/SDK** (the reusable layer: resolver, verifier, version registry, report) | No feature code; the deliverable is the layer that makes A cheap and safe | Value only shows up once something consumes it |
| **C. Single-purpose tool** (e.g. one focused capture/automation task) | Narrowest surface, fastest to evidence, smallest maintenance | Limits reuse later |

My recommendation, stated as a recommendation and not a decision: **B then A**.
The measurements in `docs/REFERENCE_FINDINGS.md` §2 are the reason — the
reference's per-build cost is dominated by exactly the facilities B provides, and
building A first reproduces that cost.

## 2. Which build do we target? — OPEN, blocking

Nothing version-specific can be written until this is answered (Q-001). The
references target a *band* (`1.26.6X.X`), and the user's other work targets a
*specific* build (`1.26.62`), and neither automatically applies here (F-023).

Working assumption if no answer is given: **the user's current device build**, and
every fact is re-verified rather than inherited. This assumption is written down
so it can be corrected, not so it can pass silently.

## 3. What is already decided

These follow from the references and are not really open questions. They are
recorded so the reasoning is auditable.

| # | Decision | Basis |
|---|---|---|
| D-1 | Android arm64-v8a, LeviLauncher/preloader mod, C++20, xmake | F-001, F-003 |
| D-2 | Version-specific facts live in per-build **data files**, never inline in logic | F-008 (~66% sig rows, ~22% offsets churn), F-007 (no dispatch in reference) |
| D-3 | Resolution requires **uniqueness**; duplicates are rejected, not first-matched | F-010 |
| D-4 | Every resolved fact carries **provenance + a structural self-check** | F-014, F-019 |
| D-5 | Features **gate on** the facts they declare; missing facts disable the feature visibly | F-011, F-012 |
| D-6 | Prefer RTTI-name resolution over byte patterns where a type name exists | D1 vs D2 decay |
| D-7 | Field access goes through resolved accessors or validated layouts, not bare offsets | F-013, D3 decay |
| D-8 | We do not copy reference offsets/signatures into code as if verified | F-023 |
| D-9 | Hook install/uninstall must be observable and reversible per hook | F-006 (chain semantics), F-012 |
| D-10 | No code in `src/` may contain a hex literal that belongs to a game build | enforces D-2 |

## 4. What is explicitly out of scope

- Any claim of compatibility with a build that has no data file. We detect and
  report "unsupported" instead (R-5).
- Trial-and-error hooking: writing a hook and seeing whether the game crashes is
  not evidence, and it is how the reference's crash classes arise (F-013, F-014).
- Reusing Zaphkiel output as if it were stable addresses (F-020).
- Merging, releasing, or publishing anything without explicit approval
  (project instruction §6).

## 5. How the work proceeds once §1/§2 are answered

1. Answer `Q-005` (feature) → record the behavioural spec as requirements
   (class CONFIRMED-as-requirements, feasibility separately).
2. Issue `T-P1` → bind the build (`evidence/T-P1/`).
3. Issue `T-P2` → prove the pipeline on one anchor.
4. Implement the layer first (D-2..D-5), then the feature on top of it, with the
   facts it needs resolved as data and gated at runtime.
5. On every game update: new data file, new report diff, re-verify only what the
   diff flags.
