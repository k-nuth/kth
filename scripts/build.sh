#!/bin/bash
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
echo "Building version: ${VERSION} with test: ${TEST}"

# rm -rf build
# rm -rf conan.lock

# conan lock create conanfile.py --version="${VERSION}" --update
# conan lock create conanfile.py --version "${VERSION}" --lockfile=conan.lock --lockfile-out=build/conan.lock
# conan install conanfile.py --lockfile=build/conan.lock -of build --build=missing

# Only configure if not already configured or if explicitly requested
if [ ! -f "build/build/Release/CMakeCache.txt" ] || [ "$RECONFIGURE" = "1" ]; then
    echo "Configuring CMake..."
    cmake --preset conan-release \
             -DCMAKE_VERBOSE_MAKEFILE=ON \
             -DGLOBAL_BUILD=ON \
             -DENABLE_TEST=ON \
             -DCMAKE_BUILD_TYPE=Release

    if [ $? -ne 0 ]; then
        echo "CMake configuration failed"
        exit 1
    fi
else
    echo "Skipping CMake configuration (already configured). Set RECONFIGURE=1 to force."
fi

cmake --build --preset conan-release --parallel

if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

# Run tests if specified
if [ "$TEST" != "0" ]; then
    echo "Running tests..."
    cd build/build/Release
    # Run tests with ctest (CTestTestfile.cmake is now generated automatically by CMake)
    ctest --output-on-failure --parallel 4
else
    echo "Skipping tests"
fi
