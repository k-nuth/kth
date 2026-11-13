// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying file COPYING.
#ifndef KTH_CAPI_DETAIL_ASYNC_EXCEPTION_HPP_
#define KTH_CAPI_DETAIL_ASYNC_EXCEPTION_HPP_

#include <exception>
#include <new>
#include <utility>
#include <type_traits>
#include <kth/capi/error.h>

namespace kth::capi {
inline kth_error_code_t async_exception_status(std::exception_ptr failure) noexcept {
    try {
        std::rethrow_exception(failure);
    } catch (std::bad_alloc const&) {
        return kth_ec_out_of_memory;
    } catch (...) {
        return kth_ec_unhandled_exception;
    }
}

// The work returns its native result. Only this completion adapter delivers it.
// Conversion failures are caught here as well as exceptions from the coroutine.
// Failure delivery must not throw; C callbacks must never throw.
template <class Deliver, class Failure>
struct async_completion {
    Deliver deliver;
    Failure failure;

    void fail(std::exception_ptr error) const noexcept {
        failure(async_exception_status(error));
    }

    template <class Result>
    void operator()(std::exception_ptr error, Result result) const noexcept {
        // Asio moves/default-constructs results before entering this handler.
        // Those steps cannot be covered by the conversion catch below.
        static_assert(std::is_nothrow_move_constructible_v<Result>);
        static_assert(std::is_nothrow_default_constructible_v<Result>);
        if (error) {
            fail(error);
            return;
        }
        try {
            deliver(std::move(result));
        } catch (...) {
            fail(std::current_exception());
        }
    }
};

} // namespace kth::capi
#endif
