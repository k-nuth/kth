#!/usr/bin/env bash
set -x

if [ -z "$1" ]; then
    echo "Usage: $0 <version>"
    exit 1
fi
VERSION="$1"

echo "Building version: ${VERSION}"

# Use a workspace separate from build.sh's `build/` so the two scripts can
# run side-by-side without trampling each other's intermediate files.
WORKDIR="build-create"

rm -rf "${WORKDIR}"
rm -rf conan.lock

conan lock create conanfile.py --version="${VERSION}" --update

conan lock create conanfile.py --version="${VERSION}" --lockfile=conan.lock --lockfile-out="${WORKDIR}/conan.lock"
conan create conanfile.py --version "${VERSION}" --lockfile="${WORKDIR}/conan.lock" --build=missing -o tests=False

# Run tests after create
echo "Running tests..."
conan test test_package "kth/${VERSION}"


