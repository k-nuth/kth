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

cat > "${WORK}/gh" <<'STUB'
#!/usr/bin/env bash
if [ "$1" = "api" ] && [ "$2" != "-X" ]; then
    cat "${FAKE_CACHES}"
    exit 0
fi
if [ "$1" = "api" ] && [ "$2" = "-X" ] && [ "$3" = "DELETE" ]; then
    echo "$4" >> "${FAKE_DELETED}"
    exit 0
fi
exit 1
STUB
chmod +x "${WORK}/gh"

cat > "${WORK}/caches.json" <<'JSON'
{"actions_caches":[
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
check "unparseable listing warns" "$(printf '%s' "${out}" | grep -c 'nothing was examined')" "1"
check "unparseable listing deletes nothing" "$(grep -c . "${FAKE_DELETED}" || true)" "0"
check "unparseable is not reported as superseded-free" \
     "$(printf '%s' "${out}" | grep -c 'nothing superseded')" "0"

# 4. No token: same discipline.
export FAKE_DELETED="${WORK}/d4"; : > "${FAKE_DELETED}"
out=$(GH_TOKEN='' "${PRUNE}" "ccache-Linux-GCC-16-clean-" 2>&1)
check "no token warns" "$(printf '%s' "${out}" | grep -c 'nothing was examined')" "1"
check "no token deletes nothing" "$(grep -c . "${FAKE_DELETED}" || true)" "0"

if [ "${failures}" -ne 0 ]; then
    echo "ccache-prune-selftest: ${failures} failure(s)"
    exit 1
fi
echo "ccache-prune-selftest: all checks passed"
