#!/usr/bin/env bash
# Verify the coverage compiler flags are cacheable and a hit restores .gcno.
set -euo pipefail

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
export CCACHE_DIR="$scratch/cache"
export CCACHE_CONFIGPATH="$scratch/ccache.conf"
printf 'max_size = 16M\n' > "$CCACHE_CONFIGPATH"
cd "$scratch"
printf 'int main(void) { return 0; }\n' > probe.c

ccache --zero-stats >/dev/null
ccache "${CC:-cc}" --coverage -fprofile-update=atomic -c probe.c -o probe.o
test -s probe.gcno
rm probe.o probe.gcno
ccache "${CC:-cc}" --coverage -fprofile-update=atomic -c probe.c -o probe.o
test -s probe.o
test -s probe.gcno

stats=$(ccache --print-stats)
hits=$(awk '$1 == "direct_cache_hit" || $1 == "preprocessed_cache_hit" { n += $2 } END { print n+0 }' <<< "$stats")
misses=$(awk '$1 == "cache_miss" { print $2 }' <<< "$stats")
if [ "$hits" != 1 ] || [ "$misses" != 1 ]; then
    echo '::error::coverage compilation must have one miss followed by one cache hit'
    ccache --show-stats --verbose
    exit 1
fi
echo 'Coverage cache: one miss, one hit, .gcno restored'
