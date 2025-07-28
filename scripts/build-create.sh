#!/bin/bash
set -x

if [ -z "$1" ]; then
    echo "Usage: $0 <version>"
    exit 1
fi
VERSION="$1"

echo "Building version: ${VERSION}"

rm -rf build
rm -rf conan.lock

conan lock create conanfile.py --version="${VERSION}" --update

conan lock create conanfile.py --version="${VERSION}" --lockfile=conan.lock --lockfile-out=build/conan.lock
conan create conanfile.py --version "${VERSION}" --lockfile=build/conan.lock --build=missing -o tests=False

# Run tests after create
echo "Running tests..."
conan test test_package conanfile.py --version "${VERSION}" --lockfile=build/conan.lock 


