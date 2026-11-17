#!/usr/bin/env python3
"""Validate a baritone-sim scenario file without the C++ engine.

This tool is deliberately standalone: it is first-party code only, has no
dependencies outside the Python standard library, and does not read, link, or
copy anything under reference/. That is why it, and not baritone-sim, is the
distributable artifact (see docs/LEGAL_SCOPE.md).

It checks the same schema the engine enforces and explains the fix for every
problem it finds. It cannot check whether a run succeeds - only the engine can.

    scenario-lint scenario.json
    scenario-lint --json examples/*.json
    scenario-lint --quiet build/*.json      # exit code only
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from typing import Any

SCHEMA_VERSION = 1
TOOLS = {"none", "pickaxe", "axe", "shovel", "hoe", "sword", "shears"}
DIRECTIONS = {"north", "south", "east", "west", "facing", ""}
MODES = {"mine", "tunnel"}
NETHER_ONLY = {
    "minecraft:ancient_debris",
    "minecraft:nether_gold_ore",
    "minecraft:nether_quartz_ore",
    "minecraft:quartz_ore",
    "minecraft:netherrack",
    "minecraft:soul_sand",
    "minecraft:soul_soil",
    "minecraft:magma",
    "minecraft:glowstone",
    "minecraft:basalt",
    "minecraft:blackstone",
}
KNOWN_TOP_LEVEL = {
    "name", "description", "mode", "dimension", "min_y", "max_y", "ground_y",
    "chunk_radius", "player", "hotbar", "inventory", "blocks", "targets",
    "quantity", "search_radius", "length", "direction", "max_ticks",
    "tick_delay_ms", "plan_budget_ms", "break_ticks", "explore", "allow_place",
    "allow_parkour",
}


class Problem(Exception):
    def __init__(self, message: str, fix: str = "") -> None:
        super().__init__(message)
        self.message = message
        self.fix = fix


def normalize_block_name(value: Any) -> str:
    if not isinstance(value, str) or not value.strip():
        raise Problem("block name must be a non-empty string", 'use e.g. "minecraft:iron_ore"')
    name = value.strip().lower()
    if ":" not in name:
        name = "minecraft:" + name
    return name


def integer(obj: dict, key: str, default: int, low: int | None = None,
            high: int | None = None) -> int:
    value = obj.get(key, default)
    if isinstance(value, bool) or not isinstance(value, int):
        raise Problem(f'"{key}" must be an integer, got {type(value).__name__}',
                      f'write {key} without quotes, e.g. "{key}": {default}')
    if low is not None and value < low:
        raise Problem(f'"{key}" is {value}, below the minimum {low}', f"use {key} >= {low}")
    if high is not None and value > high:
        raise Problem(f'"{key}" is {value}, above the maximum {high}', f"use {key} <= {high}")
    return value


def number(obj: dict, key: str, default: float) -> float:
    value = obj.get(key, default)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise Problem(f'"{key}" must be a number', f'write "{key}": {default}')
    if not math.isfinite(value):
        raise Problem(f'"{key}" is not finite', "use a normal number")
    return float(value)


def boolean(obj: dict, key: str, default: bool) -> bool:
    value = obj.get(key, default)
    if not isinstance(value, bool):
        raise Problem(f'"{key}" must be true or false', f'write "{key}": {str(default).lower()}')
    return value


def validate(data: Any) -> dict[str, Any]:
    """Return a summary dict or raise Problem. Mirrors src/sim/scenario.cpp."""
    if not isinstance(data, dict):
        raise Problem("scenario root must be a JSON object",
                      'the file must start with { and contain "mode": ...')

    unknown = sorted(set(data) - KNOWN_TOP_LEVEL)
    if unknown:
        raise Problem(f"unknown field(s): {', '.join(unknown)}",
                      "remove them; the engine rejects unknown fields so typos do not pass silently")

    mode = data.get("mode")
    if mode not in MODES:
        raise Problem(f'"mode" must be "mine" or "tunnel", got {mode!r}',
                      'add "mode": "mine" (or "tunnel")')

    summary: dict[str, Any] = {"mode": mode, "name": str(data.get("name", "scenario"))}
    dimension = integer(data, "dimension", 0, 0, 2)
    summary["dimension"] = dimension

    min_y = integer(data, "min_y", -64)
    max_y = integer(data, "max_y", 320)
    if min_y % 16 != 0:
        raise Problem("min_y must be a multiple of 16 (chunk section layout)",
                      f"use min_y = {min_y - min_y % 16} or {min_y + (16 - min_y % 16) % 16}")
    if max_y <= min_y:
        raise Problem(f"max_y ({max_y}) must be greater than min_y ({min_y})",
                      "use max_y = 320 for a full overworld column")
    ground_y = integer(data, "ground_y", -60)
    if not min_y <= ground_y < max_y:
        raise Problem(f"ground_y ({ground_y}) must be inside [{min_y}, {max_y})",
                      f"pick a ground_y between {min_y} and {max_y - 1}")
    summary.update(min_y=min_y, max_y=max_y, ground_y=ground_y)
    summary["chunk_radius"] = integer(data, "chunk_radius", 2, 0, 8)

    player = data.get("player", {})
    if not isinstance(player, dict):
        raise Problem('"player" must be an object', 'write "player": {"x": 8.5, "y": -60, "z": 8.5}')
    for key in ("x", "y", "z"):
        value = number(player, key, 8.5 if key != "y" else float(ground_y))
        if abs(value) > 1_000_000:
            raise Problem(f"player.{key} is out of range", "keep coordinates under 1,000,000")
    number(player, "yaw", 0.0)
    number(player, "pitch", 0.0)
    reach = number(player, "reach", 4.5)
    if not 0 < reach <= 16:
        raise Problem(f"player.reach must be in (0, 16], got {reach}", "vanilla reach is 4.5")

    hotbar = data.get("hotbar", [])
    if not isinstance(hotbar, list):
        raise Problem('"hotbar" must be a list', 'write "hotbar": [ {...} ]')
    if len(hotbar) > 36:
        raise Problem(f"hotbar has {len(hotbar)} entries", "at most 36 slots (0-8 hotbar, 9-35 inventory)")
    for index, item in enumerate(hotbar):
        if not isinstance(item, dict):
            raise Problem(f"hotbar[{index}] must be an object", "see examples/mine_iron_ore.json")
        normalize_block_name(item.get("name"))
        count = integer(item, "count", 1, 1, 64)
        tier = integer(item, "tier", 0, 0, 5)
        tool = item.get("tool", "none")
        if tool not in TOOLS:
            raise Problem(f'hotbar[{index}].tool {tool!r} is unknown',
                          f"use one of: {', '.join(sorted(TOOLS))}")
        speed = number(item, "speed", 1.0)
        if speed <= 0:
            raise Problem(f"hotbar[{index}].speed must be positive", "use 1.0 for a hand")
        max_damage = integer(item, "max_damage", 0)
        if max_damage < 0:
            raise Problem(f"hotbar[{index}].max_damage must not be negative", "use 0 for no durability")
        if tool != "none" and tier == 0 and max_damage > 0:
            raise Problem(f"hotbar[{index}] has a tool but tier 0",
                          "wood/gold = 1, stone = 2, iron = 3, diamond = 4, netherite = 5")
        summary.setdefault("hotbar", []).append(
            {"name": normalize_block_name(item.get("name")), "count": count, "tool": tool, "tier": tier})
    summary["hotbar_slots"] = len(hotbar)

    inventory = data.get("inventory", {})
    if not isinstance(inventory, dict):
        raise Problem('"inventory" must be an object', 'write "inventory": {"minecraft:raw_iron": 3}')
    for name, amount in inventory.items():
        normalize_block_name(name)
        if isinstance(amount, bool) or not isinstance(amount, int) or not 0 <= amount <= 1_000_000:
            raise Problem(f'inventory["{name}"] must be an integer in [0, 1000000]',
                          "use a whole number of items")

    blocks = data.get("blocks", [])
    if not isinstance(blocks, list):
        raise Problem('"blocks" must be a list', 'write "blocks": [ {"x": 1, "y": 2, "z": 3, "name": "minecraft:stone"} ]')
    names = set()
    for index, block in enumerate(blocks):
        if not isinstance(block, dict):
            raise Problem(f"blocks[{index}] must be an object", "each block needs x, y, z and name")
        for key in ("x", "y", "z", "name"):
            if key not in block:
                raise Problem(f'blocks[{index}] is missing "{key}"',
                              'write {"x": 12, "y": -20, "z": 8, "name": "minecraft:iron_ore"}')
        y = integer(block, "y", 0)
        if not min_y <= y < max_y:
            raise Problem(f"blocks[{index}].y ({y}) is outside [{min_y}, {max_y})",
                          f"use a y between {min_y} and {max_y - 1}")
        name = normalize_block_name(block["name"])
        names.add(name)
        if name in NETHER_ONLY and dimension != 1:
            raise Problem(f"blocks[{index}] is {name}, which only exists in the nether",
                          'add "dimension": 1 (and a nether-appropriate min_y/max_y)')
        for key in ("x", "z"):
            if abs(integer(block, key, 0)) > 1_000_000:
                raise Problem(f"blocks[{index}].{key} is out of range", "keep coordinates under 1,000,000")
    summary["blocks"] = len(blocks)

    targets = data.get("targets", [])
    if not isinstance(targets, list):
        raise Problem('"targets" must be a list of block names', 'write "targets": ["minecraft:iron_ore"]')
    normalized = []
    for target in targets:
        name = normalize_block_name(target)
        if any(ch in name for ch in " \t\r\n[]="):
            raise Problem(f"target {name!r} contains a block state selector",
                          'use a plain block name such as "minecraft:iron_ore"')
        if name in NETHER_ONLY and mode == "mine" and dimension != 1:
            raise Problem(f"target {name} only exists in the nether",
                          'add "dimension": 1 to the scenario')
        normalized.append(name)
    if mode == "mine" and not normalized:
        raise Problem("a mine scenario needs at least one target",
                      'add "targets": ["minecraft:iron_ore"]')
    summary["targets"] = normalized

    summary["quantity"] = integer(data, "quantity", 0, 0, 1_000_000)
    summary["search_radius"] = integer(data, "search_radius", 12, 0, 64)
    summary["length"] = integer(data, "length", 0, 0, 256)
    direction = data.get("direction", "")
    if not isinstance(direction, str) or direction not in DIRECTIONS:
        raise Problem(f'direction {direction!r} is not usable',
                      "use north, south, east, west, facing, or omit it")
    summary["direction"] = direction or "facing"
    summary["max_ticks"] = integer(data, "max_ticks", 500, 1, 100_000)
    summary["tick_delay_ms"] = integer(data, "tick_delay_ms", 1, 0, 100)
    summary["plan_budget_ms"] = integer(data, "plan_budget_ms", 3000, 10, 60_000)
    summary["break_ticks"] = integer(data, "break_ticks", 1, 1, 100)
    summary["explore"] = boolean(data, "explore", False)
    summary["allow_place"] = boolean(data, "allow_place", False)
    summary["allow_parkour"] = boolean(data, "allow_parkour", False)

    if mode == "tunnel" and summary["max_ticks"] < 20:
        summary.setdefault("warnings", []).append(
            "max_ticks is very small for a tunnel; the first path plan alone can take a few ticks")
    reachable = [name for name in summary["targets"] if name in names]
    if mode == "mine" and not reachable and not summary["explore"]:
        summary.setdefault("warnings", []).append(
            "no declared blocks match the targets: the run can only report no_target "
            "unless world generation creates one")
    return summary


def lint_file(path: str) -> tuple[bool, dict[str, Any]]:
    try:
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
    except OSError as error:
        return False, {"file": path, "error": f"cannot read file: {error}", "fix": "check the path"}
    except UnicodeDecodeError as error:
        return False, {"file": path,
                       "error": f"file is not UTF-8 text (byte {error.start})",
                       "fix": "a scenario must be UTF-8 JSON; re-save it as UTF-8"}
    try:
        data = json.loads(text)
    except json.JSONDecodeError as error:
        return False, {"file": path, "error": f"invalid JSON at line {error.lineno}, column {error.colno}: {error.msg}",
                       "fix": "fix the syntax near that position"}
    except RecursionError:
        # json.loads is recursive. A 200k-deep array is not a scenario; say so instead of
        # dying with a traceback. The engine rejects the same file with its own depth cap.
        return False, {"file": path, "error": "JSON nesting is too deep to validate",
                       "fix": "a scenario is a flat object; remove the deeply nested value"}
    try:
        summary = validate(data)
    except Problem as problem:
        return False, {"file": path, "error": problem.message, "fix": problem.fix}
    except Exception as error:  # noqa: BLE001 - a validator must not crash on input
        return False, {"file": path, "error": f"could not validate: {type(error).__name__}: {error}",
                       "fix": "report this as a bug; the file was not judged"}
    return True, {"file": path, "valid": True, "schema_version": SCHEMA_VERSION, "scenario": summary}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="scenario-lint",
        description="Validate baritone-sim scenario files (standalone, first-party).")
    parser.add_argument("files", nargs="+", help="scenario JSON file(s)")
    parser.add_argument("--json", action="store_true", help="machine-readable report on stdout")
    parser.add_argument("--quiet", action="store_true", help="no output; exit code only")
    args = parser.parse_args(argv)

    reports = [lint_file(path) for path in args.files]
    ok = all(valid for valid, _ in reports)

    if not args.quiet:
        if args.json:
            json.dump({"valid": ok, "files": [report for _, report in reports]}, sys.stdout, indent=2)
            sys.stdout.write("\n")
        else:
            for valid, report in reports:
                if valid:
                    scenario = report["scenario"]
                    targets = ",".join(scenario["targets"]) or "(none)"
                    print(f"OK   {report['file']}: {scenario['mode']} "
                          f"dimension={scenario['dimension']} targets={targets} "
                          f"quantity={scenario['quantity']}")
                    for warning in scenario.get("warnings", []):
                        print(f"     warning: {warning}")
                else:
                    print(f"FAIL {report['file']}: {report['error']}")
                    if report.get("fix"):
                        print(f"     fix: {report['fix']}")
    return 0 if ok else 2


if __name__ == "__main__":
    sys.exit(main())
