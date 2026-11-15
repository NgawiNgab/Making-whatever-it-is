# Project Charter

This repository started empty (one-line `README.md`, single commit `a4e543b`).
Two decisions determine the structure of everything that follows; both are now
recorded here with their consequences, followed by the decisions that follow from
the references and the work order.

## 1. What are we building? — DECIDED: a feature mod

Decided by the user 2026-11-15: **a feature mod** — a native mod that adds in-game
functionality (HUD/visual/player convenience) delivered as a `.levipack` and
toggled from the LeviLauncher mod menu. Requirement spec: `docs/SPEC.md`.

The alternative directions were considered and are on record so the choice is
auditable:

| Direction | Why not now |
|---|---|
| **Toolkit/SDK as the product** | It is still being built — but *internally*, as the facts layer every feature sits on (`docs/IMPLEMENTATION_PLAN.md` Phase 1). Shipping it alone would produce nothing the user can see. |
| **Single-purpose tool** | Narrowest path, but the user chose a feature mod, and the plan is ordered so the same work stays reusable. |

What that decision *changes*: the feature architecture from the reference
(module lifecycle + registry + event bus + config + menu + draw path) is now
in scope and is reused structurally. What it does **not** change: the
version-durability rules in `docs/CROSS_VERSION.md` still apply to every feature.

A useful consequence, established from the preloader headers (`F-024`): the
launcher provides the whole HUD/UI/input surface, so **Tier-0 features need no
game-binary facts**. That is why the plan can ship something real before T-P1.

## 2. Which build do we target? — DECIDED: 1.26.62 arm64

Decided by the user 2026-11-15: **MCPE 1.26.62, arm64-v8a** — the same build as
their other native-mod work.

Consequences, stated precisely:

- The target is a *version*, not yet a verified *library image*. Evidence is
  bound to one image, so `T-P1` still has to confirm size + segment table (and,
  if obtainable, a hash) before any address or offset is trusted (`F-024`).
- The user's prior 1.26.62 evidence is now a **candidate source**, not a data
  source. If `T-P1` shows the same image, re-verification is cheap; if not, it is
  reference-only. Nothing is inherited silently.
- The reference's declared band (`1.26.6X.X`) does not make its values valid for
  1.26.62 — `F-023` stands unchanged.

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

## 5. How the work proceeds now that §1/§2 are answered

Detail in `docs/IMPLEMENTATION_PLAN.md`; the order is:

1. **Phase 0** — scaffold: lifecycle, config, menu, empty feature set, CI build.
   No game facts. Not started.
2. **Phase 1** — the facts layer (uniqueness, structural check, per-fact report,
   build identity gate). No game facts; uses a synthetic test data file.
3. **Phase 2** — first Tier-0 features (launcher-only; `F-024`). Still no game
   facts. Needs a device to confirm on-screen behaviour.
4. **Phase 3** — first Tier-1 game-state read. **First hard dependency on
   Zaphkiel**: `T-P1` (library instance) → one targeted follow-up for one getter
   → data-file entry → feature gated on it.
5. **Phase 4** — Tier-2 hooks + scale-out, with HS-1..HS-4 enforced.

`T-P1` is READY and queued; the user asked to defer issuance until the product
plan existed, which it now does.

On every game update afterwards: new data file, diff the resolution report,
re-verify only what the diff flags.
