#!/usr/bin/env bash
# Exercises ccache-prune.sh against a synthetic inventory, with `gh` stubbed.
#
# Run by CI on the macOS job with the platform's own /bin/bash, which is 3.2.
# The first revision of this work used `mapfile`, which that Bash does not have:
# a script tested only under a newer Bash is a script that has not been tested
# where half these jobs run.

set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PRUNE="${HERE}/ccache-prune.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

echo "ccache-prune-selftest: bash ${BASH_VERSION}"

# A curl that answers from files instead of the network. It honours the two
# flags the script relies on — --output and --write-out '%{http_code}' — so the
# status handling is what is under test, not a mock of it.
cat > "${WORK}/curl" <<'STUB'
#!/usr/bin/env bash
out=""
method="GET"
url=""
prev=""
for arg in "$@"; do
    case "${prev}" in
        --output) out="${arg}" ;;
        --request) method="${arg}" ;;
    esac
    case "${arg}" in
        https://*|http://*) url="${arg}" ;;
    esac
    prev="${arg}"
done

if [ "${method}" = "GET" ]; then
    if [ -n "${FAKE_CURL_FAIL:-}" ]; then
        # What curl does when it never reaches the host: --write-out prints 000
        # and the process exits non-zero with a message on stderr. Both halves
        # matter — the first revision appended its own 000 to this one.
        : > "${out}"
        printf '000'
        echo "curl: (6) Could not resolve host: api.github.com" >&2
        exit 6
    fi
    page="${FAKE_PAGE:-1}"
    src="${FAKE_CACHES}"
    case "${url}" in
        *"page=2"*) [ -n "${FAKE_CACHES_P2:-}" ] && src="${FAKE_CACHES_P2}" ;;
    esac
    cat "${src}" > "${out}"
    printf '%s' "${FAKE_LIST_STATUS:-200}"
    exit 0
fi

if [ "${method}" = "DELETE" ]; then
    echo "${url}" >> "${FAKE_DELETED}"
    : > "${out}"
    printf '%s' "${FAKE_DELETE_STATUS:-204}"
    exit 0
fi
exit 1
STUB
chmod +x "${WORK}/curl"

cat > "${WORK}/caches.json" <<'JSON'
{"total_count":5,"actions_caches":[
 {"key":"ccache-Linux-GCC-16-clean-100","ref":"refs/heads/master","size_in_bytes":830000000,"created_at":"2026-08-07T13:50:00Z"},
 {"key":"ccache-Linux-GCC-16-clean-200","ref":"refs/heads/master","size_in_bytes":840000000,"created_at":"2026-08-07T14:20:00Z"},
 {"key":"ccache-Linux-GCC-16-clean-300","ref":"refs/heads/master","size_in_bytes":850000000,"created_at":"2026-08-08T12:40:00Z"},
 {"key":"ccache-Linux-GCC-16-clean-999","ref":"refs/pull/9/merge","size_in_bytes":800000000,"created_at":"2026-08-08T13:00:00Z"},
 {"key":"ccache-Linux-GCC-16-sanitizers-300","ref":"refs/heads/master","size_in_bytes":1100000000,"created_at":"2026-08-08T12:45:00Z"}
]}
JSON

export PATH="${WORK}:${PATH}"
export GITHUB_REPOSITORY="k-nuth/kth"
export FAKE_CACHES="${WORK}/caches.json"
export GITHUB_API_URL="https://api.github.com"
mkdir -p "${WORK}/empty"

failures=0
check() {
    if [ "$2" = "$3" ]; then
        echo "  ok   $1"
    else
        echo "  FAIL $1: expected [$3], got [$2]"
        failures=$(( failures + 1 ))
    fi
}

# 1. Three master entries in one family: keep the newest, drop the two older,
#    and never touch the pull-request-scoped one.
export FAKE_DELETED="${WORK}/d1"; : > "${FAKE_DELETED}"
out=$(GH_TOKEN=x "${PRUNE}" "ccache-Linux-GCC-16-clean-" 2>&1)
check "keeps the newest" "$(printf '%s' "${out}" | grep -c 'kept ccache-Linux-GCC-16-clean-300')" "1"
check "removes two" "$(grep -c . "${FAKE_DELETED}")" "2"
check "leaves the PR-scoped entry" "$(grep -c 'clean-999' "${FAKE_DELETED}" || true)" "0"

