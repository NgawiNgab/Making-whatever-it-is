"""Tests for tools/scenario-lint (the standalone, distributable validator).

The lint tool duplicates the engine's schema rules on purpose: it must run where the
engine cannot (no C++ toolchain, no reference/ sources). That duplication is only
safe if it is checked, so these tests compare lint against the real engine on the
shipped examples and on a set of invalid files.
"""

import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
LINT = ROOT / "tools" / "scenario-lint" / "scenario_lint.py"
ENGINE = ROOT / "build" / "bin" / "baritone-sim"
EXAMPLES = ROOT / "examples"


def lint(*args, timeout=120):
    return subprocess.run([sys.executable, str(LINT), *map(str, args)],
                          capture_output=True, text=True, timeout=timeout, cwd=str(ROOT))


def write(tmp: str, name: str, payload) -> pathlib.Path:
    path = pathlib.Path(tmp) / name
    path.write_text(json.dumps(payload) if not isinstance(payload, str) else payload,
                    encoding="utf-8")
    return path


MINIMAL = {
    "mode": "mine", "min_y": -64, "max_y": 320, "ground_y": -20,
    "player": {"x": 8.5, "y": -20.0, "z": 8.5},
    "hotbar": [{"name": "minecraft:iron_pickaxe", "tool": "pickaxe", "tier": 3, "speed": 6.0}],
    "blocks": [{"x": 12, "y": -20, "z": 8, "name": "minecraft:iron_ore"}],
    "targets": ["minecraft:iron_ore"], "quantity": 1,
}


class ExampleParityTests(unittest.TestCase):
    def test_every_example_passes_lint(self):
        result = lint(*sorted(EXAMPLES.glob("*.json")))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless(ENGINE.is_file(), "engine binary not built")
    def test_lint_agrees_with_the_engine_on_examples(self):
        for path in sorted(EXAMPLES.glob("*.json")):
            with self.subTest(example=path.name):
                self.assertEqual(lint(path).returncode, 0, path.name)
                run = subprocess.run([str(ENGINE), "validate", str(path)],
                                     capture_output=True, text=True, timeout=180, cwd=str(ROOT))
                self.assertEqual(run.returncode, 0, run.stderr)
                payload = json.loads(run.stdout)
                self.assertTrue(payload["valid"])

    @unittest.skipUnless(ENGINE.is_file(), "engine binary not built")
    def test_engine_and_lint_both_reject_the_same_broken_files(self):
        bad = [
            {"mode": "mine", "targets": []},
            {"mode": "dig"},
            {"mode": "tunnel", "min_y": -50},
            {"mode": "mine", "targets": ["stone"], "search_radius": 65},
            {"mode": "mine", "targets": ["stone"], "length": 300},
            {"mode": "mine", "targets": ["stone"], "dimension": 3},
            {"mode": "mine", "targets": ["ancient_debris"]},
            {"mode": "mine", "targets": ["stone"], "hotbar": [{"name": "x", "tier": 9}]},
            {"mode": "mine", "targets": ["stone"], "hotbar": [{"name": "x", "tool": "laser"}]},
            {"mode": "mine", "targets": ["stone"], "break_ticks": 0},
            {"mode": "mine", "targets": ["stone"], "player": {"reach": 0}},
            {"mode": "mine", "targets": ["stone"], "blocks": [{"x": 1, "y": 999, "z": 1, "name": "stone"}]},
            {"mode": "mine", "targets": ["stone"], "unknown_field": 1},
        ]
        with tempfile.TemporaryDirectory() as tmp:
            for index, payload in enumerate(bad):
                path = write(tmp, f"bad{index}.json", payload)
                with self.subTest(case=index, payload=payload):
                    self.assertNotEqual(lint(path).returncode, 0, "lint accepted a bad scenario")
                    run = subprocess.run([str(ENGINE), "validate", str(path)],
                                         capture_output=True, text=True, timeout=180, cwd=str(ROOT))
                    self.assertNotEqual(run.returncode, 0, "engine accepted a bad scenario")


