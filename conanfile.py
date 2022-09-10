# Copyright (c) 2016-2021 Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import os
from conans import ConanFile, CMake
from conans import __version__ as conan_version
from conans.model.version import Version

def option_on_off(option):
    return "ON" if option else "OFF"

def get_content(file_name):
    file_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), file_name)
    with open(file_path, 'r') as f:
        return f.read().replace('\n', '').replace('\r', '')

def get_version():
    return get_content('conan_version')

def get_channel():
    return get_content('conan_channel')

def get_conan_req_version():
    return get_content('conan_req_version')

class KnuthConan(ConanFile):
    name = "kth"
    # version = get_version()
    license = "http://www.boost.org/users/license.html"
    url = "https://github.com/k-nuth/kth"
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
               "with_rpc": [True, False],
               "currency": ['BCH', 'BTC', 'LTC']
    }
            #    "with_asm": ['x86_64', 'arm', 'no', 'auto'],
            #    "with_field": ['64bit', '32bit', 'auto'],
            #    "with_scalar": ['64bit', '32bit', 'auto'],
            #    "with_bignum": ['gmp', 'no', 'auto'],


    default_options = "shared=False", \
        "fPIC=True", \
        "with_tests=False", \
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
        "enable_module_recovery=True", \
        "with_rpc=False", \
        "currency=BCH"

        # "with_asm='auto'", \
        # "with_field='auto'", \
        # "with_scalar='auto'"
        # "with_bignum='auto'"

    generators = "cmake"
    exports_sources = "src/*", "CMakeLists.txt", "cmake/*", "kth-coreConfig.cmake.in", "include/*", "test/*"
    package_files = "build/lkth-core.a"
    build_policy = "missing"

    def requirements(self):
        self.requires("boost/1.79.0")
        self.requires("lmdb/0.9.29")
        self.requires("fmt/8.1.1")
        self.requires("ctre/3.7")
        self.requires("range-v3/0.12.0")
        # self.requires("libmdbx/0.7.0@kth/stable")
        # self.requires("binlog/2020.02.29@kth/stable")
        # self.requires("binlog/2020.02.29@kth/stable")
        self.requires("spdlog/1.10.0")
        self.requires("algorithm/0.1.239@tao/stable")

        if self.settings.os == "Linux" or self.settings.os == "Macos":
            self.requires("gmp/6.2.1")
        if self.options.with_rpc:
            self.requires("libzmq/4.2.2@kth/stable")
        if self.options.currency == "LTC":
             self.requires("OpenSSL/1.0.2l@conan/stable")

    def validate(self):
        if self.settings.os == "Linux" and self.settings.compiler == "gcc" and self.settings.compiler.libcxx == "libstdc++":
            raise ConanInvalidConfiguration("We just support GCC C++11ABI.\n**** Please run `conan profile update settings.compiler.libcxx=libstdc++11 default`")

    def configure(self):
        self.options["fmt"].header_only = True
        self.options["spdlog"].header_only = True

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
        cmake.definitions["WITH_RPC"] = option_on_off(self.options.with_rpc)

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

        cmake.definitions["CURRENCY"] = self.options.currency


        if self.settings.compiler == "gcc":
            if float(str(self.settings.compiler.version)) >= 5:
                cmake.definitions["NOT_USE_CPP11_ABI"] = option_on_off(False)
            else:
                cmake.definitions["NOT_USE_CPP11_ABI"] = option_on_off(True)
        elif self.settings.compiler == "clang":
            if str(self.settings.compiler.libcxx) == "libstdc++" or str(self.settings.compiler.libcxx) == "libstdc++11":
                cmake.definitions["NOT_USE_CPP11_ABI"] = option_on_off(False)

        # cmake.definitions["KTH_BUILD_NUMBER"] = os.getenv('KTH_BUILD_NUMBER', '-')
        # cmake.configure(source_dir=self.conanfile_directory)
        cmake.definitions["KTH_BUILD_NUMBER"] = os.getenv('KTH_BUILD_NUMBER', '-')
        cmake.configure(source_dir=self.source_folder)
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
        self.cpp_info.libs = ["kth-core"]

        if self.settings.os == "Linux":
            self.cpp_info.libs.append("pthread")
