// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/wallet/coin_selection_result.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/coin_selection.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::wallet::coin_selection_result;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Destructor

void kth_wallet_coin_selection_result_destruct(kth_coin_selection_result_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_coin_selection_result_mut_t kth_wallet_coin_selection_result_copy(kth_coin_selection_result_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Getters

uint64_t kth_wallet_coin_selection_result_total_selected_bch(kth_coin_selection_result_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).total_selected_bch;
}

uint64_t kth_wallet_coin_selection_result_total_selected_token(kth_coin_selection_result_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).total_selected_token;
}

uint64_t kth_wallet_coin_selection_result_estimated_size(kth_coin_selection_result_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).estimated_size;
}

} // extern "C"
