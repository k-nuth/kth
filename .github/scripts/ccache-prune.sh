#!/usr/bin/env bash
# Delete the ccache entries a family no longer needs.
#
# The primary key ends in the run id on purpose: with a split restore/save, a
# save whose key already exists is rejected, so a unique key is what makes the
# save fire at all. The cost is that every run mints a new ~1 GB entry, and
# `restore-keys` only ever restores the newest of them. The rest are dead weight
# against a hard 10 GB repository budget — and when that budget is exceeded
# GitHub evicts by least-recently-used, which takes out the family written
# LEAST often rather than the one wasting the most space.
#
# Measured on this repository (#624): six `clean` entries at 4.97 GB, six macOS
# at 1.36 GB, two coverage at 3.73 GB, 13.14 GB total against a 10 GB limit —
# and exactly one `sanitizers` entry, whose restore then found nothing at all
# and paid a 100 %-miss build of 8389 s against 149 s when it survives.
#
# So: keep the newest of the family, drop the rest. Nothing is inferred about
# which entry is useful — `restore-keys` picks the most recent, and that is the
# one kept.

set -uo pipefail

PREFIX="${1:?usage: ccache-prune.sh <key-prefix> [ref]}"
REF="${2:-refs/heads/master}"

if [ -z "${GH_TOKEN:-}" ]; then
    echo "::warning::ccache-prune: no token, nothing was examined"
    exit 0
fi

if ! listing=$(gh api "repos/${GITHUB_REPOSITORY}/actions/caches?per_page=100" 2>&1) || [ -z "${listing}" ]; then
    # Said out loud. A prune that could not read the inventory has not found
    # "nothing to delete" — it has found nothing at all, and reporting the two
    # the same way is how a silently broken cleanup looks healthy for months.
    echo "::warning::ccache-prune: could not list caches; no entry was examined or removed"
    exit 0
fi

# Newest first, restricted to this family and this ref. Only the default branch
# prunes: a pull request restoring from master must not be able to delete what
# master's next run will restore.
parsed=$(printf '%s' "${listing}" | PRUNE_PREFIX="${PREFIX}" PRUNE_REF="${REF}" python3 -c '
import json, os, sys
prefix = os.environ["PRUNE_PREFIX"]
ref = os.environ["PRUNE_REF"]
data = json.load(sys.stdin).get("actions_caches", [])
rows = [c for c in data if c["key"].startswith(prefix) and c["ref"] == ref]
rows.sort(key=lambda c: c["created_at"], reverse=True)
for c in rows:
    sys.stdout.write(c["key"] + "\t" + str(c["size_in_bytes"]) + "\n")
')
parse_rc=$?

if [ "${parse_rc}" -ne 0 ]; then
    # Not "nothing to prune". The inventory could not be read, which is a
    # different fact and has to stay one: a cleanup that crashes and reports
    # "nothing superseded" is indistinguishable from one that works, and the
    # budget keeps growing while the summary says it is fine.
    echo "::warning::ccache-prune: could not parse the cache listing; nothing was examined"
    exit 0
fi

newest=""
freed=0
removed=0

# A while-read loop, not `mapfile`: the macOS runners ship Bash 3.2, where
# `mapfile` does not exist. A script that only runs under Homebrew's Bash is a
# script that does not run where half these jobs run.
while IFS='	' read -r key size; do
    [ -n "${key}" ] || continue

    if [ -z "${newest}" ]; then
        # The list arrives newest-first, and this is the one `restore-keys`
        # would pick. Keeping anything else would be choosing for it.
        newest="${key}"
        continue
    fi

    if gh api -X DELETE "repos/${GITHUB_REPOSITORY}/actions/caches?key=${key}&ref=${REF}" >/dev/null 2>&1; then
        freed=$(( freed + size ))
        removed=$(( removed + 1 ))
        echo "ccache-prune: removed ${key} ($(( size / 1000000 )) MB)"
    else
        echo "::warning::ccache-prune: could not remove ${key}"
    fi
done <<EOF
${parsed}
EOF

if [ -z "${newest}" ]; then
    echo "ccache-prune: ${PREFIX} has no entry on ${REF}; nothing superseded"
    exit 0
fi

if [ "${removed}" -eq 0 ]; then
    echo "ccache-prune: ${PREFIX} — kept ${newest}, nothing superseded"
    exit 0
fi

echo "ccache-prune: ${PREFIX} — kept ${newest}, removed ${removed}, freed $(( freed / 1000000 )) MB"
