#!/bin/bash
# Full rebuild with AddressSanitizer enabled
# Creates build in build-asan/ directory
set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <version>"
    echo "Example: $0 0.69.0"
    exit 1
fi
VERSION="$1"

echo "Rebuilding with AddressSanitizer, version: ${VERSION}"

BUILD_DIR="build-asan"

# Set sanitizer flags
export CXXFLAGS="${CXXFLAGS} -fsanitize=address -fno-omit-frame-pointer"
export CFLAGS="${CFLAGS} -fsanitize=address -fno-omit-frame-pointer"
export LDFLAGS="${LDFLAGS} -fsanitize=address"

# Clean and rebuild
rm -rf "$BUILD_DIR"
rm -rf conan.lock

echo "Creating conan lock files..."
conan lock create conanfile.py --version="${VERSION}" -o "&:march_strategy=optimized" --update
conan lock create conanfile.py --version "${VERSION}" -o "&:march_strategy=optimized" --lockfile=conan.lock --lockfile-out="${BUILD_DIR}/conan.lock"

echo "Installing conan dependencies..."
conan install conanfile.py --version="${VERSION}" -o "&:march_strategy=optimized" --lockfile="${BUILD_DIR}/conan.lock" -of "${BUILD_DIR}" --build=missing

echo "Configuring CMake with AddressSanitizer..."
cmake -S . -B "${BUILD_DIR}/build/Release" \
    -DCMAKE_TOOLCHAIN_FILE="${BUILD_DIR}/conan_toolchain.cmake" \
    -DCMAKE_VERBOSE_MAKEFILE=ON \
    -DGLOBAL_BUILD=ON \
    -DENABLE_TEST=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
    -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" \
    -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address"

if [ $? -ne 0 ]; then
    echo "CMake configuration failed"
    exit 1
fi

echo "Building with AddressSanitizer..."
cmake --build "${BUILD_DIR}/build/Release" --parallel

if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

echo ""
echo "=============================================="
echo "AddressSanitizer build complete!"
echo "=============================================="
echo ""
echo "Binary at: ${BUILD_DIR}/build/Release/src/node-exe/kth"
echo ""
echo "To run with ASAN:"
echo "  export ASAN_OPTIONS='detect_leaks=1:abort_on_error=0:print_stats=1'"
echo "  ${BUILD_DIR}/build/Release/src/node-exe/kth -c your_config.cfg"
echo ""
echo "To debug a crash:"
echo "  gdb ${BUILD_DIR}/build/Release/src/node-exe/kth"
echo "  (gdb) run -c your_config.cfg"
echo "  (gdb) bt   # after crash, print backtrace"
