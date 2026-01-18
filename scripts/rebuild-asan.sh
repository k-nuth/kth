#!/usr/bin/env bash
# Full rebuild with AddressSanitizer enabled + debug symbols
# Creates build in build_asan/ directory
# Uses conan presets with RelWithDebInfo for better stack traces
set -ex

if [ -z "$1" ]; then
    echo "Usage: $0 <version> [test]"
    echo "Example: $0 0.76.0"
    echo "Example: $0 0.76.0 1   # compile and run tests"
    exit 1
fi
VERSION="$1"
TEST="$2"
if [ -z "$TEST" ]; then
    echo "No test specified, defaulting to '0'"
    TEST="0"
fi
echo "Building version: ${VERSION} with test: ${TEST}"

BUILD_DIR="build_asan"

# Set sanitizer flags
SANITIZER_FLAGS="-fsanitize=address -fno-omit-frame-pointer"
export CXXFLAGS="${CXXFLAGS} ${SANITIZER_FLAGS}"
export CFLAGS="${CFLAGS} ${SANITIZER_FLAGS}"
export LDFLAGS="${LDFLAGS} -fsanitize=address"

# Clean and rebuild
rm -rf "$BUILD_DIR"
rm -rf conan.lock

echo "Creating conan lock files..."
conan lock create conanfile.py --version="${VERSION}" -o "&:march_strategy=optimized" -s build_type=RelWithDebInfo --update
conan lock create conanfile.py --version="${VERSION}" -o "&:march_strategy=optimized" -s build_type=RelWithDebInfo --lockfile=conan.lock --lockfile-out="${BUILD_DIR}/conan.lock"

echo "Installing conan dependencies with RelWithDebInfo..."
conan install conanfile.py --version="${VERSION}" -o "&:march_strategy=optimized" -s build_type=RelWithDebInfo --lockfile="${BUILD_DIR}/conan.lock" -of "${BUILD_DIR}" --build=missing

echo "Configuring CMake with AddressSanitizer + debug symbols..."
# Use conan preset for RelWithDebInfo, add ASAN flags
# Disable secp256k1 asm (incompatible with ASAN - uses all registers)
# Disable TSAN tests (incompatible with ASAN)
if [ "$TEST" != "0" ]; then
    ENABLE_TEST_FLAG="-DENABLE_TEST=ON"
else
    ENABLE_TEST_FLAG="-DENABLE_TEST=OFF"
fi

cmake --preset conan-relwithdebinfo \
    -DCMAKE_VERBOSE_MAKEFILE=ON \
    -DGLOBAL_BUILD=ON \
    ${ENABLE_TEST_FLAG} \
    -DCMAKE_CXX_FLAGS="${SANITIZER_FLAGS}" \
    -DCMAKE_C_FLAGS="${SANITIZER_FLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" \
    -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address" \
    -DSECP256K1_USE_ASM=OFF \
    -DENABLE_TSAN_TESTS=OFF

if [ $? -ne 0 ]; then
    echo "CMake configuration failed"
    exit 1
fi

echo "Building with AddressSanitizer..."
cmake --build --preset conan-relwithdebinfo --parallel

if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

# Run tests if specified
if [ "$TEST" != "0" ]; then
    echo "Running tests..."
    cd "${BUILD_DIR}/build/RelWithDebInfo"
    ctest --output-on-failure --parallel 4
else
    echo "Skipping tests"
fi

echo ""
echo "=============================================="
echo "AddressSanitizer build complete!"
echo "=============================================="
echo ""
echo "Binary at: ${BUILD_DIR}/build/RelWithDebInfo/src/node-exe/kth"
echo ""
echo "To run: ./scripts/run-asan.sh"
