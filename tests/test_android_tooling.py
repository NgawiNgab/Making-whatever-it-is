"""Static checks on the Android build tooling.

There is no NDK and no device in this workspace, so the Android build cannot be
verified here. These tests keep the tooling honest about that: the script must check
its prerequisites, the CMake project must cover the whole mod layer (including the
JNI entry point), and nothing may silently claim a device-verified build.
"""

import os
import pathlib
import re
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "reference" / "build.sh"
MOD_CMAKE = ROOT / "reference" / "baritone-android" / "CMakeLists.txt"
TOP_CMAKE = ROOT / "CMakeLists.txt"


class AndroidScriptTests(unittest.TestCase):
    def test_script_is_executable(self):
        self.assertTrue(os.access(SCRIPT, os.X_OK), "reference/build.sh is not executable")

    def test_script_requires_an_ndk_and_says_so(self):
        result = subprocess.run(
            ["bash", str(SCRIPT)],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            timeout=120,
            env={k: v for k, v in os.environ.items() if k not in {"ANDROID_NDK_HOME"}},
        )
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("ANDROID_NDK_HOME", result.stderr)

    def test_script_points_at_the_top_level_cmake_project(self):
        text = SCRIPT.read_text(encoding="utf-8")
        # The old script compiled `find src` by hand with wrong include paths and never
        # linked baritone_core. It must go through CMake now.
        self.assertIn('-S "$ROOT"', text)
        self.assertIn("--target baritone_package", text)
        self.assertIn("android.toolchain.cmake", text)
        self.assertNotIn("find src -name '*.cpp'", text)

    def test_script_does_not_claim_device_verification(self):
        text = SCRIPT.read_text(encoding="utf-8")
        self.assertIn("NOT device-verified", text)


class AndroidCmakeTests(unittest.TestCase):
    def test_mod_project_globs_every_layered_source(self):
        text = MOD_CMAKE.read_text(encoding="utf-8")
        for pattern in ("src/*.cpp", "src/compat/*.cpp", "src/hooks/*.cpp",
                        "src/platform/*.cpp", "src/sdk/*.cpp"):
            self.assertIn(pattern, text, f"mod sources missing from the glob: {pattern}")
        # src/main.cpp and src/BaritoneMod.cpp are the JNI entry points.
        self.assertIn('"${CMAKE_CURRENT_SOURCE_DIR}/src"', text)

    def test_mod_project_requires_android_and_the_shared_core(self):
        text = MOD_CMAKE.read_text(encoding="utf-8")
        self.assertIn("if(NOT ANDROID)", text)
        self.assertIn("if(NOT TARGET baritone_core)", text)
        self.assertIn("baritone_core", text)

    def test_mod_project_packages_library_and_manifest(self):
        text = MOD_CMAKE.read_text(encoding="utf-8")
        self.assertIn("add_custom_target(baritone_package", text)
        self.assertIn("manifest.json", text)
        self.assertIn(".levipack", text)

    def test_top_level_cmake_only_adds_android_project_under_android(self):
        text = TOP_CMAKE.read_text(encoding="utf-8")
        match = re.search(r"if\(ANDROID\)(.*?)\nendif\(\)", text, re.S)
        self.assertIsNotNone(match, "no Android guard in the top-level CMakeLists")
        self.assertIn("add_subdirectory(reference/baritone-android)", match.group(1))

    def test_host_build_never_defines_android_targets(self):
        """A host configure must not try to link against the Android `log` library."""
        text = TOP_CMAKE.read_text(encoding="utf-8")
        host_part = text.split("if(ANDROID)")[0]
        self.assertNotIn("target_link_libraries(${MOD_LIBRARY_NAME}", host_part)


if __name__ == "__main__":
    unittest.main(verbosity=2)
