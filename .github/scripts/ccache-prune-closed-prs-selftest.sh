#!/usr/bin/env bash
# Exercises ccache-prune-closed-prs.sh against a synthetic inventory.
#
# The check that matters is the one that must never fire: an open pull request's
# cache is worth a full cold build to whoever is waiting on it, and this script
# deletes things. Every refusal below is a case where deleting would have been
# cheap to write and expensive to be wrong about.
#
# Runs under the platform's own /bin/bash, which on the macOS runners is 3.2.

set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PRUNE="${HERE}/ccache-prune-closed-prs.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

echo "ccache-prune-closed-prs-selftest: bash ${BASH_VERSION}"

# A curl that answers from files. It honours --output and --write-out, so the
# status handling under test is the real one.
cat > "${WORK}/curl" <<'STUB'
#!/usr/bin/env bash
out=""; method="GET"; url=""; prev=""
for arg in "$@"; do
    case "${prev}" in
        --output) out="${arg}" ;;
        --request) method="${arg}" ;;
    esac
    case "${arg}" in https://*|http://*) url="${arg}" ;; esac
    prev="${arg}"
done

case "${url}" in
    *"/actions/caches?per_page"*)
        cat "${FAKE_CACHES}" > "${out}"; printf '%s' "${FAKE_LIST_STATUS:-200}"; exit 0 ;;
    *"/pulls/"*)
        number="${url##*/pulls/}"
        if [ "${FAKE_PR_STATUS:-200}" != "200" ]; then
            : > "${out}"; printf '%s' "${FAKE_PR_STATUS}"; exit 0
        fi
        state="closed"
        case " ${FAKE_OPEN_PRS:-} " in *" ${number} "*) state="open" ;; esac
        printf '{"state":"%s"}' "${state}" > "${out}"
        printf '200'; exit 0 ;;
    *"/actions/caches?key="*)
        echo "${url}" >> "${FAKE_DELETED}"; : > "${out}"
        printf '%s' "${FAKE_DELETE_STATUS:-204}"; exit 0 ;;
esac
exit 1
STUB
chmod +x "${WORK}/curl"

cat > "${WORK}/caches.json" <<'JSON'
{"total_count":4,"actions_caches":[
 {"key":"ccache-coverage-aaa","ref":"refs/pull/100/merge","size_in_bytes":2230000000,"created_at":"2026-08-09T10:00:00Z"},
 {"key":"ccache-coverage-bbb","ref":"refs/pull/200/merge","size_in_bytes":2330000000,"created_at":"2026-08-09T11:00:00Z"},
 {"key":"ccache-coverage-ccc","ref":"refs/heads/master","size_in_bytes":2230000000,"created_at":"2026-08-09T12:00:00Z"},
 {"key":"ccache-Linux-GCC-16-clean-ddd","ref":"refs/heads/master","size_in_bytes":830000000,"created_at":"2026-08-09T12:00:00Z"}
]}
JSON

export PATH="${WORK}:${PATH}"
export GITHUB_REPOSITORY="k-nuth/kth"
export GITHUB_API_URL="https://api.github.com"
export FAKE_CACHES="${WORK}/caches.json"

failures=0
check() {
    if [ "$2" = "$3" ]; then
        echo "  ok   $1"
    else
        echo "  FAIL $1: expected [$3], got [$2]"
        failures=$(( failures + 1 ))
    fi
}

# 1. Both pull requests closed: both caches go, and neither branch cache is
#    touched. Branches are the family prune's business.
export FAKE_DELETED="${WORK}/d1"; : > "${FAKE_DELETED}"
out=$(GH_TOKEN=x "${PRUNE}" 2>&1)
check "removes both closed-PR caches" "$(grep -c . "${FAKE_DELETED}")" "2"
check "never touches a branch cache" "$(grep -c 'refs%2Fheads' "${FAKE_DELETED}" || true)" "0"
check "reports what it freed" "$(printf '%s' "${out}" | grep -c 'freed 4560 MB')" "1"

# 2. THE CONTROL. One of them is open. Its cache must survive, and the closed
#    one must still go — a blanket refusal would pass check 2 while doing
#    nothing useful.
export FAKE_DELETED="${WORK}/d2"; : > "${FAKE_DELETED}"
out=$(FAKE_OPEN_PRS="100" GH_TOKEN=x "${PRUNE}" 2>&1)
check "an OPEN pull request's cache is never deleted" \
     "$(grep -c 'ccache-coverage-aaa' "${FAKE_DELETED}" || true)" "0"
