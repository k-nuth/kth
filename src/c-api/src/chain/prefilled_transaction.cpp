// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/chain/prefilled_transaction.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/message/prefilled_transaction.hpp>

// Conversion functions
kth::domain::message::prefilled_transaction& kth_chain_prefilled_transaction_mut_cpp(kth_prefilled_transaction_mut_t o) {
    return *static_cast<kth::domain::message::prefilled_transaction*>(o);
}
kth::domain::message::prefilled_transaction const& kth_chain_prefilled_transaction_const_cpp(kth_prefilled_transaction_const_t o) {
    return *static_cast<kth::domain::message::prefilled_transaction const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_prefilled_transaction_mut_t kth_chain_prefilled_transaction_construct_default(void) {
    return new kth::domain::message::prefilled_transaction();
}

kth_error_code_t kth_chain_prefilled_transaction_construct_from_data(uint8_t const* data, kth_size_t n, uint32_t version, KTH_OUT_OWNED kth_prefilled_transaction_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, static_cast<size_t>(n)));
    auto result = kth::domain::message::prefilled_transaction::from_data(data_cpp, version);
    if ( ! result) return static_cast<kth_error_code_t>(result.error().value());
    *out = kth::make_leaked(std::move(*result));
    return kth_ec_success;
}

kth_prefilled_transaction_mut_t kth_chain_prefilled_transaction_construct(uint64_t index, kth_transaction_const_t tx) {
    KTH_PRECONDITION(tx != nullptr);
    auto const& tx_cpp = kth_chain_transaction_const_cpp(tx);
    return kth::make_leaked_if_valid(kth::domain::message::prefilled_transaction(index, tx_cpp));
}


// Destructor

void kth_chain_prefilled_transaction_destruct(kth_prefilled_transaction_mut_t self) {
    if (self == nullptr) return;
    delete &kth_chain_prefilled_transaction_mut_cpp(self);
}


// Copy

kth_prefilled_transaction_mut_t kth_chain_prefilled_transaction_copy(kth_prefilled_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::message::prefilled_transaction(kth_chain_prefilled_transaction_const_cpp(self));
}


// Equality

kth_bool_t kth_chain_prefilled_transaction_equals(kth_prefilled_transaction_const_t self, kth_prefilled_transaction_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::bool_to_int(kth_chain_prefilled_transaction_const_cpp(self) == kth_chain_prefilled_transaction_const_cpp(other));
}


// Serialization

uint8_t* kth_chain_prefilled_transaction_to_data(kth_prefilled_transaction_const_t self, uint32_t version, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const data = kth_chain_prefilled_transaction_const_cpp(self).to_data(version);
    return kth::create_c_array(data, *out_size);
}

kth_size_t kth_chain_prefilled_transaction_serialized_size(kth_prefilled_transaction_const_t self, uint32_t version) {
    KTH_PRECONDITION(self != nullptr);
    return kth_chain_prefilled_transaction_const_cpp(self).serialized_size(version);
}


// Getters

uint64_t kth_chain_prefilled_transaction_index(kth_prefilled_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_chain_prefilled_transaction_const_cpp(self).index();
}

kth_transaction_const_t kth_chain_prefilled_transaction_transaction(kth_prefilled_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth_chain_prefilled_transaction_const_cpp(self).transaction());
}


// Setters

void kth_chain_prefilled_transaction_set_index(kth_prefilled_transaction_mut_t self, uint64_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth_chain_prefilled_transaction_mut_cpp(self).set_index(value);
}

void kth_chain_prefilled_transaction_set_transaction(kth_prefilled_transaction_mut_t self, kth_transaction_const_t tx) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(tx != nullptr);
    auto const& tx_cpp = kth_chain_transaction_const_cpp(tx);
    kth_chain_prefilled_transaction_mut_cpp(self).set_transaction(tx_cpp);
}


// Predicates

kth_bool_t kth_chain_prefilled_transaction_is_valid(kth_prefilled_transaction_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_prefilled_transaction_const_cpp(self).is_valid());
}


// Operations

void kth_chain_prefilled_transaction_reset(kth_prefilled_transaction_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth_chain_prefilled_transaction_mut_cpp(self).reset();
}

} // extern "C"
