// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/chain_query.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/capi/detail/sync_wait.hpp>
#include <system_error>
#include <asio/co_spawn.hpp>
#include <exception>
#include <new>
#include <kth/blockchain/interface/block_chain.hpp>

namespace kth {
template <> inline
kth_block_progress_t to_c_struct<kth_block_progress_t, kth::blockchain::block_progress>(
    kth::blockchain::block_progress const& cpp) noexcept {
    kth_block_progress_t out{};
    out.height = cpp.height;
    out.ticks = cpp.ticks;
    out.target_time = cpp.target_time;
    out.next_target_time = cpp.next_target_time;
    out.confirmations = cpp.confirmations;
    out.confirmations_ticks = cpp.confirmations_ticks;
    out.block_hash = kth::to_hash_t(cpp.block_hash);
    return out;
}
} // namespace kth

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::blockchain::block_chain;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Getters

kth_error_code_t kth_chain_sync_chain_progress(kth_chain_t self, kth_block_progress_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto const result = kth::capi::sync_wait(kth::cpp_ref<cpp_t>(self), kth::cpp_ref<cpp_t>(self).fetch_chain_progress());
        if ( ! result) return kth::to_c_err(result.error());
        *out = kth::to_c_struct<kth_block_progress_t>(*result);
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

void kth_chain_async_chain_progress(kth_chain_t self, void* ctx, kth_chain_progress_fetch_handler_t handler) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(handler != nullptr);
    auto report_failure = [self, ctx, handler](std::exception_ptr failure) {
        kth_error_code_t status = kth_ec_unhandled_exception;
        try { std::rethrow_exception(failure); }
        catch (std::bad_alloc const&) { status = kth_ec_out_of_memory; }
        catch (...) {}
        handler(self, ctx, status, kth_block_progress_t{});
    };
    try {
        auto& bc = kth::cpp_ref<cpp_t>(self);
        ::asio::co_spawn(bc.executor(), [&bc]() -> decltype(bc.fetch_chain_progress()) {
            co_return co_await bc.fetch_chain_progress();
        }, [self, ctx, handler, report_failure](std::exception_ptr failure, auto result) {
            if (failure) { report_failure(failure); return; }
            if (result) {
                handler(self, ctx, kth::to_c_err(std::error_code{}), kth::to_c_struct<kth_block_progress_t>(*result));
            } else {
                handler(self, ctx, kth::to_c_err(result.error()), kth_block_progress_t{});
            }
        });
    } catch (...) {
        report_failure(std::current_exception());
    }
}


// Operations

kth_error_code_t kth_chain_sync_block_height(kth_chain_t self, kth_hash_t const* hash, kth_size_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(hash != nullptr);
    KTH_PRECONDITION(out != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto const hash_cpp = kth::hash_to_cpp(hash->hash);
        auto const result = kth::capi::sync_wait(kth::cpp_ref<cpp_t>(self), kth::cpp_ref<cpp_t>(self).fetch_block_height(hash_cpp));
        if ( ! result) return kth::to_c_err(result.error());
        *out = static_cast<kth_size_t>(*result);
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

kth_error_code_t kth_chain_sync_block_height_unsafe(kth_chain_t self, uint8_t const* hash, kth_size_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(hash != nullptr);
    KTH_PRECONDITION(out != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto const hash_cpp = kth::hash_to_cpp(hash);
        auto const result = kth::capi::sync_wait(kth::cpp_ref<cpp_t>(self), kth::cpp_ref<cpp_t>(self).fetch_block_height(hash_cpp));
        if ( ! result) return kth::to_c_err(result.error());
        *out = static_cast<kth_size_t>(*result);
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

void kth_chain_async_block_height(kth_chain_t self, void* ctx, kth_hash_t const* hash, kth_block_height_fetch_handler_t handler) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(hash != nullptr);
    KTH_PRECONDITION(handler != nullptr);
    auto report_failure = [self, ctx, handler](std::exception_ptr failure) {
        kth_error_code_t status = kth_ec_unhandled_exception;
        try { std::rethrow_exception(failure); }
        catch (std::bad_alloc const&) { status = kth_ec_out_of_memory; }
        catch (...) {}
        handler(self, ctx, status, kth_size_t{});
    };
    try {
        auto const arg0 = kth::hash_to_cpp(hash->hash);
        auto& bc = kth::cpp_ref<cpp_t>(self);
        ::asio::co_spawn(bc.executor(), [&bc, arg0]() -> decltype(bc.fetch_block_height(arg0)) {
            co_return co_await bc.fetch_block_height(arg0);
        }, [self, ctx, handler, report_failure](std::exception_ptr failure, auto result) {
            if (failure) { report_failure(failure); return; }
            if (result) {
                handler(self, ctx, kth::to_c_err(std::error_code{}), static_cast<kth_size_t>(*result));
            } else {
                handler(self, ctx, kth::to_c_err(result.error()), kth_size_t{});
            }
        });
    } catch (...) {
        report_failure(std::current_exception());
    }
}

kth_error_code_t kth_chain_sync_block_progress(kth_chain_t self, kth_hash_t const* hash, kth_block_progress_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(hash != nullptr);
    KTH_PRECONDITION(out != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto const hash_cpp = kth::hash_to_cpp(hash->hash);
        auto const result = kth::capi::sync_wait(kth::cpp_ref<cpp_t>(self), kth::cpp_ref<cpp_t>(self).fetch_block_progress(hash_cpp));
        if ( ! result) return kth::to_c_err(result.error());
        *out = kth::to_c_struct<kth_block_progress_t>(*result);
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

kth_error_code_t kth_chain_sync_block_progress_unsafe(kth_chain_t self, uint8_t const* hash, kth_block_progress_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(hash != nullptr);
    KTH_PRECONDITION(out != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto const hash_cpp = kth::hash_to_cpp(hash);
        auto const result = kth::capi::sync_wait(kth::cpp_ref<cpp_t>(self), kth::cpp_ref<cpp_t>(self).fetch_block_progress(hash_cpp));
        if ( ! result) return kth::to_c_err(result.error());
        *out = kth::to_c_struct<kth_block_progress_t>(*result);
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

void kth_chain_async_block_progress(kth_chain_t self, void* ctx, kth_hash_t const* hash, kth_block_progress_fetch_handler_t handler) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(hash != nullptr);
    KTH_PRECONDITION(handler != nullptr);
    auto report_failure = [self, ctx, handler](std::exception_ptr failure) {
        kth_error_code_t status = kth_ec_unhandled_exception;
        try { std::rethrow_exception(failure); }
        catch (std::bad_alloc const&) { status = kth_ec_out_of_memory; }
        catch (...) {}
        handler(self, ctx, status, kth_block_progress_t{});
    };
    try {
        auto const arg0 = kth::hash_to_cpp(hash->hash);
        auto& bc = kth::cpp_ref<cpp_t>(self);
        ::asio::co_spawn(bc.executor(), [&bc, arg0]() -> decltype(bc.fetch_block_progress(arg0)) {
            co_return co_await bc.fetch_block_progress(arg0);
        }, [self, ctx, handler, report_failure](std::exception_ptr failure, auto result) {
            if (failure) { report_failure(failure); return; }
            if (result) {
                handler(self, ctx, kth::to_c_err(std::error_code{}), kth::to_c_struct<kth_block_progress_t>(*result));
            } else {
                handler(self, ctx, kth::to_c_err(result.error()), kth_block_progress_t{});
            }
        });
    } catch (...) {
        report_failure(std::current_exception());
    }
}

} // extern "C"
