// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_SECURE_MEMORY_H_
#define KTH_CAPI_SECURE_MEMORY_H_

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Zero `n` bytes at `p` such that the compiler cannot optimize the
 *        write away.
 *
 * High-level bindings (py-native, cs-api, ...) use this on their own
 * stack-local value_struct buffers that carried cryptographic material
 * (a raw secret, a WIF, an HD private key, an encrypted seed) during a
 * C-API call. Scrubbing those locals on the way out closes the window
 * where a memory dump, attached debugger, or swap-to-disk could recover
 * the bytes from a no-longer-live stack frame.
 *
 * Routes to the platform's non-optimizable scrub primitive:
 *   - glibc >= 2.25, BSD, macOS >= 10.12: `explicit_bzero`
 *   - Windows: `SecureZeroMemory`
 *   - C11 Annex K (when opted-in): `memset_s`
 *   - Fallback: a `volatile`-pointer byte loop the optimizer cannot
 *     collapse into a dead-store.
 *
 * Out of scope: side-channel leaks (cache, speculation, register
 * spills the compiler chose). For those a pure scrub primitive is
 * not a mitigation — binding authors need higher-level techniques.
 *
 * @param p Caller-owned buffer to wipe. Must not alias executable
 *          memory. NULL or `n == 0` is a no-op.
 * @param n Number of bytes to wipe.
 */
KTH_EXPORT
void kth_core_secure_zero(void* p, kth_size_t n);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_SECURE_MEMORY_H_ */
