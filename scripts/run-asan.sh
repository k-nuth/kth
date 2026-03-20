#!/usr/bin/env bash
# Run the node with AddressSanitizer enabled
# Assumes rebuild-asan.sh was run first

set -e

BINARY="build_asan/build/RelWithDebInfo/src/node-exe/kth"

if [ ! -f "$BINARY" ]; then
    echo "Error: ASAN binary not found at $BINARY"
    echo "Run ./scripts/rebuild-asan.sh <version> first"
    exit 1
fi

# ASAN options:
#   detect_leaks=0      - disable leak detection (slow, not what we need)
#   abort_on_error=1    - abort on first error for clean stack trace
#   print_stats=1       - print memory stats at exit
#   symbolize=1         - show function names in stack traces
#   halt_on_error=1     - stop on first error
export ASAN_OPTIONS='detect_leaks=0:abort_on_error=1:print_stats=1:symbolize=1:halt_on_error=1'

echo "Running with ASAN enabled..."
echo "Binary: $BINARY"
echo "ASAN_OPTIONS: $ASAN_OPTIONS"
echo ""

exec "$BINARY" "$@"
