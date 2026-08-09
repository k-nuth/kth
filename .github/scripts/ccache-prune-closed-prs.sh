#!/usr/bin/env bash
# Delete the caches left behind by pull requests that are closed.
#
# Measured on this repository after the family prune of #630 landed: 8.91 GB in
# twelve entries, of which 4.56 GB — **51 %** — belonged to two merged pull
# requests. A closed pull request's cache can never be restored by anything:
# its ref is gone, no run will ever key against it, and GitHub's own cleanup is
# not prompt (both entries were still there hours after the merges).
#
# The other reading of the same numbers is that `ccache-coverage` is 76 % of the
# budget at ~2.2 GB per entry. It is the same 4.56 GB: the closed-PR waste IS
# coverage. Shrinking that cache is a second, riskier change — it would trade
# the coverage job's own hit rate for space — so this takes the half that is
# pure waste and leaves the half that is doing work.
#
# The rule is one line: an OPEN pull request's cache is never touched, and
# neither is a ref this script cannot resolve to a closed pull request. Unknown
# is not closed.

set -uo pipefail

REPO="${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is not set}"
API="${GITHUB_API_URL:-https://api.github.com}"

give_up() {
    echo "::warning::ccache-prune-closed-prs: $1; no entry was examined or removed"
    exit 0
}

command -v curl >/dev/null 2>&1 || give_up "curl is not installed on this runner"
command -v python3 >/dev/null 2>&1 || give_up "python3 is not installed on this runner"
[ -n "${GH_TOKEN:-}" ] || give_up "no token was provided"

# shellcheck source=.github/scripts/gh-api-lib.sh
. "$(dirname "$0")/gh-api-lib.sh"

# ---------------------------------------------------------------------------
# Inventory, paginated. A hundred entries is a page, not an inventory.
# ---------------------------------------------------------------------------
page=1
while :; do
    status=$(api_call GET \
        "${API}/repos/${REPO}/actions/caches?per_page=100&page=${page}" \
        "${WORK}/page.${page}.json")
    [ "${status}" = "200" ] || give_up "$(describe_status "${status}")"

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
    [ "${page}" -gt 20 ] && give_up "the cache listing did not end after 20 pages"
done

# Only caches whose ref names a pull request. `refs/heads/*` is out of scope
# here: those are the family prune's business, and a branch is not a request
# that can be closed.
if ! candidates=$(python3 -c '
import json, re, sys
rows = []
for path in sys.argv[1:]:
    with open(path) as handle:
        rows.extend(json.load(handle).get("actions_caches", []))
seen = set()
for c in rows:
    m = re.fullmatch(r"refs/pull/(\d+)/(merge|head)", c["ref"])
    if not m:
        continue
    key = (c["key"], c["ref"])
    if key in seen:
        continue
    seen.add(key)
    sys.stdout.write(m.group(1) + "\t" + c["key"] + "\t" + c["ref"] + "\t" + str(c["size_in_bytes"]) + "\n")
' "${WORK}"/page.*.json); then
    give_up "the cache listing could not be parsed"
fi

if [ -z "${candidates}" ]; then
    echo "ccache-prune-closed-prs: no pull-request-scoped cache exists; nothing to consider"
    exit 0
fi

# ---------------------------------------------------------------------------
# Delete only what belongs to a pull request that is closed.
# ---------------------------------------------------------------------------
freed=0
removed=0
kept_open=0
kept_unknown=0

ref_state() {
    # Answers "closed", "open", or "unknown". Never guesses: a request whose
    # state could not be read is left alone, because deleting a live pull
    # request's cache costs it a full cold build and nothing here is urgent
    # enough to risk that.
    local number="$1"
    local s
    s=$(api_call GET "${API}/repos/${REPO}/pulls/${number}" "${WORK}/pr.json")
    if [ "${s}" != "200" ]; then
        printf 'unknown'
        return
    fi
    PR_FILE="${WORK}/pr.json" python3 -c '
import json, os, sys
with open(os.environ["PR_FILE"]) as handle:
    data = json.load(handle)
state = data.get("state")
sys.stdout.write(state if state in ("open", "closed") else "unknown")
' 2>/dev/null || printf 'unknown'
}

while IFS="$(printf '\t')" read -r number key ref size; do
    [ -n "${key}" ] || continue

    state="$(ref_state "${number}")"
    case "${state}" in
        closed) ;;
        open)
            kept_open=$(( kept_open + 1 ))
            echo "ccache-prune-closed-prs: kept ${key} — PR #${number} is open"
            continue
            ;;
        *)
            kept_unknown=$(( kept_unknown + 1 ))
            echo "::warning::ccache-prune-closed-prs: kept ${key} — the state of PR #${number} could not be read, and unknown is not closed"
            continue
            ;;
    esac

    key_encoded="$(ENC="${key}" python3 -c 'import os,urllib.parse,sys; sys.stdout.write(urllib.parse.quote(os.environ["ENC"], safe=""))')"
    ref_encoded="$(ENC="${ref}" python3 -c 'import os,urllib.parse,sys; sys.stdout.write(urllib.parse.quote(os.environ["ENC"], safe=""))')"

    del=$(api_call DELETE \
        "${API}/repos/${REPO}/actions/caches?key=${key_encoded}&ref=${ref_encoded}" \
        "${WORK}/del.json")
    case "${del}" in
        200|204)
            freed=$(( freed + size ))
            removed=$(( removed + 1 ))
            echo "ccache-prune-closed-prs: removed ${key} ($(( size / 1000000 )) MB, PR #${number} closed)"
            ;;
        *)
            echo "::warning::ccache-prune-closed-prs: could not remove ${key}: $(describe_status "${del}")"
            ;;
    esac
done <<EOF
${candidates}
EOF

echo "ccache-prune-closed-prs: removed ${removed}, freed $(( freed / 1000000 )) MB, kept ${kept_open} open, kept ${kept_unknown} unresolved"
