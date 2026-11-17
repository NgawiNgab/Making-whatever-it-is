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

## Reproduce

```bash
./scripts/bootstrap.sh
python3 scripts/measure.py --label local-suite --output reports/metrics.json \
    --directory build --database build -- ./tests/run.sh
```

`reports/metrics.json` and `reports/*.log` are ignored by Git; update this
document only after a real measurement. Numbers depend on compiler cache state,
host CPU, and concurrent load.
