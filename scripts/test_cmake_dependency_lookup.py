"""Exercise the project's dependency lookups without configuring or compiling it."""

import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class DependencyLookupTests(unittest.TestCase):
    def test_conan_selected_major_versions_are_accepted(self):
        for source, package, version in [
            ("src/database/CMakeLists.txt", "lmdb", "1.0.2"),
            ("src/database/CMakeLists.txt", "lmdb", "0.9.32"),
        ]:
            with self.subTest(package=package, version=version), tempfile.TemporaryDirectory() as directory:
                calls = re.findall(
                    rf"^\s*(find_package\({package}\s+[^)]*\))",
                    (ROOT / source).read_text(),
                    re.MULTILINE,
                )
                self.assertEqual(len(calls), 1)
                path = Path(directory)
                # CMakeDeps version checks can reject a newer major when the
                # consumer requests an obsolete version. Exercise CMake's real
                # lookup with that compatibility policy and the production call.
                (path / f"{package}Config.cmake").write_text(
                    f'set(KTH_LOOKUP_SELECTED "{version}")\n'
                )
                script = path / "lookup.cmake"
                script.write_text(
                    "cmake_minimum_required(VERSION 3.15)\n"
                    "include(CMakePackageConfigHelpers)\n"
                    f'write_basic_package_version_file("{path / (package + "ConfigVersion.cmake")}" '
                    f"VERSION {version} COMPATIBILITY SameMajorVersion ARCH_INDEPENDENT)\n"
                    f'set({package}_DIR "{path}")\n'
                    "set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)\n"
                    + calls[0]
                    + "\n"
                    f'if(NOT KTH_LOOKUP_SELECTED STREQUAL "{version}")\n'
                    '  message(FATAL_ERROR "The selected package was not loaded")\n'
                    "endif()\n"
                )
                result = subprocess.run(
                    [os.environ.get("CMAKE_COMMAND", "cmake"), "-P", str(script)],
                    text=True,
                    capture_output=True,
                    timeout=30,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
