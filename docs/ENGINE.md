# Headless engine (`baritone-sim`)

`baritone-sim` runs the vendored pathfinder/mining/tunnel core against an in-memory
world and prints an auditable JSON report. It exists so behaviour can be measured on
a desktop instead of guessed at on a phone: no game process, no JNI, no device.

It is **not** a mod and it does not touch a live Minecraft process. See
`docs/LEGAL_SCOPE.md` for what may be distributed.

## Build and run

```bash
./scripts/bootstrap.sh          # pinned Zig + CMake + EnTT + fmt (idempotent)
./scripts/build.sh              # CMake Release build -> build/bin/baritone-sim
./build/bin/baritone-sim version
./build/bin/baritone-sim validate examples/mine_iron_ore.json
./build/bin/baritone-sim run examples/mine_iron_ore.json            # pretty JSON
./build/bin/baritone-sim run examples/tunnel_east.json --compact \
    --output reports/tunnel.json
```

`scripts/build_engine.sh` produces the same binary without CMake (that is what
`tests/run.sh` uses) into `build/bin/baritone-sim` plus `build/obj/{reference,sim}`.

Exit codes: `0` success, `2` usage/parse/validation/file error, `3` runtime error.
Errors are a JSON object on stderr: `{"error", "kind", "position"}`.

## Scenario file

Strict: unknown structures and out-of-range values are rejected, never defaulted
silently. Lengths and quantities are bounded so a typo cannot start a day-long run.

| Field | Meaning | Default |
|---|---|---|
| `mode` | `mine` or `tunnel` | required |
| `name` | label echoed into the report | `scenario` |
| `min_y` / `max_y` | world height range; `min_y` must be a multiple of 16 | `-64` / `320` |
| `ground_y` | top of the generated rock/floor | `-60` |
| `chunk_radius` | extra chunk columns around the scenario | `2` |
| `player` | `{x, y, z, yaw, pitch, reach}` | `8.5, ground_y, 8.5` |
| `hotbar` | up to 36 `{name, count, tool, tier, speed, max_damage}` | empty |
| `inventory` | `{"minecraft:raw_iron": 3, ...}` | empty |
| `blocks` | explicit `{x, y, z, name}` placements | none |
| `targets` | block names to mine (`[]` is invalid for `mine`) | none |
| `quantity` | stop after this many drops; `0` = keep going | `0` |
| `search_radius` | blocks searched around the player for reachability | `12` |
| `length` | tunnel length in blocks (max 256) | `0` (as far as it goes) |
| `direction` | `north`/`south`/`east`/`west`/`facing`/empty | `facing` |
| `max_ticks` | hard tick budget (max 100000) | `500` |
| `tick_delay_ms` | pause per tick, 0-100 | `1` |
| `plan_budget_ms` | max wait for one background path plan | `3000` |
| `break_ticks` | clicks needed per block (1 = instant) | `1` |
| `explore` / `allow_place` / `allow_parkour` | core settings passthrough | `false` |

### Tool tiers

`tier` uses the core's `PlayerContext` scale: `0` hand, `1` wood/gold, `2` stone,
`3` iron, `4` diamond, `5` netherite. Harvest levels come from
`baritone::requiredHarvestTier()` — the same function `InventoryBehavior::breakSpeed`
uses, so the report and the bot cannot disagree.

### Yaw convention

Core yaw follows `baritone/utils/Rotation.h`: `0` = south (+Z), `90` = west (-X),
`180` = north (-Z), `270` = east (+X). `docs/TESTING.md` explains why this matters.

## Report

`schema_version` 1. Top-level keys: `status`, `finished`, `bot_active`, `mode`,
`direction`, `scenario`, `ticks`, `player`, `targets`, `tools`, `breaks`,
`inventory`, `logs`, `counts`, `quality`, `determinism`, `resources`, `limitations`.

`status` values and what each one means:

| Status | Meaning |
|---|---|
| `completed` | the goal was reached or the quantity collected |
| `no_target` | the filter matched nothing the scenario created |
| `tool_insufficient` | the hotbar cannot harvest the target; no drops are possible |
| `blocked` | a target exists but was out of reach / refused |
| `timeout` | `max_ticks` ran out first — not a success |

`quality` reports false-negative/false-positive suspects. `determinism` records
whether every background path plan was awaited (`plan_wait_timeouts == 0`) and is
what makes `ticks`/`breaks` reproducible between identical runs.

## What is simulated, and what is not

Simulated: block hardness and harvest levels, break progress over `break_ticks`,
drops into the inventory, solid-block collision for movement, gravity to the resolved
footing, the core's own `rayTrace` for click targets, real `MineProcess`,
`TunnelProcess`, and `PathingBehavior` (background A* included).

Not simulated: entities and dropped-item pickup, containers, tool durability loss,
Fortune/Silk Touch, random drops, multiplayer, liquids flowing, falling blocks,
bedrock world generation. `limitations` in every report lists the active ones.

## Known findings from real runs

- `TunnelProcess::nearestHorizontal()` had north and south swapped relative to
  `Rotation.h`/`Orientation.cpp`; fixed and covered by `tests/sim_harness.cpp`.
- Tunnel worlds are generated as solid rock with a two-block pocket at the player.
  An earlier version generated air, so a "tunnel" walked through an empty world.
- Planning runs on a thread with a wall-clock budget. The runner waits for each plan
  (`determinism.plan_waits`), which is what makes tick counts reproducible.
