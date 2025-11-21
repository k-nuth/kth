# Copyright (c) 2016-2025 Knuth Project developers.
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

class KnuthConsensusConan(KnuthConanFileV2):
    name = "consensus"
    license = "https://opensource.org/licenses/MIT"
    url = "https://github.com/k-nuth/consensus"
    description = "Bitcoin Consensus Library"
    settings = "os", "compiler", "build_type", "arch"
    package_type = "library"

    options = {"shared": [True, False],
               "fPIC": [True, False],
               "tests": [True, False],
               "currency": ['BCH', 'BTC', 'LTC'],
               "march_id": ["ANY"],
               "march_strategy": ["download_if_possible", "optimized", "download_or_fail"],
               "verbose": [True, False],
               "cxxflags": ["ANY"],
               "cflags": ["ANY"],
               "cmake_export_compile_commands": [True, False],
    }

    default_options = {
        "shared": False,
        "fPIC": True,
        "tests": True,
        "currency": "BCH",
        "march_strategy": "download_if_possible",
        "verbose": False,
        "cmake_export_compile_commands": False,
    }

    exports_sources = "src/*", "CMakeLists.txt", "ci_utils/cmake/*", "cmake/*", "knuthbuildinfo.cmake", "include/*", "test/*"

    def build_requirements(self):
        if self.options.tests:
            self.test_requires("catch2/3.7.1")

    def requirements(self):
        self.requires("boost/1.86.0", transitive_headers=True, transitive_libs=True)
        self.requires("openssl/3.4.1", transitive_headers=True, transitive_libs=True)
        self.requires("secp256k1/0.22.0", transitive_headers=True, transitive_libs=True)

        if self.settings.compiler == "msvc" and self.options.currency == 'BCH':
            self.requires("safeint/3.0.28", transitive_headers=True, transitive_libs=True)

    def validate(self):
        KnuthConanFileV2.validate(self)
        if self.info.settings.compiler.cppstd:
            check_min_cppstd(self, "23")

    def config_options(self):
        KnuthConanFileV2.config_options(self)

    def configure(self):
        KnuthConanFileV2.configure(self)

        # "enable_experimental=False", \
        # "enable_endomorphism=False", \
        # "enable_ecmult_static_precomputation=True", \
        # "enable_module_ecdh=False", \
        # "enable_module_schnorr=True", \
        # "enable_module_recovery=True", \
        # "enable_module_multiset=True", \

        # if self.options.log != "boost":
        #     self.options["boost"].without_filesystem = True
        #     self.options["boost"].without_log = True

        if self.settings.os == "Emscripten":
            self.options["boost/*"].header_only = True

        if self.options.currency == 'BCH':
            self.options["secp256k1/*"].enable_module_schnorr = True
        else:
            self.options["secp256k1/*"].enable_module_schnorr = False

    def package_id(self):
        KnuthConanFileV2.package_id(self)

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = self.cmake_toolchain_basis()
        # tc.variables["CMAKE_VERBOSE_MAKEFILE"] = True
        # tc.variables["ENABLE_TEST"] = option_on_off(self.options.with_tests)
        # tc.variables["WITH_JAVA"] = option_on_off(self.options.with_java)
        # tc.variables["WITH_PYTHON"] = option_on_off(self.options.with_python)
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
                # cmake.test(target="tests")

    # def imports(self):
    #     self.copy("*.h", "", "include")

    def package(self):
        cmake = CMake(self)
        cmake.install()
        # rmdir(self, os.path.join(self.package_folder, "lib", "cmake"))
        # rmdir(self, os.path.join(self.package_folder, "lib", "pkgconfig"))
        # rmdir(self, os.path.join(self.package_folder, "res"))
        # rmdir(self, os.path.join(self.package_folder, "share"))

    def package_info(self):
        self.cpp_info.includedirs = ['include']
        self.cpp_info.libs = ["consensus"]
