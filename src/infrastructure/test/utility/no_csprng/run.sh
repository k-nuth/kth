#!/usr/bin/env bash
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Prove the CSPRNG startup probe works, by taking the CSPRNG away.
#
# pseudo_random::check_available() exists because a missing system CSPRNG is the
# one entropy failure that is real, environmental, and constant for the life of
# the process -- so it is caught once, at startup, and every draw afterwards is
# infallible. That argument is only worth anything if the probe actually fires,
# which cannot be shown on a machine whose CSPRNG works. So we remove it.
#
#   usage: run.sh <path-to-kth_infrastructure_test>
#
# Registered with ctest as kth_infrastructure_no_csprng_test on Linux. The trick
# is LD_PRELOAD symbol interposition: the dynamic linker resolves `getrandom` to
# the first definition it finds, and LD_PRELOAD puts ours ahead of libc's. That
# works because pseudo_random.cpp calls the libc symbol through the PLT -- a
# direct syscall(SYS_getrandom, ...) would need seccomp instead, and a statically
# linked libc cannot be interposed at all. Where it cannot take effect we exit 77
# (ctest SKIP) rather than report a failure we did not actually observe.

set -euo pipefail

readonly skip_exit=77

readonly test_binary="${1:-}"
if [[ -z "${test_binary}" || ! -x "${test_binary}" ]]; then
    echo "usage: $0 <path-to-kth_infrastructure_test>" >&2
    exit 2
fi

readonly here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly shim="$(mktemp -d)/libfakerandom.so"
trap 'rm -rf "$(dirname "${shim}")"' EXIT

cc -shared -fPIC -O2 "${here}/fake_getrandom.c" -o "${shim}"

fail() { echo "FAIL: $1" >&2; exit 1; }

# 0. The shim has to actually intercept, or every result below is a false pass.
if LD_PRELOAD="${shim}" "${test_binary}" \
        "pseudo random check_available succeeds on a working system" \
        >/dev/null 2>&1; then
    echo "SKIP: LD_PRELOAD did not take effect (statically linked libc?)." >&2
    echo "      Cannot remove the CSPRNG here, so nothing was verified." >&2
    exit "${skip_exit}"
fi
echo "ok: shim intercepts (probe fails when the CSPRNG is gone)"

# 1. Without the shim the probe passes, so a failure means something real.
"${test_binary}" "pseudo random check_available succeeds on a working system" \
    >/dev/null 2>&1 || fail "probe fails on this machine even with a working CSPRNG"
echo "ok: probe passes on a working CSPRNG"

# 2. A draw with no CSPRNG must abort, never return non-random bytes.
output="$(LD_PRELOAD="${shim}" "${test_binary}" \
    "pseudo random generate returns distinct values" 2>&1 || true)"

grep -q "FATAL: system CSPRNG failed" <<<"${output}" \
    || fail "fill_bytes did not report the deliberate abort; got: ${output}"

grep -q "errno=38" <<<"${output}" \
    || fail "expected ENOSYS (38) to be reported; got: ${output}"
echo "ok: a draw with no CSPRNG aborts loudly (ENOSYS), rather than returning zeros"

echo "PASS"
