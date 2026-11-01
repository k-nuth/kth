from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.build import can_run
import os

class TestPackageConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps"

    def layout(self):
        cmake_layout(self)

    def requirements(self):
        self.requires(self.tested_reference_str)

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.variables["KTH_PACKAGE_FOLDER"] = self.dependencies["kth"].package_folder
        toolchain.variables["KTH_EXPECT_CONSENSUS"] = bool(self.dependencies["kth"].options.consensus)
        toolchain.variables["KTH_EXPECT_GRPC"] = (
            self.dependencies["kth"].options.rpc == "grpc" and self.settings.os != "Emscripten")
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if can_run(self):
            for executable in ("test_exported_secp256k1", "test_big_number",
                               "test_individual", "test_meta"):
                self.run(os.path.join(self.cpp.build.bindir, executable), env="conanrun")
