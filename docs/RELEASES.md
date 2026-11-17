# Releases

## What may be published

| Candidate | Links `reference/`? | May be released? |
|---|---|---|
| `tools/scenario-lint` (standalone Python) | no | **yes**, MIT |
| `scripts/` (`bootstrap.sh`, `measure.py`, `build*.sh`) | no | yes, MIT |
| `examples/*.json`, `docs/`, `tests/test_*.py` | no | yes, MIT |
| `baritone-sim` (host CLI) | **yes** — vendored core | **no**, not until `reference/` provenance is settled |
| `sim_harness` and the other C++ harnesses | **yes** | **no**, same reason |
| Android mod package (`.levipack`) | **yes** | **no**, and it is not device-verified either |

`reference/` was supplied without a license file; `docs/LEGAL_SCOPE.md` records that
its object code must not be distributed. A binary that links it is not
"first-party tooling", whatever the driver code's own license says.

## How a release is made

1. The whole suite is green: `./tests/run.sh` → `TESTS PASSED`.
2. The artifact is built from a clean checkout of the tagged commit, never from an
   uncommitted `dist/` (which is ignored on purpose).
3. The archive is flat: the executable plus `LICENSE`. No object code, no generated
   `third_party/`, no credentials, no `libminecraftpe.so`, no device dumps.
4. The SHA-256 is computed and published with the asset, and recorded in
   `RELEASE_NOTES.md`.
5. `docs/LEGAL_SCOPE.md` and this table are re-checked before the release is created.

## Published so far

| Tag | Asset | SHA-256 |
|---|---|---|
| `scenario-lint-v1.0.0` | `scenario-lint.tar.gz` | `b36afe91786aea59e35af972f7ee48a270742bf57aef26f08d02d25d7cb99349` |

Rebuild and verify that asset:

```bash
./tools/scenario-lint/build-release.sh            # -> dist/scenario-lint.tar.gz
sha256sum dist/scenario-lint.tar.gz               # compare with the table above
```

`build-release.sh` pins sorted names, a fixed mtime, and root ownership, and streams
tar through `gzip -n`, so the archive is byte-identical across builds on any machine.
`tests/test_scenario_lint.py` builds it twice and compares. An earlier version of this
document claimed reproducibility while using a plain `tar czf`; touching a file changed
the hash, which is how the claim was caught.

## Not done, and why

- No Release was created in this session: creating one publishes a permanent,
  public artifact, and the only candidate was a 5.5 KB Python script whose value
  does not justify an irreversible publish without your confirmation. Everything
  needed is prepared here.
- `baritone-sim` is the actually useful tool and is deliberately withheld: shipping
  it would distribute object code derived from the unlicensed `reference/` tree.
