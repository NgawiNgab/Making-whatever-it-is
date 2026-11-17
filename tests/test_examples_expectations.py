"""Every shipped example must behave exactly as examples/expectations.json says.

Each example is a claim in the docs ("this one completes", "this one is refused").
Without this test, editing a scenario or the engine could quietly change what an
example demonstrates while every other test stayed green.
"""

import json
import pathlib
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ENGINE = ROOT / "build" / "bin" / "baritone-sim"
# Kept out of examples/ on purpose: examples/*.json must stay valid scenarios, so that
# "baritone-sim validate examples/*.json" means what a reader expects.
EXPECTATIONS = ROOT / "tests" / "example_expectations.json"


@unittest.skipUnless(ENGINE.is_file(), "engine binary not built")
class ExampleExpectationTests(unittest.TestCase):
    def setUp(self):
        self.expected = json.loads(EXPECTATIONS.read_text(encoding="utf-8"))

    def test_every_example_has_a_declared_expectation(self):
        shipped = {str(p.relative_to(ROOT)) for p in sorted((ROOT / "examples").glob("*.json"))}
        self.assertEqual(shipped, set(self.expected),
                         "an example was added or removed without updating expectations.json")

    def test_each_example_ends_with_its_expected_status(self):
        for path, status in sorted(self.expected.items()):
            with self.subTest(example=path):
                done = subprocess.run([str(ENGINE), "run", path, "--expect", status, "--compact"],
                                      capture_output=True, text=True, timeout=300, cwd=str(ROOT))
                self.assertEqual(done.returncode, 0,
                                 f"{path} did not end {status}:\n{done.stdout}{done.stderr}")
                payload = json.loads(done.stdout)
                self.assertEqual(payload["status"], status)

    def test_batch_run_over_all_examples_reports_what_expectations_claim(self):
        """One process, one --batch-out: the whole documented example set at once."""
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            paths = sorted(self.expected)
            done = subprocess.run([str(ENGINE), "run", *paths, "--batch-out", tmp, "--compact"],
                                  capture_output=True, text=True, timeout=600, cwd=str(ROOT))
            # tool_insufficient and timeout are not "successful" statuses by default, so the
            # batch exits 1 - that is the documented contract, not a failure of this test.
            self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
            receipts = [json.loads(line) for line in done.stdout.strip().splitlines()]
            self.assertEqual(len(receipts), len(paths))
            for path, receipt in zip(paths, receipts):
                with self.subTest(example=path):
                    self.assertEqual(receipt["status"], self.expected[path])
                    written = json.loads(pathlib.Path(receipt["written"]).read_text())
                    self.assertEqual(written["status"], receipt["status"])
                    self.assertEqual(written["determinism"]["deterministic"], True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
