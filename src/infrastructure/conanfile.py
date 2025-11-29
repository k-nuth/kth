# Copyright (c) 2016-2025 Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import os
from conan.tools.build.cppstd import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, cmake_layout
from conan.tools.files import copy
from kthbuild import KnuthConanFileV2, option_on_off

required_conan_version = ">=2.0"

class KnuthInfrastructureConan(KnuthConanFileV2):
    name = "infrastructure"
    license = "https://opensource.org/licenses/MIT"
    url = "https://github.com/knuth/infrastructure"
    description = "Multicrypto Cross-Platform C++ Development Toolkit"
    settings = "os", "compiler", "build_type", "arch"
    package_type = "library"

    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "with_icu": [True, False],
        "with_png": [True, False],
        "with_qrencode": [True, False],
        "tests": [True, False],
        "examples": [True, False],

        "march_id": ["ANY"],
        "march_strategy": ["download_if_possible", "optimized", "download_or_fail"],

        "verbose": [True, False],
        "cxxflags": ["ANY"],
        "cflags": ["ANY"],
        "cmake_export_compile_commands": [True, False],
        "asio_standalone": [True, False],
    }

    default_options = {
        "shared": False,
        "fPIC": True,
        "with_icu": False,
        "with_png": False,
        "with_qrencode": False,
        "tests": True,
        "examples": False,

        "march_strategy": "download_if_possible",

        "verbose": False,
        "cmake_export_compile_commands": False,
        "asio_standalone": False,
    }

    exports_sources = "src/*", "CMakeLists.txt", "ci_utils/cmake/*", "cmake/*", "include/*", "test/*", "examples/*"

    def build_requirements(self):
        if self.options.tests:
            self.test_requires("catch2/3.11.0")

    def requirements(self):
        self.requires("secp256k1/0.22.0", transitive_headers=True, transitive_libs=True)
        self.requires("boost/1.89.0", transitive_headers=True, transitive_libs=True)
        self.requires("fmt/11.1.3", transitive_headers=True, transitive_libs=True)
        self.requires("expected-lite/0.8.0", transitive_headers=True, transitive_libs=True)
        self.requires("ctre/3.9.0", transitive_headers=True, transitive_libs=True)

        self.requires("spdlog/1.15.1", transitive_headers=True, transitive_libs=True)

        if self.options.with_png:
            self.requires("libpng/1.6.40", transitive_headers=True, transitive_libs=True)

        if self.options.with_qrencode:
            self.requires("libqrencode/4.1.1", transitive_headers=True, transitive_libs=True)

        if self.options.asio_standalone:
            self.requires("asio/1.28.1", transitive_headers=True, transitive_libs=True)

        if self.options.with_icu:
            self.requires("icu/76.1", transitive_headers=True, transitive_libs=True)

    def validate(self):
        KnuthConanFileV2.validate(self)
        if self.info.settings.compiler.cppstd:
            check_min_cppstd(self, "23")

    def config_options(self):
        KnuthConanFileV2.config_options(self)

    def configure(self):
        # self.output.info("libcxx: %s" % (str(self.settings.compiler.libcxx),))
        KnuthConanFileV2.configure(self)

        self.options["fmt/*"].header_only = True

        if self.settings.os == "Emscripten":
            self.options["boost/*"].header_only = True

        self.options["spdlog/*"].header_only = True

    def package_id(self):
        KnuthConanFileV2.package_id(self)

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = self.cmake_toolchain_basis()
        # tc.variables["CMAKE_VERBOSE_MAKEFILE"] = True
        tc.variables["WITH_ICU"] = option_on_off(self.options.with_icu)
        # tc.variables["WITH_QRENCODE"] = option_on_off(self.options.with_qrencode)
        # tc.variables["WITH_PNG"] = option_on_off(self.options.with_qrencode)
        tc.variables["WITH_QRENCODE"] = option_on_off(self.options.with_qrencode)
        tc.variables["WITH_PNG"] = option_on_off(self.options.with_png)
        tc.variables["CONAN_DISABLE_CHECK_COMPILER"] = option_on_off(True)
        tc.generate()
        tc = CMakeDeps(self)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()

        if not self.options.cmake_export_compile_commands:
            cmake.build()
            #Note: Cmake Tests and Visual Studio doesn't work
            if self.options.tests:
                cmake.test()
                # cmake.test(target="tests")

    # def imports(self):
    #     self.copy("*.h", "", "include")

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.includedirs = ['include']
        self.cpp_info.libs = ["infrastructure"]
        ...
        # self.cpp_info.requires = ["boost::program_options",
        #                           "boost::thread",
        #                           "secp256k1::secp256k1",
        #                           "spdlog::spdlog",
        #                           "fmt::fmt",
        #                           "expected-lite::expected-lite",
        #                           "ctre::ctre"
        #                          ]

        self.cpp_info.requires = ["secp256k1::secp256k1",
                                  "spdlog::spdlog",
                                  "fmt::fmt",
                                  "expected-lite::expected-lite",
                                  "ctre::ctre"
                                 ]

        if self.settings.os != "Emscripten":
            self.cpp_info.requires.append("boost::program_options")
            self.cpp_info.requires.append("boost::thread")
        else:
            self.cpp_info.requires.append("boost::boost")

        #TODO(fernando): add the rest of the conditional dependencies


        if self.settings.os == "Linux" or self.settings.os == "FreeBSD" or self.settings.os == "Emscripten":
            self.cpp_info.system_libs.append("pthread")

        if self.settings.os == "Linux" and self.settings.compiler == "gcc" and float(str(self.settings.compiler.version)) <= 8:
            self.cpp_info.system_libs.append("stdc++fs")

        if self.settings.os == "Windows" and self.settings.compiler == "gcc": # MinGW
            self.cpp_info.system_libs.append("ws2_32")
            self.cpp_info.system_libs.append("wsock32")

        if not self.is_shared:
            self.cpp_info.defines.append("KI_STATIC")

