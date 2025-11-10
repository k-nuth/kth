# Copyright (c) 2016-2025 Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import os
from conan import ConanFile
from conan.tools.build.cppstd import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy
from kthbuild import KnuthConanFileV2, option_on_off

required_conan_version = ">=2.0"
class KnuthNetworkConan(KnuthConanFileV2):
    name = "network"
    license = "https://opensource.org/licenses/MIT"
    url = "https://github.com/k-nuth/network"
    description = "Crypto P2P Network Library"
    settings = "os", "compiler", "build_type", "arch"
    package_type = "library"

    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "tests": [True, False],
        "currency": ['BCH', 'BTC', 'LTC'],

        "march_id": ["ANY"],
        "march_strategy": ["download_if_possible", "optimized", "download_or_fail"],

        "verbose": [True, False],
        "cxxflags": ["ANY"],
        "cflags": ["ANY"],
        "cmake_export_compile_commands": [True, False],
        "log": ["boost", "spdlog", "binlog"]
    }

    default_options = {
        "shared": False,
        "fPIC": True,
        "tests": True,
        "currency": "BCH",

        "march_strategy": "download_if_possible",

        "verbose": False,
        "cmake_export_compile_commands": False,
        "log": "spdlog",
    }

    exports_sources = "src/*", "CMakeLists.txt", "ci_utils/cmake/*", "cmake/*", "knuthbuildinfo.cmake", "include/*", "test/*"

    def build_requirements(self):
        if self.options.tests:
            self.test_requires("catch2/3.11.0")

    def requirements(self):
        self.requires("domain/0.45.0", transitive_headers=True, transitive_libs=True)

    def validate(self):
        KnuthConanFileV2.validate(self)
        if self.info.settings.compiler.cppstd:
            check_min_cppstd(self, "23")

    def config_options(self):
        KnuthConanFileV2.config_options(self)

    def configure(self):
        KnuthConanFileV2.configure(self)

        #TODO(fernando): move to kthbuild
        self.options["*"].log = self.options.log
        self.output.info("Compiling with log: %s" % (self.options.log,))

    def package_id(self):
        KnuthConanFileV2.package_id(self)

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = self.cmake_toolchain_basis()
        # tc.variables["CMAKE_VERBOSE_MAKEFILE"] = True
        #TODO(fernando): move to kthbuild
        tc.variables["LOG_LIBRARY"] = self.options.log
        tc.variables["CONAN_DISABLE_CHECK_COMPILER"] = option_on_off(True)

        tc.generate()
        tc = CMakeDeps(self)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()

        if not self.options.cmake_export_compile_commands:
            cmake.build()
            if self.options.tests:
                cmake.test()

    def imports(self):
        self.copy("*.h", "", "include")

    def package(self):
        cmake = CMake(self)
        cmake.install()
        # rmdir(self, os.path.join(self.package_folder, "lib", "cmake"))
        # rmdir(self, os.path.join(self.package_folder, "lib", "pkgconfig"))
        # rmdir(self, os.path.join(self.package_folder, "res"))
        # rmdir(self, os.path.join(self.package_folder, "share"))

    def package_info(self):
        self.cpp_info.includedirs = ['include']
        self.cpp_info.libs = ["network"]

        if self.settings.arch == "armv7":
            self.cpp_info.system_libs.append("atomic")
