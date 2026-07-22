// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/chain_query.h>

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

// Operations

kth_error_code_t kth_chain_sync_block_height(kth_chain_t self, kth_hash_t const* hash, kth_size_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(hash != nullptr);
    KTH_PRECONDITION(out != nullptr);
    auto const hash_cpp = kth::hash_to_cpp(hash->hash);
    auto const result = kth::capi::sync_wait(kth::cpp_ref<cpp_t>(self), kth::cpp_ref<cpp_t>(self).fetch_block_height(hash_cpp));
    if ( ! result) return kth::to_c_err(result.error());
    *out = static_cast<kth_size_t>(*result);
    return kth_ec_success;
}

kth_error_code_t kth_chain_sync_block_height_unsafe(kth_chain_t self, uint8_t const* hash, kth_size_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(hash != nullptr);
    KTH_PRECONDITION(out != nullptr);
    auto const hash_cpp = kth::hash_to_cpp(hash);
    auto const result = kth::capi::sync_wait(kth::cpp_ref<cpp_t>(self), kth::cpp_ref<cpp_t>(self).fetch_block_height(hash_cpp));
    if ( ! result) return kth::to_c_err(result.error());
    *out = static_cast<kth_size_t>(*result);
    return kth_ec_success;
}

void kth_chain_async_block_height(kth_chain_t self, void* ctx, kth_hash_t const* hash, kth_block_height_fetch_handler_t handler) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(hash != nullptr);
    auto const arg0 = kth::hash_to_cpp(hash->hash);
    auto& bc = kth::cpp_ref<cpp_t>(self);
    ::asio::co_spawn(bc.executor(), [&bc, self, ctx, handler, arg0]() -> ::asio::awaitable<void> {
        auto result = co_await bc.fetch_block_height(arg0);
        if (result) {
            handler(self, ctx, kth::to_c_err(std::error_code{}), static_cast<kth_size_t>(*result));
        } else {
            handler(self, ctx, kth::to_c_err(result.error()), kth_size_t{});
        }
    }, ::asio::detached);
}

} // extern "C"
