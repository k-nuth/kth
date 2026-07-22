// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/chain_mining.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/capi/detail/sync_wait.hpp>
#include <system_error>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
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
    auto const result = kth::capi::sync_wait(kth::cpp_ref<cpp_t>(self), kth::cpp_ref<cpp_t>(self).fetch_mining_info());
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::to_c_struct<kth_mining_info_t>(*result);
    return kth_ec_success;
}

void kth_chain_async_mining_info(kth_chain_t self, void* ctx, kth_mining_info_fetch_handler_t handler) {
    KTH_PRECONDITION(self != nullptr);
    auto& bc = kth::cpp_ref<cpp_t>(self);
    ::asio::co_spawn(bc.executor(), [&bc, self, ctx, handler]() -> ::asio::awaitable<void> {
        auto result = co_await bc.fetch_mining_info();
        if (result) {
            handler(self, ctx, kth::to_c_err(std::error_code{}), kth::to_c_struct<kth_mining_info_t>(*result));
        } else {
            handler(self, ctx, kth::to_c_err(result.error()), kth_mining_info_t{});
        }
    }, ::asio::detached);
}

} // extern "C"
