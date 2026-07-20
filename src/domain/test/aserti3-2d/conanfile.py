# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import os
from conan import ConanFile
from conan.tools.build.cppstd import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy #, apply_conandata_patches, export_conandata_patches, get, rm, rmdir
from kthbuild import option_on_off, march_conan_manip, pass_march_to_compiler
from kthbuild import KnuthConanFileV2

required_conan_version = ">=2.0"

class AsertTestVectorsGenerator(KnuthConanFileV2):
    name = "asert-gen"
    url = "https://github.com/k-nuth/kth"
    description = "aserti3-2e test vectors generator"
    settings = "os", "compiler", "build_type", "arch"

    options = {
        # "currency": ['BCH', 'BTC', 'LTC'],

        "microarchitecture": ["ANY"],
        "fix_march": [True, False],
        "march_id": ["ANY"],

        "verbose": [True, False],

        "cxxflags": ["ANY"],
        "cflags": ["ANY"],
        "cmake_export_compile_commands": [True, False],
    }

    default_options = {
        # "currency": "BCH",

        "fix_march": False,

        "verbose": False,

        "cmake_export_compile_commands": False,
    }

    # generators = "cmake"
    # exports = "conan_*"
    exports_sources = "CMakeLists.txt", "genenerate_test_vectors.cpp"

    # build_policy = "missing"

    def requirements(self):
        self.requires("domain/0.X@%s/%s" % (self.user, self.channel))

    def config_options(self):
        KnuthConanFileV2.config_options(self)

    def configure(self):
        KnuthConanFileV2.configure(self)
        self.options["*"].currency = "BCH"

    def package_id(self):
        KnuthConanFileV2.package_id(self)

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = self.cmake_toolchain_basis()
        # tc.variables["CMAKE_VERBOSE_MAKEFILE"] = True
        tc.generate()
        tc = CMakeDeps(self)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()

        if not self.options.cmake_export_compile_commands:
            cmake.build()
            # if self.options.tests:
            #     cmake.test()

    def package(self):
        self.copy("generator.exe", dst="bin", src="bin")  # Windows
        self.copy("generator", dst="bin", src="bin")      # Unixes

    def deploy(self):
        self.copy("generator.exe", src="bin")     # copy from current package
        self.copy("generator", src="bin")         # copy from current package
        # self.copy_deps("*.dll") # copy from dependencies