# 2. A family with a single entry has nothing superseded.
export FAKE_DELETED="${WORK}/d2"; : > "${FAKE_DELETED}"
out=$(GH_TOKEN=x "${PRUNE}" "ccache-Linux-GCC-16-sanitizers-" 2>&1)
check "single entry deletes nothing" "$(grep -c . "${FAKE_DELETED}" || true)" "0"
check "single entry says so" "$(printf '%s' "${out}" | grep -c 'nothing superseded')" "1"

# 3. An unreadable inventory is reported as not examined — never as clean.
export FAKE_DELETED="${WORK}/d3"; : > "${FAKE_DELETED}"
echo 'not json' > "${WORK}/bad.json"
out=$(FAKE_CACHES="${WORK}/bad.json" GH_TOKEN=x "${PRUNE}" "ccache-Linux-GCC-16-clean-" 2>&1)
check "unparseable listing warns" "$(printf '%s' "${out}" | grep -c 'could not be parsed')" "1"
check "unparseable listing deletes nothing" "$(grep -c . "${FAKE_DELETED}" || true)" "0"
check "unparseable is not reported as superseded-free" \
     "$(printf '%s' "${out}" | grep -c 'nothing superseded')" "0"

# 4. No token: same discipline.
export FAKE_DELETED="${WORK}/d4"; : > "${FAKE_DELETED}"
out=$(GH_TOKEN='' "${PRUNE}" "ccache-Linux-GCC-16-clean-" 2>&1)
check "no token warns" "$(printf '%s' "${out}" | grep -c 'no token was provided')" "1"
check "no token deletes nothing" "$(grep -c . "${FAKE_DELETED}" || true)" "0"

# 5. Every way the API can refuse. None of them may be read as a clean
#    inventory, and each has to name itself: a rejected token and an
#    unreachable endpoint call for different fixes.
for pair in "401:token was rejected" "403:lacks actions permission" "404:answered 404" "500:answered HTTP 500" "000:could not be reached"; do
    code="${pair%%:*}"
    expected="${pair#*:}"
    export FAKE_DELETED="${WORK}/d5_${code}"; : > "${FAKE_DELETED}"
    out=$(FAKE_LIST_STATUS="${code}" GH_TOKEN=x "${PRUNE}" "ccache-Linux-GCC-16-clean-" 2>&1)
    check "list HTTP ${code} names itself" "$(printf '%s' "${out}" | grep -c "${expected}")" "1"
    check "list HTTP ${code} deletes nothing" "$(grep -c . "${FAKE_DELETED}" || true)" "0"
    check "list HTTP ${code} is not 'nothing superseded'" \
         "$(printf '%s' "${out}" | grep -c 'nothing superseded')" "0"
done

# 6. A delete that is refused is counted and named, not silently skipped: the
#    budget did not shrink and the log has to be able to say so.
export FAKE_DELETED="${WORK}/d6"; : > "${FAKE_DELETED}"
out=$(FAKE_DELETE_STATUS=403 GH_TOKEN=x "${PRUNE}" "ccache-Linux-GCC-16-clean-" 2>&1)
check "refused delete warns" "$(printf '%s' "${out}" | grep -c 'could not remove')" "2"
check "refused delete is counted" "$(printf '%s' "${out}" | grep -c 'failed 2')" "1"
check "refused delete frees nothing" "$(printf '%s' "${out}" | grep -c 'freed 0 MB')" "1"

# 7. The key and the ref reach the API encoded. `refs/heads/master` has slashes,
#    and an unencoded one would silently address a different resource.
export FAKE_DELETED="${WORK}/d7"; : > "${FAKE_DELETED}"
out=$(GH_TOKEN=x "${PRUNE}" "ccache-Linux-GCC-16-clean-" 2>&1)
check "ref is percent-encoded" "$(grep -c 'ref=refs%2Fheads%2Fmaster' "${FAKE_DELETED}")" "2"
check "no raw slashes in the ref" "$(grep -c 'ref=refs/heads' "${FAKE_DELETED}" || true)" "0"

