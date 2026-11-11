// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/chain_mining.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/capi/detail/sync_wait.hpp>
#include <system_error>
#include <asio/co_spawn.hpp>
#include <exception>
#include <new>
#include <kth/blockchain/interface/block_chain.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::blockchain::block_chain;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Getters

kth_error_code_t kth_chain_sync_mining_info(kth_chain_t self, kth_mining_info_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto const result = kth::capi::sync_wait(kth::cpp_ref<cpp_t>(self), kth::cpp_ref<cpp_t>(self).fetch_mining_info());
        if ( ! result) return kth::to_c_err(result.error());
        *out = kth::to_c_struct<kth_mining_info_t>(*result);
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

void kth_chain_async_mining_info(kth_chain_t self, void* ctx, kth_mining_info_fetch_handler_t handler) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(handler != nullptr);
    auto report_failure = [self, ctx, handler](std::exception_ptr failure) {
        kth_error_code_t status = kth_ec_unhandled_exception;
        try { std::rethrow_exception(failure); }
        catch (std::bad_alloc const&) { status = kth_ec_out_of_memory; }
        catch (...) {}
        handler(self, ctx, status, kth_mining_info_t{});
    };
    try {
        auto& bc = kth::cpp_ref<cpp_t>(self);
        ::asio::co_spawn(bc.executor(), [&bc]() -> decltype(bc.fetch_mining_info()) {
            co_return co_await bc.fetch_mining_info();
        }, [self, ctx, handler, report_failure](std::exception_ptr failure, auto result) {
            if (failure) { report_failure(failure); return; }
            if (result) {
                handler(self, ctx, kth::to_c_err(std::error_code{}), kth::to_c_struct<kth_mining_info_t>(*result));
            } else {
                handler(self, ctx, kth::to_c_err(result.error()), kth_mining_info_t{});
            }
        });
    } catch (...) {
        report_failure(std::current_exception());
    }
}

} // extern "C"
