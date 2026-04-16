// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/utxo.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/chain/utxo.hpp>

// Conversion functions
kth::domain::chain::utxo& kth_chain_utxo_mut_cpp(kth_utxo_mut_t o) {
    return *static_cast<kth::domain::chain::utxo*>(o);
}
kth::domain::chain::utxo const& kth_chain_utxo_const_cpp(kth_utxo_const_t o) {
    return *static_cast<kth::domain::chain::utxo const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_utxo_mut_t kth_chain_utxo_construct_default(void) {
    return new kth::domain::chain::utxo();
}

kth_utxo_mut_t kth_chain_utxo_construct(kth_output_point_const_t point, uint64_t amount, kth_token_data_const_t token_data) {
    KTH_PRECONDITION(point != nullptr);
    auto const& point_cpp = kth_chain_output_point_const_cpp(point);
    auto const token_data_cpp = kth::optional_cpp_ref<kth::domain::chain::token_data_t>(token_data);
    return kth::make_leaked<kth::domain::chain::utxo>(point_cpp, amount, token_data_cpp);
}


// Destructor

void kth_chain_utxo_destruct(kth_utxo_mut_t self) {
    if (self == nullptr) return;
    delete &kth_chain_utxo_mut_cpp(self);
}


// Copy

kth_utxo_mut_t kth_chain_utxo_copy(kth_utxo_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::chain::utxo(kth_chain_utxo_const_cpp(self));
}


// Equality

kth_bool_t kth_chain_utxo_equals(kth_utxo_const_t self, kth_utxo_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::bool_to_int(kth_chain_utxo_const_cpp(self) == kth_chain_utxo_const_cpp(other));
}


// Getters

uint32_t kth_chain_utxo_height(kth_utxo_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_chain_utxo_const_cpp(self).height();
}

kth_output_point_const_t kth_chain_utxo_point(kth_utxo_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth_chain_utxo_const_cpp(self).point());
}

uint64_t kth_chain_utxo_amount(kth_utxo_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_chain_utxo_const_cpp(self).amount();
}

kth_token_data_const_t kth_chain_utxo_token_data(kth_utxo_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const& opt = kth_chain_utxo_const_cpp(self).token_data();
    return opt.has_value() ? &(*opt) : nullptr;
}


// Setters

void kth_chain_utxo_set_height(kth_utxo_mut_t self, uint32_t height) {
    KTH_PRECONDITION(self != nullptr);
    kth_chain_utxo_mut_cpp(self).set_height(height);
}

void kth_chain_utxo_set_point(kth_utxo_mut_t self, kth_output_point_const_t point) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(point != nullptr);
    auto const& point_cpp = kth_chain_output_point_const_cpp(point);
    kth_chain_utxo_mut_cpp(self).set_point(point_cpp);
}

void kth_chain_utxo_set_amount(kth_utxo_mut_t self, uint64_t amount) {
    KTH_PRECONDITION(self != nullptr);
    kth_chain_utxo_mut_cpp(self).set_amount(amount);
}

void kth_chain_utxo_set_token_data(kth_utxo_mut_t self, kth_token_data_const_t token_data) {
    KTH_PRECONDITION(self != nullptr);
    auto const token_data_cpp = kth::optional_cpp_ref<kth::domain::chain::token_data_t>(token_data);
    kth_chain_utxo_mut_cpp(self).set_token_data(token_data_cpp);
}

} // extern "C"
