"""End-to-end tests for the first-party headless CLI (build/bin/baritone-sim).

These exercise the real binary: argument handling, exit codes, file/ststdout output,
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
        self.assertEqual(done.returncode, 0, done.stderr)
        payload = json.loads(done.stdout)
        self.assertEqual(payload["status"], "tool_insufficient")
        self.assertTrue(payload["tools"]["insufficient"])
        self.assertGreater(payload["tools"]["required_tier"], payload["tools"]["best_tier"])
        self.assertEqual(payload["breaks"], [])
        self.assertTrue(any("harvest level" in text for text in payload["limitations"]))

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


if __name__ == "__main__":
    unittest.main(verbosity=2)
