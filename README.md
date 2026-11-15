# Making-whatever-it-is

Research and implementation groundwork for **Minecraft Bedrock native modding**
(Android, `arm64-v8a`, LeviLauncher/preloader), built under a strict
reference-and-evidence discipline.

This repository is currently at the **evidence-gathering** stage: the references
have been inspected and measured, the cross-version strategy is written down, and
the first reverse-engineering investigation is specified. There is no mod source
yet, on purpose — two open decisions (below) determine its shape, and writing
game-facing code before them would be guesswork.

## Start here

| Document | What it holds |
|---|---|
| [`docs/PROJECT_CHARTER.md`](docs/PROJECT_CHARTER.md) | The two open decisions, the ten settled architecture decisions, scope |
| [`docs/REFERENCE_FINDINGS.md`](docs/REFERENCE_FINDINGS.md) | What BedrockTools and Zaphkiel actually are — every claim cited to path + line |
| [`docs/CROSS_VERSION.md`](docs/CROSS_VERSION.md) | How the project is built so a game update is a *data* change |
| [`docs/EVIDENCE_LOG.md`](docs/EVIDENCE_LOG.md) | Classified knowledge: CONFIRMED / INFERRED / HYPOTHESIS / UNKNOWN |
| [`docs/INVESTIGATIONS.md`](docs/INVESTIGATIONS.md) | Zaphkiel investigation requests, with decision criteria |

## The short version of what the research found

BedrockTools is a good structural reference and a poor model for durability:

- It is **single-build by construction** — no version dispatch anywhere. A game
  update rewrites its tables: on its own `26.52 → 26.60` support commit,
  **100 of 152 signature rows** and **47 of 218 offset constants** changed.
- Its resolution is **first-match with no uniqueness check**, duplicates resolve
  silently to whichever match the scanner reaches first, and `resolveAll` reports
  success if *any* row resolved.
- Its field/vtable accessors are **unchecked dereferences**; hooks install with
  their return values discarded.

Zaphkiel is a user-operated, docs-only tool: it cannot be linked or invoked by
this repository, and its addresses are ASLR-slid live VAs that do not survive a
run. Its signature generator does emit a format the runtime directly accepts,
which is the one confirmed seam between the two references.

So this project keeps the reference's *structure* and replaces its *durability*:
version-coupled facts become per-build data files; resolution must prove
uniqueness and identity; features gate on the facts they declare and fail
visibly. The reasoning and the measurements are in
[`docs/CROSS_VERSION.md`](docs/CROSS_VERSION.md).

## Waiting on two answers

1. **Which build?** A concrete `libminecraftpe.so` build string (or confirmation
   to target the same build as your other projects). Everything version-specific
   is blocked on this — `Q-001`, investigation `T-P1`.
2. **Which product?** A feature mod, a reusable toolkit/SDK layer, or a
   single-purpose tool. Structural consequences are laid out in
   [`docs/PROJECT_CHARTER.md`](docs/PROJECT_CHARTER.md) §1.

## Status of everything buildable

No source, no build, no tests yet. Nothing in this repository has been compiled
or run against a game binary — the environment here has no Android NDK, no
xmake, and no target device, and any claim otherwise would be false. What exists
is verified documentation and a specified investigation.
