// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/secure_memory.h>

#include <stddef.h>
#if defined(_WIN32)
#  include <windows.h>
#endif

// explicit_bzero on glibc/BSD is declared in <strings.h>, but glibc
// gates the declaration behind `_DEFAULT_SOURCE` / `_GNU_SOURCE` /
// `_BSD_SOURCE`. Those macros can be turned OFF by a higher-priority
// macro the build system sets (e.g. `-D_POSIX_C_SOURCE=200809L` on
// a `-std=c++23` TU), leaving the header included but the symbol
// invisible — the failure mode that broke the first two attempts at
// this file. Sidestep the feature-test dance entirely by declaring
// the prototype ourselves: glibc >= 2.25 and the BSDs all export the
// symbol, so the link resolves. macOS, which lacks `explicit_bzero`
// altogether, falls through to the volatile-loop branch and never
// references the symbol.
#if !defined(_WIN32) && !defined(__APPLE__)
extern "C" void explicit_bzero(void* s, size_t n);
#endif

extern "C" {

void kth_core_secure_zero(void* p, kth_size_t n) {
    if (p == nullptr || n == 0) return;
#if defined(_WIN32)
    // Windows: documented non-optimizable scrub.
    SecureZeroMemory(p, n);
#elif (defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 25))) \
    || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    // glibc >= 2.25 and the BSDs ship `explicit_bzero` as a first-class
    // scrub primitive. We declared the prototype above to bypass any
    // feature-test macro gating that would otherwise hide it. The
    // `__GLIBC__ > 2 || ...` form is the safe multi-component version
    // check (a hypothetical glibc 3.0 would still be covered).
    explicit_bzero(p, n);
#else
    // Portable fallback (macOS, musl without explicit_bzero, exotic
    // targets). macOS does not expose `explicit_bzero`, and while its
    // libc provides `memset_s`, the C11 Annex K header dance
    // (`__STDC_WANT_LIB_EXT1__` + `<string.h>`) is not reliably
    // supported from C++ translation units across toolchains. Drop
    // down to the `volatile`-pointer byte loop — slower but correct
    // and portable.
    volatile unsigned char* vp = static_cast<volatile unsigned char*>(p);
    while (n--) *vp++ = 0;
#  if defined(__GNUC__) || defined(__clang__)
    // Belt-and-suspenders: empty asm with a memory clobber so LTO /
    // whole-program optimizers cannot reason past the `volatile`
    // boundary and prove the writes dead.
    __asm__ __volatile__("" : : "r"(p) : "memory");
#  endif
#endif
}

} // extern "C"