class LintBehaviourTests(unittest.TestCase):
    def test_minimal_scenario_is_accepted(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = lint(write(tmp, "ok.json", MINIMAL))
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertIn("OK", result.stdout)

    def test_malformed_json_reports_line_and_column(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = write(tmp, "broken.json", '{\n  "mode": "mine",\n}')
            result = lint(path)
            self.assertEqual(result.returncode, 2)
            self.assertIn("line 3", result.stdout)
            self.assertIn("fix:", result.stdout)

    def test_missing_file_is_a_clean_failure(self):
        result = lint("/nope/scenario.json")
        self.assertEqual(result.returncode, 2)
        self.assertIn("cannot read file", result.stdout)

    def test_json_report_is_machine_readable(self):
        result = lint("--json", *sorted(EXAMPLES.glob("*.json")))
        self.assertEqual(result.returncode, 0, result.stdout)
        payload = json.loads(result.stdout)
        self.assertTrue(payload["valid"])
        self.assertEqual(len(payload["files"]), len(list(EXAMPLES.glob("*.json"))))
        for entry in payload["files"]:
            self.assertTrue(entry["valid"])
            self.assertIn(entry["scenario"]["mode"], {"mine", "tunnel"})

    def test_quiet_mode_only_uses_the_exit_code(self):
        good = lint("--quiet", EXAMPLES / "mine_iron_ore.json")
        self.assertEqual((good.returncode, good.stdout), (0, ""))
        with tempfile.TemporaryDirectory() as tmp:
            bad = lint("--quiet", write(tmp, "bad.json", {"mode": "mine"}))
            self.assertEqual((bad.returncode, bad.stdout), (2, ""))

    def test_warns_when_no_declared_block_matches_the_target(self):
        scenario = dict(MINIMAL, blocks=[])
        with tempfile.TemporaryDirectory() as tmp:
            result = lint(write(tmp, "nomatch.json", scenario))
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertIn("warning: no declared blocks match", result.stdout)

    def test_nether_block_in_overworld_explains_the_fix(self):
        scenario = dict(MINIMAL, blocks=[{"x": 12, "y": -20, "z": 8, "name": "minecraft:netherrack"}])
        with tempfile.TemporaryDirectory() as tmp:
            result = lint(write(tmp, "nether.json", scenario))
            self.assertEqual(result.returncode, 2)
            self.assertIn("nether", result.stdout.lower())
            self.assertIn('"dimension": 1', result.stdout)

    def test_short_type_errors_name_the_field(self):
        scenario = dict(MINIMAL, quantity="many")
        with tempfile.TemporaryDirectory() as tmp:
            result = lint(write(tmp, "type.json", scenario))
            self.assertEqual(result.returncode, 2)
            self.assertIn('"quantity"', result.stdout)


class AdversarialInputTests(unittest.TestCase):
    """A validator must never crash. A traceback on bad input is a bug, not a rejection."""

    def test_deeply_nested_json_is_rejected_without_a_traceback(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "deep.json"
            path.write_text("[" * 200_000 + "]" * 200_000, encoding="utf-8")
            result = lint(path)
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertNotIn("Traceback", result.stderr)
            self.assertIn("too deep", result.stdout)

    def test_empty_file_is_a_clean_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "empty.json"
            path.write_text("", encoding="utf-8")
            result = lint(path)
            self.assertEqual(result.returncode, 2)
            self.assertNotIn("Traceback", result.stderr)

    def test_a_directory_is_a_clean_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = lint(tmp)
            self.assertEqual(result.returncode, 2)
            self.assertNotIn("Traceback", result.stderr)

    def test_non_utf8_bytes_are_a_clean_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "bin.json"
            path.write_bytes(b'{"mode": "mine", "name": "' + bytes([0xFF, 0xFE]) + b'"}')
            result = lint(path)
            self.assertEqual(result.returncode, 2)
            self.assertNotIn("Traceback", result.stderr)


class ReleaseAssetTests(unittest.TestCase):
    """The published archive must be reproducible and contain only first-party files."""

    BUILDER = ROOT / "tools" / "scenario-lint" / "build-release.sh"

    def test_asset_is_byte_identical_across_builds(self):
        with tempfile.TemporaryDirectory() as tmp:
            first = pathlib.Path(tmp) / "a.tar.gz"
            second = pathlib.Path(tmp) / "b.tar.gz"
            for target in (first, second):
                result = subprocess.run(["bash", str(self.BUILDER), str(target)],
                                        capture_output=True, text=True, timeout=120, cwd=str(ROOT))
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("RELEASE_ASSET_OK", result.stdout)
            self.assertEqual(first.read_bytes(), second.read_bytes(),
                             "release asset is not reproducible")

    def test_asset_contains_only_the_executable_and_the_license(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = pathlib.Path(tmp) / "res.tar.gz"
            subprocess.run(["bash", str(self.BUILDER), str(target)], check=True,
                           capture_output=True, timeout=120, cwd=str(ROOT))
            listing = subprocess.run(["tar", "tzf", str(target)], capture_output=True,
                                     text=True, check=True).stdout.split()
            self.assertEqual(sorted(listing), ["LICENSE", "scenario-lint"])

    def test_recorded_hash_matches_the_builder(self):
        documented = (ROOT / "docs" / "RELEASES.md").read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as tmp:
            target = pathlib.Path(tmp) / "res.tar.gz"
            subprocess.run(["bash", str(self.BUILDER), str(target)], check=True,
                           capture_output=True, timeout=120, cwd=str(ROOT))
            import hashlib
            digest = hashlib.sha256(target.read_bytes()).hexdigest()
            self.assertIn(digest, documented,
                          "docs/RELEASES.md must record the hash of the current tree")
            # RELEASE_NOTES.md documents the published v1.0.0 asset, which was built from
            # the tag, so it must keep *that* hash even after the tool changes.
            notes = (ROOT / "RELEASE_NOTES.md").read_text(encoding="utf-8")
            self.assertIn("b36afe91786aea59e35af972f7ee48a270742bf57aef26f08d02d25d7cb99349", notes)


if __name__ == "__main__":
    unittest.main(verbosity=2)
