#!/usr/bin/env bash
set -e

echo "Running existing tests..."

# Check if build directory exists
if [ ! -d "build" ]; then
    echo "Error: build directory not found. Please run build script first."
    echo "Usage: ./scripts/build.sh <version>"
    exit 1
fi

cd build/build/Release

echo "Running tests with ctest..."
ctest --output-on-failure --parallel 4 --verbose

echo "Test run completed!"
