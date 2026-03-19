#!/usr/bin/env bash
# Update dependencies (conan install) and rebuild without cleaning.
# Use this when you changed a dependency version but don't want a full rebuild.
set -x

if [ -z "$1" ]; then
    echo "Usage: $0 <version> [test]"
    exit 1
fi
VERSION="$1"
TEST="${2:-0}"

echo "Updating deps and building version: ${VERSION}"

rm -rf conan.lock

conan lock create conanfile.py --version="${VERSION}" -o "&:march_strategy=optimized" -o "&:with_stats=True" --update
conan lock create conanfile.py --version "${VERSION}" -o "&:march_strategy=optimized" -o "&:with_stats=True" --lockfile=conan.lock --lockfile-out=build/conan.lock
conan install conanfile.py --version="${VERSION}" -o "&:march_strategy=optimized" -o "&:with_stats=True" --lockfile=build/conan.lock -of build --build=missing

# Reconfigure CMake (needed after conan install to pick up new deps)
cmake --preset conan-release \
         -DCMAKE_VERBOSE_MAKEFILE=ON \
         -DGLOBAL_BUILD=ON \
         -DENABLE_TEST=ON \
         -DCMAKE_BUILD_TYPE=Release

if [ $? -ne 0 ]; then
    echo "CMake configuration failed"
    exit 1
fi

cmake --build --preset conan-release --parallel

if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

if [ "$TEST" != "0" ]; then
    echo "Running tests..."
    cd build/build/Release
    ctest --output-on-failure --parallel 4
else
    echo "Skipping tests"
fi
