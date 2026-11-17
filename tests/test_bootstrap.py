"""Regression tests for bootstrap and measurement tooling (stdlib unittest only)."""
from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BOOTSTRAP = ROOT / "scripts" / "bootstrap.sh"
MEASURE = ROOT / "scripts" / "measure.py"


class BootstrapTests(unittest.TestCase):
    def test_pinned_sources_are_checksummed_and_allowlisted(self) -> None:
        text = BOOTSTRAP.read_text(encoding="utf-8")
        urls = re.findall(r"https://[^\s\"']+", text)
        self.assertTrue(urls)
        for url in urls:
            host = url.split("/", 3)[2]
            self.assertIn(host, {"codeload.github.com", "files.pythonhosted.org"}, url)
        for package in ("ENTT_SHA256", "FMT_SHA256", "ZIG_SHA256", "CMAKE_SHA256"):
            self.assertRegex(text, rf'{package}="[0-9a-f]{{64}}"')
        self.assertIn("--check", text)
        self.assertIn("--offline", text)

    def test_bootstrap_check_passes_on_prepared_workspace(self) -> None:
        result = subprocess.run(
            [str(BOOTSTRAP), "--check"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=120,
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("BOOTSTRAP_OK", result.stdout)

    def test_generated_compiler_supports_cxx23_format(self) -> None:
        env_file = ROOT / "third_party" / "env.sh"
        self.assertTrue(env_file.is_file(), "run scripts/bootstrap.sh first")
        zig = ROOT / "third_party/toolchain/ziglang/ziglang/zig"
        self.assertTrue(zig.is_file() and os.access(zig, os.X_OK))
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "probe.cpp"
            binary = Path(tmp) / "probe"
            source.write_text(
                '#include <format>\n#include <cstdio>\n'
                'int main(){std::puts(std::format("{}", 42).c_str());}\n',
                encoding="utf-8",
            )
            env = dict(os.environ)
            env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / "third_party/zig-global-cache")
            env["ZIG_LOCAL_CACHE_DIR"] = str(ROOT / "third_party/zig-local-cache")
            build = subprocess.run(
                [str(zig), "c++", "-std=c++23", str(source), "-o", str(binary)],
                env=env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                timeout=120,
            )
            self.assertEqual(build.returncode, 0, build.stdout)
            run = subprocess.run([str(binary)], text=True, stdout=subprocess.PIPE, timeout=10)
            self.assertEqual(run.stdout.strip(), "42")

    def test_runner_cache_covers_external_headers_and_defaults_to_incremental(self) -> None:
        runner = (ROOT / "tests/run.sh").read_text(encoding="utf-8")
        self.assertIn('${INCREMENTAL:-1}', runner)
        self.assertIn('third_party/entt/src', runner)
        self.assertIn('third_party/fmt/include', runner)
        self.assertIn('preloader-android/include', runner)
        self.assertIn('cksum "$ROOT/tests/run.sh"', runner)


class IdempotenceTests(unittest.TestCase):
    """Bootstrap must be safe to run twice, and offline must never download."""

    def test_check_is_read_only_and_repeatable(self) -> None:
        env_file = ROOT / "third_party" / "env.sh"
        before = {path: path.stat().st_mtime_ns for path in (env_file,)}
        for _ in range(2):
            result = subprocess.run(
                [str(BOOTSTRAP), "--check"],
                cwd=ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                timeout=120,
            )
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertIn("BOOTSTRAP_OK", result.stdout)
        for path, mtime in before.items():
            self.assertEqual(path.stat().st_mtime_ns, mtime, f"--check rewrote {path.name}")

    def test_offline_fails_cleanly_without_cache_and_creates_nothing(self) -> None:
        """--offline with an empty cache must fail, not reach for the network."""
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "third_party"
            env = dict(os.environ)
            env["BOOTSTRAP_ROOT"] = str(root)
            env["BOOTSTRAP_CACHE"] = str(root / "cache")
            Path(env["BOOTSTRAP_CACHE"]).mkdir(parents=True)
            result = subprocess.run(
                [str(BOOTSTRAP), "--offline"],
                cwd=ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                timeout=120,
                env=env,
            )
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("offline", result.stdout.lower())
            self.assertEqual(list(Path(env["BOOTSTRAP_CACHE"]).iterdir()), [],
                             "offline mode created a partial download")
            # Nothing was extracted into the scratch root: an empty toolchain dir is a
            # side effect of mkdir, not an install.
            self.assertEqual(sorted(p.name for p in root.iterdir()), ["cache", "toolchain"])
            self.assertEqual(list((root / "toolchain").iterdir()), [],
                             "offline mode extracted a toolchain from nowhere")

    def test_offline_check_uses_the_cache_without_network(self) -> None:
        """--check on a prepared workspace verifies the pinned archives offline."""
        if not (ROOT / "third_party" / "cache").is_dir():
            self.skipTest("no prepared cache in this workspace")
        result = subprocess.run(
            [str(BOOTSTRAP), "--check", "--offline"],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=180,
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("BOOTSTRAP_OK", result.stdout)

    def test_runner_builds_the_engine_without_cmake(self) -> None:
        runner = (ROOT / "tests" / "run.sh").read_text(encoding="utf-8")
        self.assertIn("build_engine.sh", runner)
        # The harness must link the objects that script produced, not a stale glob
        # that silently matched nothing (that was a real bug: the paths pointed at
        # build/obj/{sim,reference} before anything built them).
        self.assertIn('ENGINE_SIM=(', runner)
        self.assertIn('ENGINE_REF=(', runner)


class MeasureTests(unittest.TestCase):
    def test_measurement_records_exit_cpu_wall_and_rss(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "metrics.json"
            command = [sys.executable, str(MEASURE), "--label", "unit", "--output", str(output),
                       "--", sys.executable, "-c", "print('ok')"]
            result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout)
            report = json.loads(output.read_text(encoding="utf-8"))
            record = report["measurements"][0]
            self.assertEqual(record["exit_code"], 0)
            self.assertGreater(record["wall_seconds"], 0)
            self.assertGreaterEqual(record["cpu_total_seconds"], 0)
            self.assertGreater(record["peak_rss_bytes"], 0)

    def test_measurement_propagates_failure(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "metrics.json"
            command = [sys.executable, str(MEASURE), "--label", "failure", "--output", str(output),
                       "--", sys.executable, "-c", "raise SystemExit(7)"]
            result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=30)
            self.assertEqual(result.returncode, 7, result.stdout)
            record = json.loads(output.read_text(encoding="utf-8"))["measurements"][0]
            self.assertEqual(record["exit_code"], 7)


if __name__ == "__main__":
    unittest.main(verbosity=2)
