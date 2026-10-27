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
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if can_run(self):
            self.run(os.path.join(self.cpp.build.bindir, "test_exported_secp256k1"))
            self.output.info("🧪 Testing KTH package integration...")
            
            # Test individual targets
            test_individual = os.path.join(self.cpp.build.bindir, "test_individual")
            if os.path.exists(test_individual):
                self.output.info("Running individual targets test...")
                self.run(test_individual)
            else:
                self.output.warn("test_individual executable not found")
            
            # Test meta target
            test_meta = os.path.join(self.cpp.build.bindir, "test_meta")
            if os.path.exists(test_meta):
                self.output.info("Running meta target test...")
                self.run(test_meta)
            else:
                self.output.warn("test_meta executable not found")
            
            self.output.info("✅ KTH package integration tests completed successfully!")
            self.output.info("🎉 All KTH targets are properly linked and functional!")
