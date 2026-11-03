"""Exercise the actual Conan recipe and CMake RPC selection without downloads.

Only kthbuild's unrelated architecture/build helpers are stubbed. Conan's option,
requirement and component models and all recipe methods under test are real.
"""

import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

from conan import ConanFile
from conan.errors import ConanException, ConanInvalidConfiguration

ROOT = Path(__file__).resolve().parents[1]


class BuildHelpers(ConanFile):
    def config_options(self):
        pass

    def configure(self):
        pass


class RpcRecipeTests(unittest.TestCase):
    def recipe(self, backend=None, system="Linux"):
        helpers = SimpleNamespace(
            KnuthConanFileV2=BuildHelpers,
            option_on_off=lambda value: "ON" if value else "OFF",
        )
        with mock.patch.dict(sys.modules, kthbuild=helpers):
            module = runpy.run_path(str(ROOT / "conanfile.py"))
        recipe = module["KthRecipe"]()
        recipe.settings = SimpleNamespace(os=system)
        recipe.cpp_info = recipe.cpp.package
        if backend is not None:
            recipe.options.rpc = backend
        return recipe, module

    def test_default_keeps_jsonrpc(self):
        recipe, _ = self.recipe()
        self.assertEqual(str(recipe.options.rpc), "jsonrpc")

    def test_none_omits_http_dependency_and_consumer_define(self):
        for backend, enabled in (("jsonrpc", True), ("none", False)):
            with self.subTest(backend=backend):
                recipe, _ = self.recipe(backend)
                recipe.requirements()
                refs = {requirement.ref.name for requirement in recipe.requires.values()}
                self.assertEqual("llhttp" in refs, enabled)
                recipe.package_info()
                node = recipe.cpp_info.components["node"]
                self.assertEqual("llhttp::llhttp" in node.requires, enabled)
                self.assertEqual("KTH_WITH_RPC" in node.defines, enabled)

    def test_toolchain_passes_backend_name_instead_of_truthiness(self):
        for backend in ("jsonrpc", "none"):
            with self.subTest(backend=backend):
                recipe, module = self.recipe(backend)
                toolchain = SimpleNamespace(variables={}, generate=mock.Mock())
                with mock.patch.object(BuildHelpers, "cmake_toolchain_basis",
                                       return_value=toolchain, create=True), \
                     mock.patch.dict(module["KthRecipe"].generate.__globals__,
                                     CMakeDeps=mock.Mock()):
                    recipe.generate()
                self.assertEqual(toolchain.variables["KTH_RPC_BACKEND"], backend)
                self.assertNotIn("KTH_WITH_RPC", toolchain.variables)

    def test_future_backends_fail_before_resolving_dependencies(self):
        for backend in ("grpc", "capnproto"):
            with self.subTest(backend=backend):
                recipe, _ = self.recipe(backend)
                with mock.patch.object(BuildHelpers, "configure") as base:
                    with self.assertRaisesRegex(ConanInvalidConfiguration,
                                                backend + ".*not implemented"):
                        recipe.configure()
                    base.assert_not_called()

    def test_emscripten_disables_rpc(self):
        for backend in ("jsonrpc", "none", "grpc", "capnproto"):
            with self.subTest(backend=backend):
                recipe, _ = self.recipe(backend, "Emscripten")
                recipe.config_options()
                self.assertEqual(str(recipe.options.rpc), "none")
                recipe.configure()
                recipe.requirements()
                self.assertNotIn("llhttp", {r.ref.name for r in recipe.requires.values()})
                recipe.package_info()
                self.assertNotIn("KTH_WITH_RPC", recipe.cpp_info.components["node"].defines)

    def test_invalid_or_old_boolean_options_are_rejected(self):
        for backend in ("grcp", "", "True", "False", True, False):
            with self.subTest(backend=backend):
                recipe, _ = self.recipe()
                with self.assertRaises(ConanException):
                    recipe.options.rpc = backend


