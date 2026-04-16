// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/chain/compact_block.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/message/compact_block.hpp>

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_compact_block_mut_t kth_chain_compact_block_construct_default(void) {
    return new kth::domain::message::compact_block();
}

kth_error_code_t kth_chain_compact_block_construct_from_data(uint8_t const* data, kth_size_t n, uint32_t version, KTH_OUT_OWNED kth_compact_block_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, static_cast<size_t>(n)));
    auto result = kth::domain::message::compact_block::from_data(data_cpp, version);
    if ( ! result) return static_cast<kth_error_code_t>(result.error().value());
    *out = kth::make_leaked(std::move(*result));
    return kth_ec_success;
}

kth_compact_block_mut_t kth_chain_compact_block_construct(kth_header_const_t header, uint64_t nonce, kth_u64_list_const_t short_ids, kth_prefilled_transaction_list_const_t transactions) {
    KTH_PRECONDITION(header != nullptr);
    KTH_PRECONDITION(short_ids != nullptr);
    KTH_PRECONDITION(transactions != nullptr);
    auto const& header_cpp = kth::cpp_ref<kth::domain::chain::header>(header);
    auto const& short_ids_cpp = kth::cpp_ref<std::vector<uint64_t>>(short_ids);
    auto const& transactions_cpp = kth::cpp_ref<kth::domain::message::prefilled_transaction::list>(transactions);
    return kth::make_leaked<kth::domain::message::compact_block>(header_cpp, nonce, short_ids_cpp, transactions_cpp);
}


// Destructor

void kth_chain_compact_block_destruct(kth_compact_block_mut_t self) {
    if (self == nullptr) return;
    delete &kth::cpp_ref<kth::domain::message::compact_block>(self);
}


// Copy

kth_compact_block_mut_t kth_chain_compact_block_copy(kth_compact_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::message::compact_block(kth::cpp_ref<kth::domain::message::compact_block>(self));
}


// Equality

kth_bool_t kth_chain_compact_block_equals(kth_compact_block_const_t self, kth_compact_block_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::message::compact_block>(self) == kth::cpp_ref<kth::domain::message::compact_block>(other));
}


// Serialization

uint8_t* kth_chain_compact_block_to_data(kth_compact_block_const_t self, uint32_t version, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const data = kth::cpp_ref<kth::domain::message::compact_block>(self).to_data(version);
    return kth::create_c_array(data, *out_size);
}

kth_size_t kth_chain_compact_block_serialized_size(kth_compact_block_const_t self, uint32_t version) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<kth::domain::message::compact_block>(self).serialized_size(version);
}


// Getters

kth_header_const_t kth_chain_compact_block_header(kth_compact_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<kth::domain::message::compact_block>(self).header());
}

uint64_t kth_chain_compact_block_nonce(kth_compact_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<kth::domain::message::compact_block>(self).nonce();
}

kth_u64_list_const_t kth_chain_compact_block_short_ids(kth_compact_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<kth::domain::message::compact_block>(self).short_ids());
}

kth_prefilled_transaction_list_const_t kth_chain_compact_block_transactions(kth_compact_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<kth::domain::message::compact_block>(self).transactions());
}


// Setters

void kth_chain_compact_block_set_header(kth_compact_block_mut_t self, kth_header_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth::cpp_ref<kth::domain::chain::header>(value);
    kth::cpp_ref<kth::domain::message::compact_block>(self).set_header(value_cpp);
}

void kth_chain_compact_block_set_nonce(kth_compact_block_mut_t self, uint64_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<kth::domain::message::compact_block>(self).set_nonce(value);
}

void kth_chain_compact_block_set_short_ids(kth_compact_block_mut_t self, kth_u64_list_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth::cpp_ref<std::vector<uint64_t>>(value);
    kth::cpp_ref<kth::domain::message::compact_block>(self).set_short_ids(value_cpp);
}

void kth_chain_compact_block_set_transactions(kth_compact_block_mut_t self, kth_prefilled_transaction_list_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth::cpp_ref<kth::domain::message::prefilled_transaction::list>(value);
    kth::cpp_ref<kth::domain::message::compact_block>(self).set_transactions(value_cpp);
}


// Predicates

kth_bool_t kth_chain_compact_block_is_valid(kth_compact_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::message::compact_block>(self).is_valid());
}


// Operations

void kth_chain_compact_block_reset(kth_compact_block_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<kth::domain::message::compact_block>(self).reset();
}

} // extern "C"
