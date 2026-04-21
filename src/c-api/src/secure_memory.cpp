// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/secure_memory.h>

#include <stddef.h>
#if defined(_WIN32)
#  include <windows.h>
#elif (defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 25))) \
    || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
// explicit_bzero is declared in <strings.h> (not <string.h>) on glibc
// and the BSDs. Including the wrong header slips an implicit-function-
// declaration warning that becomes a hard error under -Werror on
// Clang 16+ / GCC 14+.
#  include <strings.h>
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
    // scrub primitive. The `__GLIBC__ > 2 || ...` form is the safe
    // multi-component version check (a hypothetical glibc 3.0 would
    // still be covered).
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
