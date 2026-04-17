// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/utxo.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/chain/utxo.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::chain::utxo;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_utxo_mut_t kth_chain_utxo_construct_default(void) {
    return kth::leak<cpp_t>();
}

kth_utxo_mut_t kth_chain_utxo_construct(kth_output_point_const_t point, uint64_t amount, kth_token_data_const_t token_data) {
    KTH_PRECONDITION(point != nullptr);
    auto const& point_cpp = kth::cpp_ref<kth::domain::chain::output_point>(point);
    auto const token_data_cpp = kth::optional_cpp_ref<kth::domain::chain::token_data_t>(token_data);
    return kth::leak<cpp_t>(point_cpp, amount, token_data_cpp);
}


// Destructor

void kth_chain_utxo_destruct(kth_utxo_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_utxo_mut_t kth_chain_utxo_copy(kth_utxo_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_chain_utxo_equals(kth_utxo_const_t self, kth_utxo_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}


// Getters

uint32_t kth_chain_utxo_height(kth_utxo_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).height();
}

kth_output_point_const_t kth_chain_utxo_point(kth_utxo_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).point());
}

uint64_t kth_chain_utxo_amount(kth_utxo_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).amount();
}

kth_token_data_const_t kth_chain_utxo_token_data(kth_utxo_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const& opt = kth::cpp_ref<cpp_t>(self).token_data();
    return opt.has_value() ? &(*opt) : nullptr;
}


// Setters

void kth_chain_utxo_set_height(kth_utxo_mut_t self, uint32_t height) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).set_height(height);
}

void kth_chain_utxo_set_point(kth_utxo_mut_t self, kth_output_point_const_t point) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(point != nullptr);
    auto const& point_cpp = kth::cpp_ref<kth::domain::chain::output_point>(point);
    kth::cpp_ref<cpp_t>(self).set_point(point_cpp);
}

void kth_chain_utxo_set_amount(kth_utxo_mut_t self, uint64_t amount) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).set_amount(amount);
}

void kth_chain_utxo_set_token_data(kth_utxo_mut_t self, kth_token_data_const_t token_data) {
    KTH_PRECONDITION(self != nullptr);
    auto const token_data_cpp = kth::optional_cpp_ref<kth::domain::chain::token_data_t>(token_data);
    kth::cpp_ref<cpp_t>(self).set_token_data(token_data_cpp);
}

} // extern "C"
