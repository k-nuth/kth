#
# Copyright (c) 2017 Bitprim developers (see AUTHORS)
#
# This file is part of Bitprim.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

# import os
from conans import ConanFile, CMake

def option_on_off(option):
    return "ON" if option else "OFF"

class BitprimConan(ConanFile):
    name = "bitprim"
    version = "0.1"
    license = "http://www.boost.org/users/license.html"
    url = "https://github.com/bitprim/bitprim"
    description = "Bitcoin Cross-Platform C++ Development Toolkit"
    settings = "os", "compiler", "build_type", "arch"


    options = {"shared": [True, False],
               "fPIC": [True, False],
               "with_tests": [True, False],
               "with_examples": [True, False],
               "with_icu": [True, False],
               "with_png": [True, False],
               "with_litecoin": [True, False],
               "with_qrencode": [True, False],
               "not_use_cpp11_abi": [True, False],
               "enable_benchmark": [True, False],
               "enable_tests": [True, False],
               "enable_openssl_tests": [True, False],
               "enable_experimental": [True, False],
               "enable_endomorphism": [True, False],
               "enable_ecmult_static_precomputation": [True, False],
               "enable_module_ecdh": [True, False],
               "enable_module_schnorr": [True, False],
               "enable_module_recovery": [True, False],               
    }
            #    "with_asm": ['x86_64', 'arm', 'no', 'auto'],
            #    "with_field": ['64bit', '32bit', 'auto'],
            #    "with_scalar": ['64bit', '32bit', 'auto'],
            #    "with_bignum": ['gmp', 'no', 'auto'],


    default_options = "shared=False", \
        "fPIC=True", \
        "with_tests=True", \
        "with_examples=False", \
        "with_icu=False", \
        "with_png=False", \
        "with_litecoin=False", \
        "with_qrencode=False", \
        "not_use_cpp11_abi=False", \
        "enable_benchmark=False", \
        "enable_tests=True", \
        "enable_openssl_tests=False", \
        "enable_experimental=False", \
        "enable_endomorphism=False", \
        "enable_ecmult_static_precomputation=True", \
        "enable_module_ecdh=False", \
        "enable_module_schnorr=False", \
        "enable_module_recovery=True"

        # "with_asm='auto'", \
        # "with_field='auto'", \
        # "with_scalar='auto'"
        # "with_bignum='auto'"



    generators = "cmake"
    exports_sources = "src/*", "CMakeLists.txt", "cmake/*", "bitprim-coreConfig.cmake.in", "include/*", "test/*"
    package_files = "build/lbitprim-core.a"
    build_policy = "missing"

    requires = (("bitprim-conan-boost/1.64.0@bitprim/stable"))

    def requirements(self):
        if self.settings.os == "Linux" or self.settings.os == "Macos":
            self.requires("gmp/6.1.2@bitprim/stable")

    def build(self):
        cmake = CMake(self)
        cmake.definitions["USE_CONAN"] = "ON"
        cmake.definitions["NO_CONAN_AT_ALL"] = "OFF"
        cmake.definitions["CMAKE_VERBOSE_MAKEFILE"] = "ON"
        cmake.definitions["ENABLE_SHARED"] = option_on_off(self.options.shared)
        cmake.definitions["ENABLE_POSITION_INDEPENDENT_CODE"] = option_on_off(self.options.fPIC)
        # cmake.definitions["NOT_USE_CPP11_ABI"] = option_on_off(self.options.not_use_cpp11_abi)
        cmake.definitions["WITH_TESTS"] = option_on_off(self.options.with_tests)
        cmake.definitions["WITH_EXAMPLES"] = option_on_off(self.options.with_examples)
        cmake.definitions["WITH_ICU"] = option_on_off(self.options.with_icu)
        cmake.definitions["WITH_PNG"] = option_on_off(self.options.with_png)
        cmake.definitions["WITH_LITECOIN"] = option_on_off(self.options.with_litecoin)
        cmake.definitions["WITH_QRENCODE"] = option_on_off(self.options.with_qrencode)
        
        # if self.settings.compiler == "gcc":
        #     if float(str(self.settings.compiler.version)) >= 5:
        #         cmake.definitions["_GLIBCXX_USE_CXX11_ABI"] = "1"
        #     else:
        #         cmake.definitions["_GLIBCXX_USE_CXX11_ABI"] = "0"

        if self.settings.compiler == "gcc":
            if float(str(self.settings.compiler.version)) >= 5:
                cmake.definitions["NOT_USE_CPP11_ABI"] = option_on_off(False)
            else:
                cmake.definitions["NOT_USE_CPP11_ABI"] = option_on_off(True)


        # Secp256k1 --------------------------------------------
        cmake.definitions["ENABLE_POSITION_INDEPENDENT_CODE"] = option_on_off(self.options.fPIC)
        cmake.definitions["ENABLE_BENCHMARK"] = option_on_off(self.options.enable_benchmark)
        cmake.definitions["ENABLE_TESTS"] = option_on_off(self.options.enable_tests)
        cmake.definitions["ENABLE_OPENSSL_TESTS"] = option_on_off(self.options.enable_openssl_tests)
        cmake.definitions["ENABLE_EXPERIMENTAL"] = option_on_off(self.options.enable_experimental)
        cmake.definitions["ENABLE_ENDOMORPHISM"] = option_on_off(self.options.enable_endomorphism)
        cmake.definitions["ENABLE_ECMULT_STATIC_PRECOMPUTATION"] = option_on_off(self.options.enable_ecmult_static_precomputation)
        cmake.definitions["ENABLE_MODULE_ECDH"] = option_on_off(self.options.enable_module_ecdh)
        cmake.definitions["ENABLE_MODULE_SCHNORR"] = option_on_off(self.options.enable_module_schnorr)
        cmake.definitions["ENABLE_MODULE_RECOVERY"] = option_on_off(self.options.enable_module_recovery)

        if self.settings.os == "Windows":
            cmake.definitions["WITH_BIGNUM"] = "no"
            if self.settings.compiler == "Visual Studio" and (self.settings.compiler.version != 12):
                cmake.definitions["ENABLE_TESTS"] = "OFF"   #Workaround. test broke MSVC
        else:
            cmake.definitions["WITH_BIGNUM"] = "gmp"

        # cmake.definitions["WITH_ASM"] = option_on_off(self.options.with_asm)
        # cmake.definitions["WITH_FIELD"] = option_on_off(self.options.with_field)
        # cmake.definitions["WITH_SCALAR"] = option_on_off(self.options.with_scalar)
        # cmake.definitions["WITH_BIGNUM"] = option_on_off(self.options.with_bignum)
        # Secp256k1 -------------------------------------------- (END)




        if self.settings.compiler == "gcc":
            if float(str(self.settings.compiler.version)) >= 5:
                cmake.definitions["_GLIBCXX_USE_CXX11_ABI"] = "1"
            else:
                cmake.definitions["_GLIBCXX_USE_CXX11_ABI"] = "0"

        cmake.configure(source_dir=self.conanfile_directory)
        cmake.build()

    def imports(self):
        self.copy("*.h", "", "include")

    def package(self):
        self.copy("*.h", dst="include", src="include")
        self.copy("*.hpp", dst="include", src="include")
        self.copy("*.ipp", dst="include", src="include")
        self.copy("*.lib", dst="lib", keep_path=False)
        self.copy("*.dll", dst="bin", keep_path=False)
        self.copy("*.dylib*", dst="lib", keep_path=False)
        self.copy("*.so", dst="lib", keep_path=False)
        self.copy("*.a", dst="lib", keep_path=False)

    def package_info(self):
        self.cpp_info.includedirs = ['include']
        self.cpp_info.libs = ["bitprim-core"]

        if self.settings.os == "Linux":
            self.cpp_info.libs.append("pthread")