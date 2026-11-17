"""End-to-end tests for the first-party headless CLI (build/bin/baritone-sim).

These exercise the real binary: argument handling, exit codes, file/stdout output,
strict JSON on stdout, and the two scenario kinds. Every test asserts on parsed JSON,
never on log text, so a formatting change cannot hide a behavioural regression.
"""

import json
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BIN = ROOT / "build" / "bin" / "baritone-sim"
EXAMPLES = ROOT / "examples"


def run(*args, timeout=180):
    return subprocess.run(
        [str(BIN), *map(str, args)],
        capture_output=True,
        text=True,
        timeout=timeout,
        cwd=str(ROOT),
    )


@unittest.skipUnless(BIN.is_file(), "build/bin/baritone-sim missing; run scripts/build_engine.sh")
class CliTests(unittest.TestCase):
    def test_version_reports_schema_and_engine(self):
        done = run("version")
        self.assertEqual(done.returncode, 0, done.stderr)
        payload = json.loads(done.stdout)
        self.assertEqual(payload["engine"], "baritone-sim")
        self.assertGreaterEqual(payload["result_schema_version"], 1)
        self.assertIn("license_scope", payload)

    def test_help_is_not_an_error(self):
        done = run("--help")
        self.assertEqual(done.returncode, 0)
        self.assertIn("validate", done.stdout)
        self.assertIn("run", done.stdout)

    def test_unknown_command_exits_2_with_json_error(self):
        done = run("dig")
        self.assertEqual(done.returncode, 2)
        payload = json.loads(done.stderr)
        self.assertIn("unknown command", payload["error"])

    def test_missing_file_exits_2(self):
        done = run("validate", "/nonexistent/scenario.json")
        self.assertEqual(done.returncode, 2)
        self.assertIn("error", json.loads(done.stderr))

    def test_validate_accepts_examples_without_running(self):
        for scenario in sorted(EXAMPLES.glob("*.json")):
            with self.subTest(scenario=scenario.name):
                done = run("validate", scenario)
                self.assertEqual(done.returncode, 0, done.stderr)
                payload = json.loads(done.stdout)
                self.assertTrue(payload["valid"])
                self.assertIn("mode", payload["scenario"])

    def test_validate_rejects_bad_scenario(self):
        with tempfile.TemporaryDirectory() as tmp:
            bad = pathlib.Path(tmp) / "bad.json"
            bad.write_text('{"mode": "mine", "targets": []}', encoding="utf-8")
            done = run("validate", bad)
            self.assertEqual(done.returncode, 2)
            self.assertIn("targets", json.loads(done.stderr)["error"])

    def test_validate_rejects_malformed_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            bad = pathlib.Path(tmp) / "broken.json"
            bad.write_text('{"mode": "mine",', encoding="utf-8")
            done = run("validate", bad)
            self.assertEqual(done.returncode, 2)
            self.assertIn(" at byte ", json.loads(done.stderr)["error"])

    def test_run_mine_example_writes_result_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = pathlib.Path(tmp) / "result.json"
            done = run("run", EXAMPLES / "mine_iron_ore.json", "--output", out)
            self.assertEqual(done.returncode, 0, done.stderr)
            payload = json.loads(out.read_text(encoding="utf-8"))
            self.assertEqual(payload["schema_version"], 1)
            self.assertEqual(payload["mode"], "mine")
            self.assertEqual(payload["status"], "completed")
            self.assertEqual(payload["breaks"][0]["block"], "minecraft:iron_ore")
            self.assertEqual(payload["inventory"]["minecraft:raw_iron"], 1)

    def test_run_tunnel_example_completes(self):
        done = run("run", EXAMPLES / "tunnel_east.json", "--compact")
        self.assertEqual(done.returncode, 0, done.stderr)
        payload = json.loads(done.stdout)
        self.assertEqual(payload["mode"], "tunnel")
        self.assertEqual(payload["status"], "completed")
        self.assertEqual(payload["direction"], "east")
        self.assertGreater(payload["player"]["x"], 8.5)

    def test_tool_insufficient_is_reported_not_faked(self):
        done = run("run", EXAMPLES / "tool_insufficient.json", "--compact")
        # The run itself is a clean, correct result - the scenario asked for a block the
        # hotbar cannot harvest, and the engine says so. It is reported as an unexpected
        # outcome by the exit code (1), so --expect is needed to accept it in CI.
        self.assertEqual(done.returncode, 1, done.stderr)
        payload = json.loads(done.stdout)
        self.assertEqual(payload["status"], "tool_insufficient")
        self.assertTrue(payload["tools"]["insufficient"])
        self.assertGreater(payload["tools"]["required_tier"], payload["tools"]["best_tier"])
        self.assertEqual(payload["breaks"], [])
        self.assertTrue(any("harvest level" in text for text in payload["limitations"]))

    def test_nether_example_uses_dimension_one(self):
        done = run("run", EXAMPLES / "nether_debris.json", "--compact")
        self.assertEqual(done.returncode, 0, done.stderr)
        payload = json.loads(done.stdout)
        self.assertEqual(payload["scenario"]["dimension"], 1)
        self.assertEqual(payload["status"], "completed")
        self.assertFalse(
            [line for line in payload["logs"] if "Nether ore" in line["message"]],
            "a nether scenario must not be told its ore is in the wrong dimension",
        )

    def test_compact_output_is_one_line(self):
        done = run("run", EXAMPLES / "no_target.json", "--compact")
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(len(done.stdout.strip().splitlines()), 1)
        self.assertEqual(json.loads(done.stdout)["status"], "no_target")

    def test_result_has_no_nan_and_is_stable(self):
        first = run("run", EXAMPLES / "mine_iron_ore.json", "--compact")
        second = run("run", EXAMPLES / "mine_iron_ore.json", "--compact")
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertNotIn("NaN", first.stdout)
        a, b = json.loads(first.stdout), json.loads(second.stdout)
        for key in ("status", "ticks", "breaks", "inventory", "counts", "targets"):
            self.assertEqual(a[key], b[key], f"{key} changed between identical runs")
        self.assertTrue(a["determinism"]["deterministic"])
        self.assertEqual(a["determinism"]["plan_wait_timeouts"], 0)
        for key in ("wall_ms", "cpu_ms", "peak_rss_kb"):
            self.assertGreaterEqual(a["resources"][key], 0)

    def test_runtime_output_directory_is_created(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = pathlib.Path(tmp) / "nested" / "dir" / "result.json"
            done = run("run", EXAMPLES / "no_target.json", "--output", out, "--compact")
            self.assertEqual(done.returncode, 0, done.stderr)
            self.assertTrue(out.is_file())


class MultiScenarioTests(unittest.TestCase):
    """Several scenarios in one process, and exit codes automation can trust."""

    def test_validate_accepts_several_files(self):
        done = run("validate", EXAMPLES / "mine_iron_ore.json",
                   EXAMPLES / "nether_debris.json", EXAMPLES / "tunnel_east.json")
        self.assertEqual(done.returncode, 0, done.stderr)
        payload = json.loads(done.stdout)
        self.assertTrue(payload["valid"])
        self.assertEqual(len(payload["files"]), 3)

    def test_validate_reports_the_path_and_stops_on_the_first_bad_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            bad = pathlib.Path(tmp) / "bad.json"
            bad.write_text('{"mode": "mine", "targets": []}', encoding="utf-8")
            done = run("validate", EXAMPLES / "mine_iron_ore.json", bad)
            self.assertEqual(done.returncode, 2)
            self.assertIn("target", done.stderr)

    def test_batch_out_writes_one_named_result_per_scenario(self):
        with tempfile.TemporaryDirectory() as tmp:
            done = run("run", EXAMPLES / "mine_iron_ore.json", EXAMPLES / "nether_debris.json",
                       EXAMPLES / "no_target.json", "--batch-out", tmp, "--compact")
            # no_target counts as expected; every example here is expected to succeed
            self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
            receipts = [json.loads(line) for line in done.stdout.strip().splitlines()]
            self.assertEqual([r["scenario"] for r in receipts],
                             ["mine-iron-ore", "nether-debris", "no-diamond-here"])
            for receipt in receipts:
                written = pathlib.Path(receipt["written"])
                self.assertTrue(written.is_file())
                self.assertEqual(written.parent.name, pathlib.Path(tmp).name)
                self.assertEqual(json.loads(written.read_text())["status"], receipt["status"])

    def test_batch_exit_code_is_one_when_a_run_is_not_expected(self):
        with tempfile.TemporaryDirectory() as tmp:
            done = run("run", EXAMPLES / "tool_insufficient.json", "--batch-out", tmp,
                       "--compact")
            self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
            receipt = json.loads(done.stdout.strip().splitlines()[0])
            self.assertEqual(receipt["status"], "tool_insufficient")
            self.assertFalse(receipt["expected"])

    def test_expect_selects_the_status_and_overrides_the_default(self):
        completed = run("run", EXAMPLES / "mine_iron_ore.json", "--expect", "completed")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        wrong = run("run", EXAMPLES / "mine_iron_ore.json", "--expect", "no_target")
        self.assertEqual(wrong.returncode, 1)
        expected_ok = run("run", EXAMPLES / "no_target.json", "--expect", "no_target")
        self.assertEqual(expected_ok.returncode, 0)

    def test_expect_rejects_an_unknown_status(self):
        done = run("run", EXAMPLES / "mine_iron_ore.json", "--expect", "definitely")
        self.assertEqual(done.returncode, 2)

    def test_scenario_name_cannot_escape_the_batch_directory(self):
        payload = json.loads((EXAMPLES / "mine_iron_ore.json").read_text())
        payload["name"] = "../../../tmp/escaped"
        with tempfile.TemporaryDirectory() as tmp:
            scenario = pathlib.Path(tmp) / "evil.json"
            scenario.write_text(json.dumps(payload), encoding="utf-8")
            out = pathlib.Path(tmp) / "out"
            done = run("run", scenario, "--batch-out", out, "--compact")
            self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
            written = pathlib.Path(json.loads(done.stdout.strip())["written"]).resolve()
            self.assertEqual(written.parent, out.resolve(),
                             "the scenario name escaped the batch output directory")
            self.assertNotIn("..", written.name)
            self.assertFalse(pathlib.Path("/tmp/escaped.json").exists())

    def test_duplicate_names_do_not_overwrite_each_other(self):
        with tempfile.TemporaryDirectory() as tmp:
            scenario = pathlib.Path(tmp) / "one.json"
            scenario.write_text((EXAMPLES / "mine_iron_ore.json").read_text(), encoding="utf-8")
            done = run("run", scenario, scenario, "--batch-out", pathlib.Path(tmp) / "out",
                       "--compact")
            self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
            receipts = [json.loads(line) for line in done.stdout.strip().splitlines()]
            self.assertNotEqual(receipts[0]["written"], receipts[1]["written"])

    def test_batch_out_and_output_are_mutually_exclusive(self):
        with tempfile.TemporaryDirectory() as tmp:
            done = run("run", EXAMPLES / "mine_iron_ore.json",
                       "--batch-out", tmp, "--output", pathlib.Path(tmp) / "r.json")
            self.assertEqual(done.returncode, 2)
            self.assertIn("mutually exclusive", done.stderr)

    def test_output_with_several_scenarios_points_at_batch_out(self):
        done = run("run", EXAMPLES / "mine_iron_ore.json", EXAMPLES / "tunnel_east.json",
                   "--output", "/tmp/should-not-be-written.json")
        self.assertEqual(done.returncode, 2)
        self.assertIn("--batch-out", done.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
