#!/bin/bash

# VERSION=$(cat conanfile.py | grep "version" | cut -d '"' -f 2)
VERSION="0.68.0"

echo "Building version: ${VERSION}"

rm -rf build
rm -rf conan.lock

conan lock create conanfile.py --version="${VERSION}" --update
# conan lock create conanfile.py --version "${VERSION}" --lockfile=conan.lock --lockfile-out=build/conan.lock
# conan install conanfile.py --lockfile=build/conan.lock -of build --build=missing

# cmake --preset conan-release \
#          -DCMAKE_VERBOSE_MAKEFILE=ON \
#          -DGLOBAL_BUILD=ON \
#          -DCMAKE_BUILD_TYPE=Release

# cmake --build --preset conan-release -j4 \
#          -DCMAKE_VERBOSE_MAKEFILE=ON \
#          -DGLOBAL_BUILD=ON \
#          -DCMAKE_BUILD_TYPE=Release
