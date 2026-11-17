# scenario-lint

Validate a `baritone-sim` scenario file **without** the C++ engine.

`scenario-lint` is standalone Python 3 (standard library only). It is first-party
code, does not read or link anything under `reference/`, and is therefore the part
of this repository that may be distributed (MIT, see the repository `LICENSE`).
`baritone-sim` itself links the vendored core and stays a local tool until that
code's license provenance is settled — see `docs/LEGAL_SCOPE.md`.

```bash
scenario-lint examples/mine_iron_ore.json
scenario-lint --json examples/*.json
scenario-lint --quiet build/*.json        # exit code only
```

Exit codes: `0` every file is valid, `2` at least one file is invalid or unreadable.

It enforces the same schema as `src/sim/scenario.cpp` and explains the fix for each
problem (`"targets": []` → add a target; ancient debris in the overworld → set
`"dimension": 1`; `min_y = -50` → the nearest multiple of 16). It also warns when no
declared block matches the targets, which usually means the run can only report
`no_target`.

It cannot tell you whether a run will succeed. Only the engine can do that.

The duplication with the C++ schema is deliberate and checked: `tests/test_scenario_lint.py`
runs both lint and `baritone-sim validate` over the shipped examples and over a list of
invalid files, and requires them to agree.
