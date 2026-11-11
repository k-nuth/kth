#!/usr/bin/env python3
"""Exercise the workflow's real package commands with a small Conan/CMake project."""
import argparse
import os
from pathlib import Path
import subprocess
import shutil
import tempfile
import yaml


ROOT = Path(__file__).resolve().parents[2]


def run(command, directory, env, **kwargs):
    return subprocess.run(command, cwd=directory, env=env, check=True, **kwargs)


def exercise(workflow):
    document = yaml.safe_load(workflow.read_text())
    steps = next(iter(document["jobs"].values()))["steps"]
    step = next(s for s in steps if s.get("name") == "📦 Validate installed package consumers")
    with tempfile.TemporaryDirectory(prefix="kth-package-check-") as temporary:
        root = Path(temporary)
        env = dict(os.environ, CONAN_HOME=str(root / "conan"),
                   GITHUB_WORKSPACE=str(ROOT), CI_PACKAGE_BUILD_LOG=str(root / "built.log"))
        # Keep the runner's supported compiler versions, without copying its
        # remotes, credentials or binary cache into this disposable Conan home.
        existing = Path(subprocess.check_output(["conan", "config", "home"], text=True).strip())
        (root / "conan").mkdir()
        for name in ("settings.yml", "settings_user.yml"):
            if (existing / name).exists():
                shutil.copy2(existing / name, root / "conan" / name)
        run(["conan", "profile", "detect"], root, env)
        profile = root / "conan/profiles/default"
        detected = dict(line.split("=", 1) for line in profile.read_text().splitlines() if "=" in line)
        settings = yaml.safe_load((root / "conan/settings.yml").read_text())
        compiler, version = detected["compiler"], detected["compiler.version"]
        # A newly installed compiler can precede Conan's bundled settings.
        # Extend only this fixture's version vocabulary, as CI's setup does.
        if version not in map(str, settings["compiler"][compiler]["version"]):
            user = root / "conan/settings_user.yml"
            additions = yaml.safe_load(user.read_text()) if user.exists() else {}
            additions.setdefault("compiler", {}).setdefault(compiler, {}).setdefault("version", []).append(version)
            user.write_text(yaml.safe_dump(additions))
        for name in ("linux-ci-cd", "general-ci-cd"):
            (profile.parent / name).write_text(profile.read_text())
        dependency = root / "dependency"
        dependency.mkdir()
        (dependency / "conanfile.py").write_text('''from conan import ConanFile
class Dependency(ConanFile):
    name = "ci-dependency"
    version = "0.1"
    package_type = "header-library"
    def package_info(self):
        self.cpp_info.includedirs = []
''')
        run(["conan", "create", str(dependency), "--no-remote"], root, env)
        (root / "conanfile.py").write_text('''from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
class Kth(ConanFile):
    name = "kth"
    settings = "os", "arch", "compiler", "build_type"
    options = {"tests": [True, False], "consensus": [True, False], "march_id": ["ANY"]}
    default_options = {"tests": True, "consensus": False, "march_id": "portable"}
    exports_sources = "CMakeLists.txt", "value.cpp", "value.h", "record.cmake"
    def requirements(self):
        self.requires("ci-dependency/0.1")
    def package_id(self):
        self.info.options.tests = "ANY"
    def layout(self):
        cmake_layout(self)
    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["WITH_CONSENSUS"] = bool(self.options.consensus)
        tc.generate()
    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
    def package(self):
        CMake(self).install()
    def package_info(self):
        self.cpp_info.libs = ["kth"]
''')
        (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.22)
project(package_control LANGUAGES CXX)
option(WITH_CONSENSUS "Variant" OFF)
option(ENABLE_TEST "Tests" ON)
add_library(kth STATIC value.cpp)
target_compile_definitions(kth PRIVATE VALUE=$<BOOL:${WITH_CONSENSUS}>)
add_custom_command(TARGET kth POST_BUILD COMMAND ${CMAKE_COMMAND}
    -DLOG=$ENV{CI_PACKAGE_BUILD_LOG} -DVALUE=${WITH_CONSENSUS}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/record.cmake)
install(TARGETS kth ARCHIVE DESTINATION lib)
install(FILES value.h DESTINATION include)
''')
        (root / "record.cmake").write_text('file(APPEND "${LOG}" "${VALUE}\\n")\n')
        (root / "value.cpp").write_text('#include "value.h"\nint value() { return VALUE; }\n')
        (root / "value.h").write_text('int value();\n')
        test = root / "test_package"
        test.mkdir()
        (test / "conanfile.py").write_text('''from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
class Test(ConanFile):
    settings = "os", "arch", "compiler", "build_type"
    generators = "CMakeDeps"
    def requirements(self):
        self.requires(self.tested_reference_str)
    def layout(self):
        cmake_layout(self)
    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["EXPECTED"] = int(bool(self.dependencies["kth"].options.consensus))
        tc.generate()
    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
    def test(self):
        import os
        self.run(os.path.join(self.cpp.build.bindir, "consumer"), env="conanrun")
''')
        (test / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.22)
project(consumer LANGUAGES CXX)
find_package(kth REQUIRED)
add_executable(consumer consumer.cpp)
target_compile_definitions(consumer PRIVATE EXPECTED=${EXPECTED})
target_link_libraries(consumer PRIVATE kth::kth)
''')
        (test / "consumer.cpp").write_text('#include <value.h>\nint main() { return value() == EXPECTED ? 0 : 1; }\n')
        common = ["--version", "ci-smoke", "-o", "consensus=True", "-o", "tests=True"]
        run(["conan", "install", ".", *common, "-of", "build", "--no-remote"], root, env)
        run(["conan", "lock", "create", ".", *common, "--lockfile-out=build/conan.lock", "--no-remote"], root, env)
        run(["cmake", "-S", ".", "-B", "build/build/Release",
             "-DCMAKE_TOOLCHAIN_FILE=generators/conan_toolchain.cmake", "-DCMAKE_BUILD_TYPE=Release"], root, env)
        run(["cmake", "--build", "build/build/Release"], root, env)
        command = step["run"].replace("${{ inputs.build-version }}", "ci-smoke")
        run(["bash", "-e", "-c", command], root, env)
        builds = (root / "built.log").read_text().splitlines()
        if builds != ["ON", "OFF"]:
            raise AssertionError(f"expected one build per ABI, got {builds}")
        # Remove only the disposable fixture's dependency binary. Validation
        # must fail rather than recreating it or using an unrelated root cache.
        run(["conan", "remove", "ci-dependency/0.1:*", "--confirm"], root, env)
        missing = subprocess.run(["bash", "-e", "-c", command], cwd=root, env=env,
                                 capture_output=True, text=True)
        if missing.returncode == 0 or "Missing binary: ci-dependency/0.1" not in missing.stdout + missing.stderr:
            raise AssertionError("missing dependency did not fail for the intended reason:\n" + missing.stdout + missing.stderr)
        if (root / "built.log").read_text().splitlines() != builds:
            raise AssertionError("validation rebuilt after a missing dependency")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workflows", nargs="*", type=Path)
    args = parser.parse_args()
    for workflow in args.workflows or [ROOT / ".github/workflows/build-with-container.yml",
                                     ROOT / ".github/workflows/build-without-container.yml"]:
        exercise(workflow)
    print("Package validation: both consumers ran, one build per ABI, missing binaries cannot rebuild")
