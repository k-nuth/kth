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
#
# Talks to the REST API with curl rather than with `gh`. The first revision used
# `gh`, which is on the hosted runners and is NOT in this project's build
# container: the container job reported "could not list caches" and pruned
# nothing while every other job worked. curl is in every image here.
#
# A `while` loop rather than `mapfile`: the macOS runners ship Bash 3.2.

set -uo pipefail

PREFIX="${1:?usage: ccache-prune.sh <key-prefix> [ref]}"
REF="${2:-refs/heads/master}"
API="${GITHUB_API_URL:-https://api.github.com}"

# Every early exit below says what could not be done. None of them may read as
# "there was nothing to prune": that sentence means the inventory was examined
# and found clean, and an unexamined inventory keeps growing while the log says
# it is fine — which is how the defect this script exists for survived.
give_up() {
    echo "::warning::ccache-prune: $1; no entry was examined or removed"
    exit 0
}

command -v curl >/dev/null 2>&1 || give_up "curl is not installed on this runner"
command -v python3 >/dev/null 2>&1 || give_up "python3 is not installed on this runner"
[ -n "${GH_TOKEN:-}" ] || give_up "no token was provided"
[ -n "${GITHUB_REPOSITORY:-}" ] || give_up "GITHUB_REPOSITORY is not set"

# shellcheck source=.github/scripts/gh-api-lib.sh
. "$(dirname "$0")/gh-api-lib.sh"


# Paginated. A hundred entries is a page, not an inventory, and pruning from a
# truncated view would keep the newest of what it happened to see — which for a
# family whose entries all sat on page two is nothing at all, silently.
page=1
pages_read=0
while :; do
    status=$(api_call GET \
        "${API}/repos/${GITHUB_REPOSITORY}/actions/caches?per_page=100&page=${page}" \
        "${WORK}/page.${page}.json")
    if [ "${status}" != "200" ]; then
        give_up "$(describe_status "${status}")"
    fi
    pages_read=$(( pages_read + 1 ))

    if ! more=$(PAGE_FILE="${WORK}/page.${page}.json" PAGE_NUM="${page}" python3 -c '
import json, os, sys
with open(os.environ["PAGE_FILE"]) as handle:
    data = json.load(handle)
seen = int(os.environ["PAGE_NUM"]) * 100
sys.stdout.write("yes" if data.get("total_count", 0) > seen and data.get("actions_caches") else "no")
'); then
        give_up "the cache listing could not be parsed"
    fi

    [ "${more}" = "yes" ] || break
    page=$(( page + 1 ))

    if [ "${page}" -gt 20 ]; then
        # Two thousand entries is not a state this repository reaches; reading
        # on would mean the loop is wrong, not that the inventory is enormous.
        give_up "the cache listing did not end after ${pages_read} pages"
    fi
done

# Newest first, restricted to this family and this ref. Only the default branch
# prunes: a pull request restoring from master must not be able to delete what
# master's next run will restore.
if ! parsed=$(PRUNE_PREFIX="${PREFIX}" PRUNE_REF="${REF}" python3 -c '
import json, os, sys
prefix = os.environ["PRUNE_PREFIX"]
ref = os.environ["PRUNE_REF"]
entries = []
for path in sys.argv[1:]:
    with open(path) as handle:
        entries.extend(json.load(handle).get("actions_caches", []))
rows = [c for c in entries if c["key"].startswith(prefix) and c["ref"] == ref]
rows.sort(key=lambda c: c["created_at"], reverse=True)
for c in rows:
    sys.stdout.write(c["key"] + "\t" + str(c["size_in_bytes"]) + "\n")
' "${WORK}"/page.*.json); then
    give_up "the cache listing could not be parsed"
fi

# Keys are opaque strings that end up in a query string. Encoding them by hand
# would be one more thing to get wrong the day a key grows a character that
# needs it.
urlencode() {
    ENC_VALUE="$1" python3 -c 'import os, urllib.parse, sys; sys.stdout.write(urllib.parse.quote(os.environ["ENC_VALUE"], safe=""))'
}

ref_encoded="$(urlencode "${REF}")"
newest=""
freed=0
removed=0
failed=0

while IFS="$(printf '\t')" read -r key size; do
    [ -n "${key}" ] || continue

    if [ -z "${newest}" ]; then
        # The list arrives newest-first, and this is the one `restore-keys`
        # would pick. Keeping anything else would be choosing for it.
        newest="${key}"
        continue
    fi

    key_encoded="$(urlencode "${key}")"
    del_status=$(api_call DELETE \
        "${API}/repos/${GITHUB_REPOSITORY}/actions/caches?key=${key_encoded}&ref=${ref_encoded}" \
        "${WORK}/del.json")

    case "${del_status}" in
        200|204)
            freed=$(( freed + size ))
            removed=$(( removed + 1 ))
            echo "ccache-prune: removed ${key} ($(( size / 1000000 )) MB)"
            ;;
        *)
            failed=$(( failed + 1 ))
            echo "::warning::ccache-prune: could not remove ${key}: $(describe_status "${del_status}")"
            ;;
    esac
done <<EOF
${parsed}
EOF

if [ -z "${newest}" ]; then
    echo "ccache-prune: ${PREFIX} has no entry on ${REF}; nothing superseded"
    exit 0
fi

if [ "${removed}" -eq 0 ] && [ "${failed}" -eq 0 ]; then
    echo "ccache-prune: ${PREFIX} — kept ${newest}, nothing superseded"
    exit 0
fi

echo "ccache-prune: ${PREFIX} — kept ${newest}, removed ${removed}, freed $(( freed / 1000000 )) MB, failed ${failed}"
