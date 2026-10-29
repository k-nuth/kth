// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_DETAIL_EXCEPTION_BARRIER_HPP_
#define KTH_CAPI_DETAIL_EXCEPTION_BARRIER_HPP_

// Exception policy at the C boundary: a C++ exception must never unwind into a
// C caller. Compiled only with `KTH_CAPI_EXCEPTION_BARRIERS`, which is what
// the generated status-returning wrappers are emitted against.

#if defined(KTH_CAPI_EXCEPTION_BARRIERS)

#include <new>

#include <kth/capi/error.h>

namespace kth {

// Run a generated status-returning operation behind the C++ exception
// barrier. The generator gives the lambda a `noexcept` specification that
// reflects what it can prove, so provably non-throwing operations compile
// without any try/catch. Allocation failure maps to `kth_ec_out_of_memory`;
// any other exception maps to `kth_ec_unhandled_exception`.
// TODO(kth-tools#6): the `if constexpr` fast path below is not selected by the
// generated bindings yet: their operation lambdas are conservatively
// `noexcept(false)`. Enabling it needs a proof over the whole operation (the
// argument conversions, result inspection, error mapping and output
// publication), not just the C++ method's own exception specification.
// https://github.com/k-nuth/kth-tools/issues/6
template <typename Operation>
inline kth_error_code_t capi_invoke(Operation& operation) noexcept {
    if constexpr (noexcept(operation())) {
        return operation();
    } else {
        try {
            return operation();
        } catch (std::bad_alloc const&) {
            return kth_ec_out_of_memory;
        } catch (...) {
            return kth_ec_unhandled_exception;
        }
    }
}

} // namespace kth

#endif // KTH_CAPI_EXCEPTION_BARRIERS

#endif /* KTH_CAPI_DETAIL_EXCEPTION_BARRIER_HPP_ */
