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
        for backend, enabled in (("jsonrpc", True), ("none", False), ("grpc", False)):
            with self.subTest(backend=backend):
                recipe, _ = self.recipe(backend)
                recipe.requirements()
                refs = {requirement.ref.name for requirement in recipe.requires.values()}
                self.assertEqual("llhttp" in refs, enabled)
                recipe.package_info()
                node = recipe.cpp_info.components["node"]
                self.assertEqual("llhttp::llhttp" in node.requires, enabled)
                self.assertEqual("KTH_WITH_RPC" in node.defines, enabled)

    def test_grpc_pulls_its_own_pinned_dependencies_and_nothing_else_does(self):
        grpc_only = {"grpc", "protobuf", "asio-grpc"}
        for backend in ("jsonrpc", "none", "grpc"):
            with self.subTest(backend=backend):
                recipe, _ = self.recipe(backend)
                recipe.requirements()
                recipe.package_info()
                refs = {r.ref.name: r for r in recipe.requires.values()}
                node = recipe.cpp_info.components["node"]
                self.assertEqual(grpc_only <= set(refs), backend == "grpc")
                self.assertEqual("KTH_WITH_GRPC" in node.defines, backend == "grpc")
                # Consumers of the gRPC headers must see the standalone-asio macros;
                # the other backends' defines stay exactly as they were.
                self.assertEqual("KTH_ASIO_STANDALONE" in node.defines, backend == "grpc")
                self.assertEqual("ASIO_STANDALONE" in node.defines, backend == "grpc")
                self.assertEqual("grpc::grpc++_unsecure" in node.requires, backend == "grpc")
                self.assertEqual("asio-grpc::asio-grpc" in node.requires, backend == "grpc")
                if backend == "grpc":
                    self.assertTrue(refs["grpc"].ref.revision, "grpc must be pinned to a recipe revision")
                    self.assertTrue(refs["asio-grpc"].ref.revision)
                    self.assertTrue(refs["protobuf"].ref.revision)
                    # The kth asio fork must beat asio-grpc's asio/[>=1.17 <2] range.
                    self.assertTrue(refs["asio"].force)
                    self.assertEqual(str(refs["asio"].ref.version), "1.38.3-kth.1")
                    # Existing pins stay put: no openssl is added or overridden here.
                    self.assertNotIn("openssl", refs)
                else:
                    self.assertFalse(refs["asio"].force)

    def test_grpc_build_tools_come_from_the_build_context(self):
        for backend, expected in (("grpc", True), ("jsonrpc", False), ("none", False)):
            with self.subTest(backend=backend):
                recipe, _ = self.recipe(backend)
                tools, tests = [], []
                recipe.tool_requires = tools.append
                recipe.test_requires = tests.append
                recipe.build_requirements()
                self.assertEqual(
                    {"protobuf/<host_version>", "grpc/<host_version>"} <= set(tools), expected)
                self.assertEqual(tests, ["catch2/3.16.1", "nanobench/4.3.11"])

    def test_grpc_configures_asio_backend_and_trims_plugins(self):
        recipe, _ = self.recipe("grpc")
        recipe.configure()
        self.assertEqual(str(recipe.options["asio-grpc/*"].backend), "asio")
        self.assertEqual(str(recipe.options["grpc/*"].python_plugin), "False")

    def test_requirements_are_unique_for_every_backend_and_consensus_value(self):
        # The recipe must not declare a package twice (Conan: "Duplicated
        # requirement"), whatever the combination; grpc+consensus is rejected in
        # configure() before requirements are ever evaluated.
        for backend in ("jsonrpc", "none", "grpc"):
            for consensus in (False, True):
                with self.subTest(backend=backend, consensus=consensus):
                    recipe, _ = self.recipe(backend)
                    recipe.options.consensus = consensus
                    if backend == "grpc" and consensus:
                        with self.assertRaises(ConanInvalidConfiguration):
                            recipe.configure()
                    recipe.requirements()
                    names = [r.ref.name for r in recipe.requires.values()]
                    self.assertEqual(len(names), len(set(names)))
                    self.assertEqual("openssl" in names, consensus)

    def test_grpc_with_consensus_fails_with_the_reason(self):
        recipe, _ = self.recipe("grpc")
        recipe.options.consensus = True
        with mock.patch.object(BuildHelpers, "configure") as base:
            with self.assertRaisesRegex(ConanInvalidConfiguration, "openssl"):
                recipe.configure()
            base.assert_not_called()

    def test_toolchain_passes_backend_name_instead_of_truthiness(self):
        for backend in ("jsonrpc", "none", "grpc"):
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
                self.assertNotIn("KTH_WITH_GRPC", toolchain.variables)

    def test_reserved_backend_fails_before_resolving_dependencies(self):
        for backend in ("capnproto",):
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
                self.assertNotIn("KTH_WITH_GRPC", recipe.cpp_info.components["node"].defines)
                self.assertFalse({"grpc", "protobuf", "asio-grpc"} &
                                 {r.ref.name for r in recipe.requires.values()})

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
        for backend, diagnostic in (("", "Invalid RPC backend"),
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
            ("", ["-DKTH_RPC_BACKEND=grpc"], "grpc;OFF;grpc"),
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

    def test_node_target_selects_grpc_sources_without_json_rpc(self):
        # The real node target configured per backend, with imported placeholders
        # for the gRPC stack and fake generator executables (configure only).
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "gRPCConfig.cmake").write_text(
                "add_library(gRPC::grpc++_unsecure INTERFACE IMPORTED)\n")
            (root / "protobuf-config.cmake").write_text(
                "add_library(protobuf::libprotobuf INTERFACE IMPORTED)\n")
            (root / "asio-grpc-config.cmake").write_text(
                "add_library(asio-grpc::asio-grpc INTERFACE IMPORTED)\n")
            (root / "llhttpConfig.cmake").write_text(
                "add_library(llhttp::llhttp INTERFACE IMPORTED)\n")
            for tool in ("protoc", "grpc_cpp_plugin"):
                (root / tool).write_text("#!/bin/sh\n")
                (root / tool).chmod(0o755)
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
                'file(WRITE "${CMAKE_BINARY_DIR}/selected" "${sources}\\n${links}\\n${defines}\\n'
                '${KTH_RPC_BACKEND}\\n")\n'
            )
            for backend, grpc, json in (("grpc", True, False), ("jsonrpc", False, True),
                                        ("none", False, False), ("grpc", True, False)):
                result = subprocess.run(
                    [os.environ.get("CMAKE_COMMAND", "cmake"), "-S", str(root),
                     "-B", str(root / "build"), f"-DKTH_RPC_BACKEND={backend}",
                     f"-DgRPC_DIR={root}", f"-Dprotobuf_DIR={root}", f"-Dasio-grpc_DIR={root}",
                     f"-Dllhttp_DIR={root}", f"-DKTH_PROTOC={root / 'protoc'}",
                     f"-DKTH_GRPC_CPP_PLUGIN={root / 'grpc_cpp_plugin'}"],
                    capture_output=True, text=True, timeout=60,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                sources, links, defines, selected = (root / "build/selected").read_text().splitlines()
                with self.subTest(backend=backend):
                    for name in ("transport", "chain_handlers", "server"):
                        self.assertEqual(f"src/grpc_rpc/{name}.cpp" in sources, grpc)
                    self.assertEqual("node.grpc.pb.cc" in sources, grpc)
                    self.assertEqual("gRPC::grpc++_unsecure" in links, grpc)
                    self.assertEqual("protobuf::libprotobuf" in links, grpc)
                    self.assertEqual("asio-grpc::asio-grpc" in links, grpc)
                    self.assertEqual("KTH_WITH_GRPC" in defines, grpc)
                    self.assertEqual("KTH_WITH_RPC" in defines, json)
                    self.assertEqual("src/rpc/server.cpp" in sources, json)
                    self.assertEqual("llhttp::llhttp" in links, json)
                    self.assertEqual(selected, backend)

    def test_grpc_configure_fails_clearly_without_generator_tools(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, target in (("gRPC", "gRPC::grpc++_unsecure"), ("protobuf", "protobuf::libprotobuf"),
                                 ("asio-grpc", "asio-grpc::asio-grpc")):
                config = "gRPCConfig.cmake" if name == "gRPC" else f"{name}-config.cmake"
                (root / config).write_text(f"add_library({target} INTERFACE IMPORTED)\n")
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.22)\nproject(selection LANGUAGES C CXX)\n"
                "include(CheckCXXCompilerFlag)\n"
                f'list(APPEND CMAKE_MODULE_PATH "{ROOT / "ci_utils/cmake"}")\n'
                "add_library(blockchain::blockchain INTERFACE IMPORTED)\n"
                "add_library(network::network INTERFACE IMPORTED)\n"
                "set(ENABLE_TEST OFF)\n"
                f'add_subdirectory("{ROOT / "src/node"}" node)\n'
            )
            result = subprocess.run(
                [os.environ.get("CMAKE_COMMAND", "cmake"), "-S", str(root), "-B", str(root / "build"),
                 "-DKTH_RPC_BACKEND=grpc", f"-DgRPC_DIR={root}", f"-Dprotobuf_DIR={root}",
                 f"-Dasio-grpc_DIR={root}", "-DCMAKE_PROGRAM_PATH=/nonexistent",
                 "-DKTH_PROTOC=KTH_PROTOC-NOTFOUND"],
                capture_output=True, text=True, timeout=60,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("KTH_PROTOC", result.stdout + result.stderr)

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
