#!/usr/bin/env bash
set -x

if [ -z "$1" ]; then
    echo "Usage: $0 <version>"
    exit 1
fi
VERSION="$1"

echo "Building version: ${VERSION}"

rm -rf build
rm -rf conan-wasm.lock

conan lock create conanfile.py --version="${VERSION}" --lockfile=conan-wasm.lock --update -pr ems2 -o consensus=False -o tests=False

conan lock create conanfile.py --version "${VERSION}" --lockfile=conan-wasm.lock --lockfile-out=build/conan.lock -pr ems2 -o consensus=False -o tests=False
conan create conanfile.py --version "${VERSION}" --lockfile=build/conan.lock --build=missing  -pr ems2 -o consensus=False -o tests=False


