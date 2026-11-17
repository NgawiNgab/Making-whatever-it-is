# Release notes

## `scenario-lint` v1.0.0 — first distributable artifact

`scenario-lint` validates a `baritone-sim` scenario file without the C++ engine.
Standalone Python 3, standard library only.

Why this and not `baritone-sim`: the engine links the vendored core under
`reference/`, which has no supplied license. `scenario-lint` is first-party code
that never reads or links that tree, so it is the part of this repository that can
be distributed under the repository `LICENSE` (MIT). See `docs/LEGAL_SCOPE.md`.

### Asset

| Asset | Contents |
|---|---|
| `scenario-lint.tar.gz` | `scenario-lint` (executable) and `LICENSE` |

SHA-256 of `scenario-lint.tar.gz`:

```
c429051d34b63bc71c7fcf506fbaae2cd35c931960d4a7cf2f4bee1db0c3aefe
```

```bash
curl -LO https://github.com/NgawiNgab/Making-whatever-it-is/releases/download/scenario-lint-v1.0.0/scenario-lint.tar.gz
sha256sum scenario-lint.tar.gz
tar xzf scenario-lint.tar.gz
./scenario-lint examples/mine_iron_ore.json     # after cloning the repository
```

### What it does

- Enforces the same schema as `src/sim/scenario.cpp` and names the fix for every
  problem: empty `targets`, nether-only blocks in the overworld (set
  `"dimension": 1`), unaligned `min_y`, out-of-range tier/length/ticks, unknown or
  misspelled fields, wrong JSON types.
- `--json` for machine-readable output, `--quiet` for exit-code-only use.
- Exit `0` when every file is valid, `2` when any file is invalid or unreadable.

### What it does not do

- It does not run a simulation. Only `baritone-sim` does that, and that binary is
  not distributed (see above).
- It does not contact the network, read credentials, or modify any file.

### Verification in this repository

- `tests/test_scenario_lint.py` (11 tests) compares lint against the real engine on
  every shipped example and on 13 invalid scenarios; both must agree.
- Full suite at release time: 6 C++ harnesses (120 checks) and 44 Python tests,
  `TESTS PASSED`.
