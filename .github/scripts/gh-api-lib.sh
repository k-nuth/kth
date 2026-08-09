#!/usr/bin/env bash
# Shared GitHub REST plumbing for the cache scripts.
#
# One copy on purpose. The subtleties here are the ones that already went wrong
# once: `--write-out '%{http_code}'` prints 000 by itself when curl never
# reaches the host, so appending another gives 000000 — a status no branch
# matches, reported as an answer the server never gave. And `api_call` is used
# as `$(api_call ...)`, which is a subshell, so anything it assigns is lost:
# the exit code travels through a file.
#
# Sourced, not executed. The caller owns the failure policy.

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

# Returns the HTTP status on stdout and leaves the body in $2.
#
# The status and curl's own exit code are read separately on purpose. With
# `--write-out '%{http_code}'` curl ALREADY prints 000 when it never got an
# answer, so an `|| echo 000` on the end appends a second one and the caller
# sees `000000` — a status no branch matches, reported as "the API answered
# HTTP 000000". Whatever went wrong is then described as an answer that was
# never given.
# Through a file, not a variable: `api_call` is used as `$(api_call ...)`, which
# runs it in a subshell, so anything it assigns is gone by the time the caller
# reads it. The first attempt at this stored the exit code in a variable and the
# diagnostic silently reported `curl exit 0` for every failure.
api_call() {
    local method="$1" url="$2" body="$3"
    local status
    status=$(curl --silent --show-error --request "${method}" \
         --output "${body}" --write-out '%{http_code}' \
         --header "Authorization: Bearer ${GH_TOKEN}" \
         --header "Accept: application/vnd.github+json" \
         --header "X-GitHub-Api-Version: 2022-11-28" \
         "${url}" 2>"${WORK}/curl.err")
    local rc=$?
    printf '%s' "${rc}" > "${WORK}/curl.rc"

    if [ "${rc}" -ne 0 ]; then
        # One 000, whatever curl printed. The reason is not lost: the exit code
        # and the captured stderr both reach the diagnostic below.
        printf '000'
        return 0
    fi

    printf '%s' "${status}"
}

# Named apart because they call for different answers: a token that cannot read
# is a configuration problem, and a 500 is not.
describe_status() {
    case "$1" in
        401) echo "the token was rejected (401)" ;;
        403) echo "the token lacks actions permission, or the rate limit is exhausted (403)" ;;
        404) echo "the endpoint answered 404 — wrong repository, or caches are unavailable here" ;;
        000) echo "the API could not be reached (curl exit $(cat "${WORK}/curl.rc" 2>/dev/null || echo '?'): $(tr -d '\n' < "${WORK}/curl.err" 2>/dev/null))" ;;
        *)   echo "the API answered HTTP $1" ;;
    esac
}