class RpcCMakeTests(unittest.TestCase):
    def configure(self, *options, build=None):
        if build is None:
            directory = tempfile.TemporaryDirectory()
            self.addCleanup(directory.cleanup)
            build = Path(directory.name)
        result = subprocess.run(
            [os.environ.get("CMAKE_COMMAND", "cmake"), "-S", str(ROOT),
             "-B", str(build), "-DCMAKE_DISABLE_FIND_PACKAGE_gmp=TRUE", *options],
            capture_output=True, text=True, timeout=60,
        )
        return result, build

    def test_future_and_invalid_backends_are_rejected_before_dependencies(self):
        for backend, diagnostic in (("", "Invalid RPC backend"), ("grpc", "not implemented"),
                                    ("capnproto", "not implemented"),
                                    ("grcp", "Invalid RPC backend")):
            with self.subTest(backend=backend):
                result, _ = self.configure("-DKTH_RPC_BACKEND=" + backend)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(diagnostic, result.stdout + result.stderr)
                self.assertNotIn("Knuth: Cryptocurrency:", result.stdout)
                self.assertNotIn("Knuth: Mempool backend:", result.stdout)
                self.assertNotIn("Knuth: RPC backend:", result.stdout)

    def test_default_legacy_normal_variable_and_wasm_selection(self):
        for setup, options, expected in (
            ("", [], "jsonrpc;ON;jsonrpc"),
            ("", ["-DKTH_WITH_RPC=ON"], "jsonrpc;ON;jsonrpc"),
            ('set(KTH_RPC_BACKEND none)\n', [], "none;OFF;none"),
            ("", ["-DCMAKE_SYSTEM_NAME=Emscripten", "-DKTH_RPC_BACKEND=grpc"],
             "none;OFF;grpc"),
        ):
            with self.subTest(setup=setup, options=options), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root / "CMakeLists.txt").write_text(
                    "cmake_minimum_required(VERSION 3.22)\nproject(selection NONE)\n"
                    + setup + f'include("{ROOT / "cmake/KthRpcBackend.cmake"}")\n'
                    'get_property(cached CACHE KTH_RPC_BACKEND PROPERTY VALUE)\n'
                    'file(WRITE "${CMAKE_BINARY_DIR}/selected" '
                    '"${KTH_RPC_BACKEND};${KTH_WITH_RPC};${cached}")\n'
                )
                result = subprocess.run(
                    [os.environ.get("CMAKE_COMMAND", "cmake"), "-S", str(root),
                     "-B", str(root / "build"), *options],
                    capture_output=True, text=True, timeout=30,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual((root / "build/selected").read_text(), expected)

    def test_node_target_tracks_backend_when_reconfigured(self):
        # Configure the real node target with imported dependency placeholders.
        # This checks actual source/link/define selection, not copied CMake logic.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "llhttpConfig.cmake").write_text(
                "add_library(llhttp::llhttp INTERFACE IMPORTED)\n"
            )
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.22)\nproject(selection LANGUAGES C CXX)\n"
                "include(CheckCXXCompilerFlag)\n"
                f'list(APPEND CMAKE_MODULE_PATH "{ROOT / "ci_utils/cmake"}")\n'
                "add_library(blockchain::blockchain INTERFACE IMPORTED)\n"
                "add_library(network::network INTERFACE IMPORTED)\n"
                "set(ENABLE_TEST OFF)\n"
                f'add_subdirectory("{ROOT / "src/node"}" node)\n'
                'get_target_property(sources node SOURCES)\n'
                'get_target_property(links node INTERFACE_LINK_LIBRARIES)\n'
                'get_target_property(defines node INTERFACE_COMPILE_DEFINITIONS)\n'
                'file(WRITE "${CMAKE_BINARY_DIR}/selected" "${sources}\\n${links}\\n${defines}")\n'
            )
            for backend, enabled in (("jsonrpc", True), ("none", False), ("jsonrpc", True)):
                result = subprocess.run(
                    [os.environ.get("CMAKE_COMMAND", "cmake"), "-S", str(root),
                     "-B", str(root / "build"), f"-DKTH_RPC_BACKEND={backend}",
                     f"-Dllhttp_DIR={root}"],
                    capture_output=True, text=True, timeout=60,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                sources, links, defines = (root / "build/selected").read_text().splitlines()
                self.assertEqual("src/rpc/server.cpp" in sources, enabled)
                self.assertEqual("src/rpc/methods.cpp" in sources, enabled)
                self.assertEqual("llhttp::llhttp" in links, enabled)
                self.assertEqual("KTH_WITH_RPC" in defines, enabled)

    def test_backend_switch_overrides_cached_legacy_flag(self):
        # Exercise the production selector in an isolated CMake project; no
        # package stubs or C++ compilation are needed to inspect its output.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.20)\nproject(rpc NONE)\n"
                f'include("{ROOT / "cmake/KthRpcBackend.cmake"}")\n'
                'file(WRITE "${CMAKE_BINARY_DIR}/selected" '
                '"${KTH_RPC_BACKEND};${KTH_WITH_RPC}")\n'
            )
            for options, expected in (
                (["-DKTH_WITH_RPC=OFF"], "none;OFF"),
                (["-DKTH_RPC_BACKEND=jsonrpc"], "jsonrpc;ON"),
                (["-DKTH_RPC_BACKEND=none"], "none;OFF"),
                ([], "none;OFF"),
                (["-DKTH_WITH_RPC=ON"], "none;OFF"),
            ):
                result = subprocess.run(
                    [os.environ.get("CMAKE_COMMAND", "cmake"), "-S", str(root),
                     "-B", str(root / "build"), *options],
                    capture_output=True, text=True, timeout=30,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual((root / "build/selected").read_text(), expected)


if __name__ == "__main__":
    unittest.main()
