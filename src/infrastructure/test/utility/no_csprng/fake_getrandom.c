// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// LD_PRELOAD shim that makes the system CSPRNG look absent, the way a kernel
// older than 3.17 or a seccomp-filtered sandbox would. Used by run.sh to prove
// that pseudo_random::check_available() actually detects the condition, and
// that fill_bytes aborts rather than handing back non-random bytes.
//
// We intercept the libc symbols rather than filter the syscall because that is
// exactly what pseudo_random.cpp calls; on a recent glibc the call may be
// serviced by the vDSO without ever entering the kernel.

#define _GNU_SOURCE

#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

ssize_t getrandom(void* buf, size_t buflen, unsigned int flags) {
    (void)buf;
    (void)buflen;
    (void)flags;
    errno = ENOSYS;
    return -1;
}

int getentropy(void* buf, size_t buflen) {
    (void)buf;
    (void)buflen;
    errno = ENOSYS;
    return -1;
}
