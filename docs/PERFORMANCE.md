# Measured performance

All numbers below were produced by `scripts/measure.py` on 2026-11-17 in this
workspace: Linux x86-64, 2 logical CPUs, 3.8 GiB RAM. Wall time is monotonic;
CPU time is the direct child's `wait4` rusage (including descendants it waited
for); peak RSS is the maximum sampled sum of `/proc` RSS across the live process
tree. `database_bytes` is 0 because the test suite has no persistent database;
the world snapshot is an in-memory C++ object and the generated `build/`
directory was empty at the time of these measurements.

| Label | Command | Wall | CPU | CPU/wall | Peak RSS | Files/DB |
|---|---|---:|---:|---:|---:|---:|
| `baseline-cold` | cold Zig caches + full `INCREMENTAL=0 ./tests/run.sh --clean` | 269.025 s | 462.150 s | 171.8% | 332.56 MiB | `build/` 0 B; DB 0 B |
| `baseline-full` | warm Zig caches + full rebuild | 9.527 s | 6.296 s | 66.1% | 280.35 MiB | `build/` 0 B; DB 0 B |
| `optimized-warm` | incremental object reuse, before Python suites were added | 7.467 s | 2.833 s | 37.9% | 276.38 MiB | `build/` 0 B; DB 0 B |
| `suite-phase1` | complete current suite: 5 C++ harnesses + 6 Python tests | 13.078 s | 8.453 s | 64.6% | 242.64 MiB | `build/` 0 B; DB 0 B |

The cold run is dominated by the first population of Zig's libc++/compiler cache,
not by test execution. CPU/wall above 100% is expected because `tests/run.sh`
compiles with `xargs -P "$(nproc)"`.

## Profiling before optimization

The original runner deliberately rebuilt all 83 core objects on every run after
a stale-object crash. The cold and warm baselines above were measured first. That
showed two separate costs:

1. a one-time cold compiler-cache cost (269 s), and
2. repeated object/link work even when the Zig cache was warm (9.5 s).

Only then was incremental reuse made the default. The guard was strengthened at
the same time: the fingerprint now covers all core/mod sources and headers,
preloader headers, host stubs, EnTT/fmt headers, compiler identity, compilation
flags, and `tests/run.sh` itself. The complete suite is now 13.1 s warm,
including six Python tests that were added after `optimized-warm`. Set
`INCREMENTAL=0` to reproduce the conservative full rebuild.

## Measurement correctness

An early version of `measure.py` used a `RUSAGE_CHILDREN` delta and reported
693 s of CPU in 224 s of wall time on a 2-CPU host, an impossible 309%. It was
replaced with `wait4(WNOHANG)`, whose direct-child rusage includes descendants
waited for without duplicate accounting. A controlled two-process CPU burn then
reported 2.017 s CPU in 1.060 s wall (190.3%), and an incremental test run's
measurement (1.974 s user + 1.139 s system) matched the shell's own `time`
builtin (1.972 s user + 1.139 s system). This is recorded so the earlier invalid
number is not reused.

## Phase 2: headless engine and CLI

Measured after the engine existed, same host and same script. The cold row clears
`build/obj` **and** Zig's global/local caches; clearing only `build/obj` reports
about 1 s because Zig reuses its compiler cache, which is not a cold build.

| Label | Command | Wall | CPU | CPU/wall | Peak RSS | Artifacts |
|---|---|---:|---:|---:|---:|---:|
| `engine-cold` | clear `build/obj` + Zig caches, then `FORCE_REBUILD=1 scripts/build_engine.sh` | 129.920 s | 239.820 s | 184.6% | 301.97 MiB | `build/` 313,859,633 B |
| `engine-warm` | `scripts/build_engine.sh` with warm caches | 1.015 s | 1.766 s | 174.0% | 115.70 MiB | (same tree) |
| `suite-phase2` | complete suite at measure time: 6 C++ harnesses + 19 Python tests | 15.793 s | 9.204 s | 58.3% | 273.38 MiB | (same tree) |
| `suite-phase2b` | after the bootstrap idempotence/offline tests were added: 6 C++ harnesses + 23 Python tests | 23.709 s | 16.709 s | 70.5% | 258.72 MiB | (same tree) |

Single-run CLI cost (`scripts/measure.py --label cli-... -- ./build/bin/baritone-sim
run examples/<x>.json --compact`):

| Scenario | Wall | CPU | Peak RSS |
|---|---:|---:|---:|
| `mine_iron_ore` (1 ore, 12 ticks) | 0.102 s | 0.048 s | 13.09 MiB |
| `tunnel_east` (4-block corridor) | 0.101 s | 0.035 s | 13.04 MiB |
| `tool_insufficient` (refuses, explains) | 0.102 s | 0.080 s | 13.10 MiB |
| `no_target` (60-tick budget, none found) | 0.051 s | 0.006 s | 13.10 MiB |

Notes:

- The engine binary is about 10 MB unstripped and carries the vendored core; the
  `sim_harness` binary is about 46 MB because it is linked without dead-code
  elimination. Neither is shipped; both live in ignored `build/`.
- `database_bytes` is `null` in these rows: the engine has no persistent database.
  Per-run `resources.database_bytes` in the result JSON is `0` for the same reason.
- `world_snapshot_estimate_bytes` in a report (about 1.2 MiB for a 5x5 chunk world at
  `chunk_radius 1`) is an in-memory estimate of allocated chunk sections, not RSS.

### What was optimized, and what was not

Profiling preceded each change; nothing here was tuned by guesswork:

1. The first useful optimization was the object cache in `tests/run.sh` (Phase 1,
   13.1 s warm vs 269 s cold).
2. `scripts/build_engine.sh` reuses `build/obj` with a fingerprint over all core/sim
   sources, headers, compiler identity and flags, which brings a warm engine build
   to about 1 s. `FORCE_REBUILD=1` forces a clean rebuild.
3. No algorithmic optimization was attempted in the pathfinder. The observed cost is
   dominated by process/thread startup and A* on a small world; there is no profile
   showing a hot spot worth changing, so none was changed.

## Phase 2c-2e: dimensions, Android tooling, scenario-lint

| Label | What it measures | Wall | CPU | Peak RSS |
|---|---|---:|---:|---:|
| `suite-phase2e` | full `./tests/run.sh`: 6 C++ harnesses (120 checks) + 47 Python tests | 25.017 s | 17.934 s | 260.26 MiB |

The suite grew from 23 to 47 Python tests (dimension rules, Android tooling guards,
11 scenario-lint tests, 3 release-asset reproducibility tests) while wall time rose
from 23.709 s to 25.017 s: the new tests are cheap next to the C++ rebuild they ride
on. `scenario-lint` itself costs about 0.03 s per file.

The scenario reachability scan is measured separately in `docs/ENGINE.md`
(`search_radius` 64 is about 2.1M blocks and about 0.26 s).

## Reproduce

```bash
./scripts/bootstrap.sh
python3 scripts/measure.py --label local-suite --output reports/metrics.json \
    --directory build --database build -- ./tests/run.sh
```

`reports/metrics.json` and `reports/*.log` are ignored by Git; update this
document only after a real measurement. Numbers depend on compiler cache state,
host CPU, and concurrent load.
