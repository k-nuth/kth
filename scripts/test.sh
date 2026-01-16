#!/usr/bin/env bash
set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <version>"
    exit 1
fi
VERSION="$1"

echo "Running tests for version: ${VERSION}"

# Build with tests enabled
conan lock create conanfile.py --version="${VERSION}" --update
conan lock create conanfile.py --version="${VERSION}" --lockfile=conan.lock --lockfile-out=build/conan.lock
conan install conanfile.py --lockfile=build/conan.lock -of build --build=missing -o tests=True

# Configure with tests enabled
cmake --preset conan-release \
         -DCMAKE_VERBOSE_MAKEFILE=ON \
         -DGLOBAL_BUILD=ON \
         -DENABLE_TEST=ON \
         -DCMAKE_BUILD_TYPE=Release

cmake --build --preset conan-release -j4

# Run tests after build
echo "Running tests..."
cd build
ctest --output-on-failure --parallel 4

echo "Tests completed successfully!"
