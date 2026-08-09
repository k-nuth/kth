#!/usr/bin/env bash
# Export the recipe and print the revision it produced.
#
# The revision is a hash of the exported bytes, so it answers a question no
# other check here asks: do Linux, macOS and Windows agree on what this commit
# publishes? A version number is a label; the revision is the content.
#
# Nothing is uploaded and no secret is read. `conan export` writes to the local
# cache and stops.
#
# Usage: recipe-revision.sh <version> <output-file>

set -uo pipefail

VERSION="${1:?usage: recipe-revision.sh <version> <output-file>}"
OUTPUT="${2:?usage: recipe-revision.sh <version> <output-file>}"

# Pinned, and checked rather than trusted. The revision is computed by the
# client, so two different conan versions can disagree about the same bytes —
# and a comparison across platforms running different clients would be a
# comparison of conan versions wearing the costume of a source check.
EXPECTED_CONAN="2.31.2"

fail() {
    # Every failure here is "could not verify", and none of them may reach the
    # comparison as agreement. Missing evidence is not equality.
    echo "::error::recipe-revision: $1"
    exit 1
}

command -v conan >/dev/null 2>&1 || fail "conan is not installed"
command -v python3 >/dev/null 2>&1 || fail "python3 is not installed"

actual_conan=$(conan --version 2>/dev/null | tr -d '\r' | awk '{print $NF}')
if [ "${actual_conan}" != "${EXPECTED_CONAN}" ]; then
    fail "conan ${EXPECTED_CONAN} was pinned but ${actual_conan:-nothing} is on PATH; the revisions from this run would not be comparable"
fi

echo "recipe-revision: conan ${actual_conan} on $(uname -s 2>/dev/null || echo unknown)"

export_json="$(mktemp)"
trap 'rm -f "${export_json}"' EXIT

if ! conan export . --version "${VERSION}" --format=json > "${export_json}" 2>"${export_json}.err"; then
    echo "----- conan export output -----"
    cat "${export_json}.err" 2>/dev/null
    fail "the export failed, so this platform has no revision to compare"
fi

reference=$(EXPORT_JSON="${export_json}" python3 -c '
import json, os, sys
with open(os.environ["EXPORT_JSON"]) as handle:
    data = json.load(handle)
ref = data.get("reference", "")
if "#" not in ref:
    sys.exit("no revision in the exported reference: " + repr(ref))
sys.stdout.write(ref)
') || fail "the exported reference could not be read"

revision="${reference#*#}"
[ -n "${revision}" ] || fail "the exported reference carried no revision"

printf '%s\n' "${revision}" > "${OUTPUT}"

echo "recipe-revision: reference ${reference}"
echo "recipe-revision: revision  ${revision}"
