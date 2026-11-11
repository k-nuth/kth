"""Run the RPC workflow's Conan commands against an offline native package.

The consumer control must install and test the binaries already built by the
job, and the native host/build gRPC graphs must select the same plugin binary.
"""

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / '.github/workflows/rpc-backends.yml'


def step_command(name, backend='none'):
    text = WORKFLOW.read_text()
    step = re.search(r'^      - name: .*' + re.escape(name) + r'\n(.*?)(?=^      - name:|\Z)',
                     text, re.M | re.S)
    if step is None:
        raise AssertionError(f'Missing workflow step: {name}')
    run = re.search(r'^        run: \|\n((?:          .*\n|\n)+)', step[1], re.M)
    if run is None:
        inline = re.search(r'^        run: (.+)$', step[1], re.M)
        if inline is None:
            raise AssertionError(f'Missing run block: {name}')
        return inline[1]
    return '\n'.join(line[10:] for line in run[1].splitlines()).replace(
        '${{ matrix.backend }}', backend).replace('${{ matrix.targets }}', 'kth_node_test')


class RpcCiTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.env = dict(os.environ, CONAN_HOME=str(self.root / 'cache'))
        self.run_command('conan profile detect --force')
        profiles = self.root / 'cache/profiles'
        (profiles / 'linux-ci-cd').write_text('include(default)\n')
        profile = ROOT / '.github/conan/rpc-ci'
        if profile.exists():
            target = self.root / '.github/conan/rpc-ci'
            target.parent.mkdir(parents=True)
            shutil.copyfile(profile, target)
        self.write_package()

    def run_command(self, command, **kwargs):
        result = subprocess.run(['bash', '-e', '-o', 'pipefail', '-c', command],
                                cwd=self.root, env=self.env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs)
        self.assertEqual(result.returncode, 0,
                         f'{command}\n{result.stdout}\n{result.stderr}')
        return result

    def write_package(self):
        (self.root / 'conanfile.py').write_text('''
from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout
import os
class Control(ConanFile):
    name = "kth"
    settings = "os", "compiler", "build_type", "arch"
    options = {"tests": [True, False], "consensus": [True, False],
               "rpc": ["none", "grpc", "capnproto"], "march_id": ["ANY"]}
    default_options = {"tests": True, "consensus": False, "rpc": "none", "march_id": "ANY"}
    exports_sources = "CMakeLists.txt", "*.cpp"
    generators = "CMakeDeps", "CMakeToolchain"
    def layout(self):
        cmake_layout(self)
    def requirements(self):
        if self.options.rpc == "grpc":
            self.requires("grpc/1.84.0")
    def build_requirements(self):
        if self.options.rpc == "grpc":
            self.tool_requires("grpc/<host_version>")
    def configure(self):
        if self.options.rpc == "grpc":
            for plugin in ("csharp", "node", "objective_c", "php", "python", "ruby"):
                setattr(self.options["grpc/*"], plugin + "_plugin", False)
            self.options["grpc/*"].csharp_ext = False
    def build(self):
        if os.getenv("RPC_CI_FORBID_REBUILD"):
            raise RuntimeError("RPC consumer check rebuilt the package")
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
    def package(self):
        CMake(self).install()
    def package_info(self):
        self.cpp_info.libs = ["rpc_ci_control"]
        self.cpp_info.includedirs = []
        if self.options.tests:
            self.cpp_info.defines = ["RPC_CI_TEST_PACKAGE"]
''')
        (self.root / 'CMakeLists.txt').write_text('''
cmake_minimum_required(VERSION 3.23)
project(rpc_ci_control LANGUAGES CXX)
add_library(c-api STATIC value.cpp)
set_target_properties(c-api PROPERTIES OUTPUT_NAME rpc_ci_control)
add_executable(kth_node_test main.cpp)
target_link_libraries(kth_node_test PRIVATE c-api)
add_executable(node-exe main.cpp)
target_link_libraries(node-exe PRIVATE c-api)
install(TARGETS c-api node-exe ARCHIVE DESTINATION lib RUNTIME DESTINATION bin)
''')
        (self.root / 'value.cpp').write_text('int rpc_ci_value() { return 42; }\n')
        (self.root / 'main.cpp').write_text(
            'int rpc_ci_value(); int main() { return rpc_ci_value() == 42 ? 0 : 1; }\n')
        consumer = self.root / 'test_package'
        consumer.mkdir()
        (consumer / 'conanfile.py').write_text('''
from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout
import os
class Consumer(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"
    def requirements(self):
        self.requires(self.tested_reference_str)
    def layout(self):
        cmake_layout(self)
    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
    def test(self):
        self.run(os.path.join(self.cpp.build.bindir, "consumer"), env="conanrun")
''')
        (consumer / 'CMakeLists.txt').write_text('''
cmake_minimum_required(VERSION 3.23)
project(consumer LANGUAGES CXX)
find_package(kth CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE kth::kth)
''')
        (consumer / 'main.cpp').write_text('''
#include <cstdlib>
#include <fstream>
#ifdef RPC_CI_TEST_PACKAGE
#error Installed consumer must use the tests=False package
#endif
int rpc_ci_value();
int main() {
    if (rpc_ci_value() != 42) return 1;
    std::ofstream(std::getenv("RPC_CI_CONSUMER_MARKER")) << rpc_ci_value();
}
''')

    def test_installed_consumer_uses_the_existing_build(self):
        self.run_command(step_command('Install dependencies'))
        self.run_command(step_command('Build'))
        library = self.root / 'build/build/Release/librpc_ci_control.a'
        library_mtime = library.stat().st_mtime_ns
        self.env['RPC_CI_FORBID_REBUILD'] = '1'
        marker = self.root / 'consumer-ran'
        self.env['RPC_CI_CONSUMER_MARKER'] = str(marker)
        self.run_command(step_command('Package consumer check'))
        self.assertEqual(library.stat().st_mtime_ns, library_mtime,
                         'Production reconfiguration recompiled the core library')
        self.assertEqual(marker.read_text(), '42')

        # Round-trip the workflow's archive through a fresh Conan cache, then
        # link and run the installed consumer again without downloading/building.
        archive_dir = self.root / 'archive'
        archive_dir.mkdir()
        self.env['RUNNER_TEMP'] = str(archive_dir)
        self.run_command(step_command('Export dependency archive'))
        self.env['CONAN_HOME'] = str(self.root / 'restored-cache')
        self.run_command(step_command('Import cached dependencies'))
        self.run_command('conan profile detect --force')
        profiles = self.root / 'restored-cache/profiles'
        (profiles / 'linux-ci-cd').write_text('include(default)\n')
        marker.unlink()
        self.run_command(
            'conan test test_package kth/0.0.0 --no-remote '
            '-pr:b .github/conan/rpc-ci -pr:h .github/conan/rpc-ci '
            "-o 'kth/*:march_id=ZLm9Pjh' -o 'kth/*:tests=False' "
            "-o 'kth/*:consensus=False' -o 'kth/*:rpc=none'")
        self.assertEqual(marker.read_text(), '42')

    def test_native_grpc_host_and_build_share_the_plugin_binary(self):
        grpc = self.root / 'grpc'
        grpc.mkdir()
        (grpc / 'conanfile.py').write_text('''
from conan import ConanFile
class Grpc(ConanFile):
    name = "grpc"
    version = "1.84.0"
    settings = "os", "compiler", "build_type", "arch"
    options = {name + "_plugin": [True, False] for name in
               ("csharp", "node", "objective_c", "php", "python", "ruby")}
    options["csharp_ext"] = [True, False]
    default_options = {name: True for name in options}
''')
        self.run_command('conan export grpc')
        command = step_command('Install dependencies', 'grpc')
        command = command.replace('conan install', 'conan graph info').replace(
            '--build=missing', '--format=json --no-remote').replace(' -of build', '')
        graph = json.loads(self.run_command(command).stdout)['graph']['nodes']
        grpc_nodes = [node for node in graph.values() if node.get('ref', '').startswith('grpc/')]
        self.assertEqual({node['context'] for node in grpc_nodes}, {'host', 'build'})
        self.assertEqual(len({node['package_id'] for node in grpc_nodes}), 1,
                         'Native RPC CI would compile two different gRPC binaries')


if __name__ == '__main__':
    unittest.main()
