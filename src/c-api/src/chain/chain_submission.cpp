// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/chain_submission.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/capi/detail/sync_wait.hpp>
#include <kth/blockchain/interface/block_chain.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::blockchain::block_chain;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Operations

kth_error_code_t kth_chain_sync_submit_block(kth_chain_t self, kth_block_const_t block) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(block != nullptr);
    auto operation = [&]() -> kth_error_code_t {
        auto const& block_cpp = kth::cpp_ref<kth::domain::chain::block>(block);
        return kth::to_c_err(kth::capi::sync_wait(kth::cpp_ref<cpp_t>(self), kth::cpp_ref<cpp_t>(self).submit_block(block_cpp)).error);
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

} // extern "C"
