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

class KnuthNodeExeConan(KnuthConanFileV2):
    name = "kth"
    license = "https://opensource.org/licenses/MIT"
    url = "https://github.com/k-nuth/kth"
    description = "Bitcoin full node executable"
    settings = "os", "compiler", "build_type", "arch"
    package_type = "application"

    options = {
        "currency": ['BCH', 'BTC', 'LTC'],
        "no_compilation": [True, False],
        "march_id": ["ANY"],
        "march_strategy": ["download_if_possible", "optimized", "download_or_fail"],
        "verbose": [True, False],
        "mempool": [True, False],
        "db": ['dynamic'],
        "db_readonly": [True, False],
        "cxxflags": ["ANY"],
        "cflags": ["ANY"],
        "cmake_export_compile_commands": [True, False],
        "statistics": [True, False],
    }

    default_options = {
        "currency": "BCH",
        "no_compilation": False,
        "march_strategy": "download_if_possible",
        "verbose": False,
        "mempool": False,
        "db": "dynamic",
        "db_readonly": False,
        "cmake_export_compile_commands": False,
        "statistics": False,
    }

    exports_sources = "CMakeLists.txt", "ci_utils/cmake/*", "cmake/*", "src/*"

    def _is_legacy_db(self):
        return self.options.db == "legacy" or self.options.db == "legacy_full"

    def dont_compile(self, options, settings):
        return options.no_compilation or (settings.compiler == None and settings.arch == 'x86_64' and settings.os in ('Linux', 'Windows', 'Macos'))

    def requirements(self):
        if not self.options.no_compilation and self.settings.get_safe("compiler") is not None:
            self.requires("node/0.58.0", transitive_headers=True, transitive_libs=True)

    def validate(self):
        KnuthConanFileV2.validate(self)
        if self.info.settings.compiler.cppstd:
            check_min_cppstd(self, "23")

    def config_options(self):
        KnuthConanFileV2.config_options(self)

    def configure(self):
        KnuthConanFileV2.configure(self)

        # if self.options.no_compilation or (self.settings.compiler == None and self.settings.arch == 'x86_64' and self.settings.os in ('Linux', 'Windows', 'Macos')):
        if self.dont_compile(self.options, self.settings):
            self.settings.remove("compiler")
            self.settings.remove("build_type")

        self.options["*"].db_readonly = self.options.db_readonly
        self.output.info("Compiling with read-only DB: %s" % (self.options.db_readonly,))

        self.options["*"].mempool = self.options.mempool
        self.output.info("Compiling with mempool: %s" % (self.options.mempool,))

        self.options["*"].statistics = self.options.statistics
        self.output.info("Compiling with statistics: %s" % (self.options.statistics,))

    def package_id(self):
        KnuthConanFileV2.package_id(self)

        if self.dont_compile(self.info.options, self.info.settings):
            self.info.requires.clear()

            # self.output.info("package_id - self.channel: %s" % (self.channel,))
            # self.output.info("package_id - self.options.no_compilation: %s" % (self.options.no_compilation,))
            # self.output.info("package_id - self.settings.compiler: %s" % (self.settings.compiler,))
            # self.output.info("package_id - self.settings.arch: %s" % (self.settings.arch,))
            # self.output.info("package_id - self.settings.os: %s" % (self.settings.os,))
            # self.output.info("package_id - is_development_branch_internal: %s" % (is_development_branch_internal(self.channel),))

            # if not is_development_branch_internal(self.channel):
                # self.info.settings.compiler = "ANY"
                # self.info.settings.build_type = "ANY"
            self.info.settings.compiler = "ANY"
            self.info.settings.build_type = "ANY"

        self.info.options.no_compilation = "ANY"

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = self.cmake_toolchain_basis()
        # tc.variables["CMAKE_VERBOSE_MAKEFILE"] = True
        tc.variables["WITH_MEMPOOL"] = option_on_off(self.options.mempool)
        tc.variables["DB_READONLY_MODE"] = option_on_off(self.options.db_readonly)
        tc.variables["STATISTICS"] = option_on_off(self.options.statistics)
        tc.variables["CONAN_DISABLE_CHECK_COMPILER"] = option_on_off(True)
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
        cmake = CMake(self)
        cmake.install()

    # def package(self):
    #     self.copy("kth.exe", dst="bin", src="bin")  # Windows
    #     self.copy("kth", dst="bin", src="bin")      # Unixes

    def deploy(self):
        self.copy("kth.exe", src="bin")     # copy from current package
        self.copy("kth", src="bin")         # copy from current package
        # self.copy_deps("*.dll") # copy from dependencies
