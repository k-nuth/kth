// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <kth/capi/chain/utxo.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

// Conversion functions
kth::domain::chain::utxo& kth_chain_utxo_cpp(kth_utxo_mut_t o) {
    return *static_cast<kth::domain::chain::utxo*>(o);
}
kth::domain::chain::utxo const& kth_chain_utxo_const_cpp(kth_utxo_const_t o) {
    return *static_cast<kth::domain::chain::utxo const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

kth_utxo_mut_t kth_chain_utxo_construct_default() {
    return new kth::domain::chain::utxo();
}

void kth_chain_utxo_destruct(kth_utxo_mut_t utxo) {
    if (utxo == nullptr) return;
    delete &kth_chain_utxo_cpp(utxo);
}

uint32_t kth_chain_utxo_height(kth_utxo_const_t utxo) {
    return kth_chain_utxo_const_cpp(utxo).height();
}

kth_output_point_const_t kth_chain_utxo_point(kth_utxo_const_t utxo) {
    return &kth_chain_utxo_const_cpp(utxo).point();
}

uint64_t kth_chain_utxo_amount(kth_utxo_const_t utxo) {
    return kth_chain_utxo_const_cpp(utxo).amount();
}

kth_token_data_const_t kth_chain_utxo_token_data(kth_utxo_const_t utxo) {
    auto const& td = kth_chain_utxo_const_cpp(utxo).token_data();
    return td.has_value() ? &td.value() : nullptr;
}

void kth_chain_utxo_set_height(kth_utxo_mut_t utxo, uint32_t height) {
    kth_chain_utxo_cpp(utxo).set_height(height);
}

void kth_chain_utxo_set_point(kth_utxo_mut_t utxo, kth_output_point_const_t point) {
    kth_chain_utxo_cpp(utxo).set_point(kth_chain_output_point_const_cpp(point));
}

void kth_chain_utxo_set_amount(kth_utxo_mut_t utxo, uint64_t amount) {
    kth_chain_utxo_cpp(utxo).set_amount(amount);
}

void kth_chain_utxo_set_token_data(kth_utxo_mut_t utxo, kth_token_data_const_t token_data) {
    if (token_data == nullptr) {
        kth_chain_utxo_cpp(utxo).set_token_data(std::nullopt);
    } else {
        kth_chain_utxo_cpp(utxo).set_token_data(kth_chain_token_data_const_cpp(token_data));
    }
}

} // extern "C"