check "the open one is named as kept" "$(printf '%s' "${out}" | grep -c 'PR #100 is open')" "1"
check "the closed one is still removed" \
     "$(grep -c 'ccache-coverage-bbb' "${FAKE_DELETED}")" "1"
check "the count of kept-open is reported" "$(printf '%s' "${out}" | grep -c 'kept 1 open')" "1"

# 3. Every pull request open: nothing is deleted at all.
export FAKE_DELETED="${WORK}/d3"; : > "${FAKE_DELETED}"
out=$(FAKE_OPEN_PRS="100 200" GH_TOKEN=x "${PRUNE}" 2>&1)
check "all open deletes nothing" "$(grep -c . "${FAKE_DELETED}" || true)" "0"
check "all open frees nothing" "$(printf '%s' "${out}" | grep -c 'freed 0 MB')" "1"

# 4. A pull request whose state cannot be read is NOT closed. This is the case
#    that would be easiest to get wrong: treating an API hiccup as permission to
#    delete a live branch's cache.
export FAKE_DELETED="${WORK}/d4"; : > "${FAKE_DELETED}"
out=$(FAKE_PR_STATUS=500 GH_TOKEN=x "${PRUNE}" 2>&1)
check "unreadable PR state deletes nothing" "$(grep -c . "${FAKE_DELETED}" || true)" "0"
check "unreadable PR state says unknown is not closed" \
     "$(printf '%s' "${out}" | grep -c 'unknown is not closed')" "2"
check "unresolved are counted" "$(printf '%s' "${out}" | grep -c 'kept 2 unresolved')" "1"

# 5. The inventory itself failing is not an empty inventory.
export FAKE_DELETED="${WORK}/d5"; : > "${FAKE_DELETED}"
out=$(FAKE_LIST_STATUS=403 GH_TOKEN=x "${PRUNE}" 2>&1)
check "a refused listing is named" "$(printf '%s' "${out}" | grep -c 'lacks actions permission')" "1"
check "a refused listing deletes nothing" "$(grep -c . "${FAKE_DELETED}" || true)" "0"
check "a refused listing is not 'nothing to consider'" \
     "$(printf '%s' "${out}" | grep -c 'nothing to consider')" "0"

# 6. No pull-request-scoped cache at all is a clean, honest answer.
cat > "${WORK}/branches.json" <<'JSON'
{"total_count":1,"actions_caches":[
 {"key":"ccache-coverage-ccc","ref":"refs/heads/master","size_in_bytes":2230000000,"created_at":"2026-08-09T12:00:00Z"}
]}
JSON
export FAKE_DELETED="${WORK}/d6"; : > "${FAKE_DELETED}"
out=$(FAKE_CACHES="${WORK}/branches.json" GH_TOKEN=x "${PRUNE}" 2>&1)
check "branch-only inventory deletes nothing" "$(grep -c . "${FAKE_DELETED}" || true)" "0"
check "branch-only inventory says so" "$(printf '%s' "${out}" | grep -c 'nothing to consider')" "1"

# 7. A refused DELETE is named and does not inflate the freed total.
export FAKE_DELETED="${WORK}/d7"; : > "${FAKE_DELETED}"
out=$(FAKE_DELETE_STATUS=403 GH_TOKEN=x "${PRUNE}" 2>&1)
check "refused delete is named" "$(printf '%s' "${out}" | grep -c 'could not remove')" "2"
check "refused delete frees nothing" "$(printf '%s' "${out}" | grep -c 'freed 0 MB')" "1"

# 8. The ref is percent-encoded before it reaches the API.
export FAKE_DELETED="${WORK}/d8"; : > "${FAKE_DELETED}"
out=$(GH_TOKEN=x "${PRUNE}" 2>&1)
check "ref is encoded" "$(grep -c 'ref=refs%2Fpull%2F' "${FAKE_DELETED}")" "2"
check "no raw slashes in the ref" "$(grep -c 'ref=refs/pull' "${FAKE_DELETED}" || true)" "0"

if [ "${failures}" -ne 0 ]; then
    echo "ccache-prune-closed-prs-selftest: ${failures} failure(s)"
    exit 1
fi
echo "ccache-prune-closed-prs-selftest: all checks passed"
