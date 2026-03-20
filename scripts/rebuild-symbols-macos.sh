#!/bin/bash
# Full rebuild with Release optimizations + debug symbols (RelWithDebInfo)
set -x

if [ -z "$1" ]; then
    echo "Usage: $0 <version> <test>"
    exit 1
fi
VERSION="$1"
TEST="$2"
if [ -z "$TEST" ]; then
    echo "No test specified, defaulting to '0'"
    TEST="0"
fi
echo "Building version: ${VERSION} with test: ${TEST} (RelWithDebInfo)"

rm -rf build
rm -rf conan.lock

conan lock create conanfile.py --version="${VERSION}" --update || exit 1
conan lock create conanfile.py --version "${VERSION}" --lockfile=conan.lock --lockfile-out=build/conan.lock || exit 1
conan install conanfile.py --version="${VERSION}" --lockfile=build/conan.lock -of build --build=missing -s build_type=RelWithDebInfo || exit 1

cmake --preset conan-relwithdebinfo \
         -DCMAKE_VERBOSE_MAKEFILE=ON \
         -DGLOBAL_BUILD=ON \
         -DENABLE_TEST=ON \
         -DCMAKE_BUILD_TYPE=RelWithDebInfo

if [ $? -ne 0 ]; then
    echo "CMake configuration failed"
    exit 1
fi

cmake --build --preset conan-relwithdebinfo --parallel

if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

echo ""
echo "Build complete. Binary with debug symbols at:"
echo "  build/build/RelWithDebInfo/src/node-exe/kth"
echo ""
echo "To debug: lldb build/build/RelWithDebInfo/src/node-exe/kth"

# Run tests if specified
if [ "$TEST" != "0" ]; then
    echo "Running tests..."
    cd build/build/RelWithDebInfo || exit 1
    ctest --output-on-failure --parallel 4
else
    echo "Skipping tests"
fi
