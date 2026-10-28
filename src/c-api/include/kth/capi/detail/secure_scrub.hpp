// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_DETAIL_SECURE_SCRUB_HPP_
#define KTH_CAPI_DETAIL_SECURE_SCRUB_HPP_

// Scope guard that wipes stack copies of sensitive material.

#include <cstddef>

#include <kth/capi/secure_memory.h>

namespace kth {

// RAII scrub for stack-local copies of sensitive material (keys, seeds,
// hashes of secrets) that the C-API impl materialises from a C input. The
// C-API leaves those copies on its own stack frame; a guard scoped for the
// lifetime of the local wipes them on every return path (including
// exceptions propagating past a precondition or an expected error-return)
// so the material does not linger beyond the call.
//
// Declared non-copyable / non-movable so accidental copies can't
// duplicate the pointer in multiple guards that would double-scrub
// (harmless but confusing if later changed to do more).
struct secure_scrub {
    void* p;
    std::size_t n;

    secure_scrub(void* p_, std::size_t n_) noexcept : p(p_), n(n_) {}
    ~secure_scrub() noexcept { kth_core_secure_zero(p, n); }

    secure_scrub(secure_scrub const&) = delete;
    secure_scrub(secure_scrub&&) = delete;
    secure_scrub& operator=(secure_scrub const&) = delete;
    secure_scrub& operator=(secure_scrub&&) = delete;
};

} // namespace kth

#endif /* KTH_CAPI_DETAIL_SECURE_SCRUB_HPP_ */