# 8. A missing prerequisite is a statement about the runner, and it travels the
#    same guard as an absent curl or python3. Those two cannot be simulated by
#    emptying PATH — the script's own interpreter would go with them — so the
#    guard is exercised through the one prerequisite a test can withhold.
#    The real case is on record: the container job had no `gh` and reported it
#    rather than pretending it had pruned.
export FAKE_DELETED="${WORK}/d8"; : > "${FAKE_DELETED}"
out=$(GITHUB_REPOSITORY='' GH_TOKEN=x "${PRUNE}" "ccache-Linux-GCC-16-clean-" 2>&1)
check "missing prerequisite is named" "$(printf '%s' "${out}" | grep -c 'GITHUB_REPOSITORY is not set')" "1"
check "missing prerequisite deletes nothing" "$(grep -c . "${FAKE_DELETED}" || true)" "0"
check "missing prerequisite is not 'nothing superseded'" \
     "$(printf '%s' "${out}" | grep -c 'nothing superseded')" "0"


# 9. curl failing outright. --write-out has already printed 000, so a second one
#    appended by the caller would make the status 000000 — a value no branch
#    matches, reported as an answer the server never gave. Both the single 000
#    and curl's own message have to survive.
export FAKE_DELETED="${WORK}/d9"; : > "${FAKE_DELETED}"
out=$(FAKE_CURL_FAIL=1 GH_TOKEN=x "${PRUNE}" "ccache-Linux-GCC-16-clean-" 2>&1)
check "curl failure says unreachable" "$(printf '%s' "${out}" | grep -c 'could not be reached')" "1"
check "curl failure keeps its exit code" "$(printf '%s' "${out}" | grep -c 'curl exit 6')" "1"
check "curl failure keeps its message" "$(printf '%s' "${out}" | grep -c 'Could not resolve host')" "1"
check "curl failure is not a 000000 status" "$(printf '%s' "${out}" | grep -c '000000')" "0"
check "curl failure deletes nothing" "$(grep -c . "${FAKE_DELETED}" || true)" "0"

# 10. An inventory that does not fit on one page. Pruning from the first hundred
#     would keep the newest of what it happened to see — for a family whose
#     entries all sit on page two, that is nothing at all, silently.
cat > "${WORK}/page1.json" <<'JSON'
{"total_count":102,"actions_caches":[
 {"key":"ccache-Other-999","ref":"refs/heads/master","size_in_bytes":1,"created_at":"2026-08-08T00:00:00Z"}
]}
JSON
cat > "${WORK}/page2.json" <<'JSON'
{"total_count":102,"actions_caches":[
 {"key":"ccache-Linux-GCC-16-clean-700","ref":"refs/heads/master","size_in_bytes":700000000,"created_at":"2026-08-08T10:00:00Z"},
 {"key":"ccache-Linux-GCC-16-clean-800","ref":"refs/heads/master","size_in_bytes":800000000,"created_at":"2026-08-08T11:00:00Z"}
]}
JSON
export FAKE_DELETED="${WORK}/d10"; : > "${FAKE_DELETED}"
out=$(FAKE_CACHES="${WORK}/page1.json" FAKE_CACHES_P2="${WORK}/page2.json" \
      GH_TOKEN=x "${PRUNE}" "ccache-Linux-GCC-16-clean-" 2>&1)
check "second page is read" "$(printf '%s' "${out}" | grep -c 'kept ccache-Linux-GCC-16-clean-800')" "1"
check "second page is pruned" "$(grep -c 'clean-700' "${FAKE_DELETED}")" "1"

if [ "${failures}" -ne 0 ]; then
    echo "ccache-prune-selftest: ${failures} failure(s)"
    exit 1
fi
echo "ccache-prune-selftest: all checks passed"
