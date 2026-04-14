// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/chain/merkle_block.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/message/merkle_block.hpp>

// Conversion functions
kth::domain::message::merkle_block& kth_chain_merkle_block_mut_cpp(kth_merkle_block_mut_t o) {
    return *static_cast<kth::domain::message::merkle_block*>(o);
}
kth::domain::message::merkle_block const& kth_chain_merkle_block_const_cpp(kth_merkle_block_const_t o) {
    return *static_cast<kth::domain::message::merkle_block const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_merkle_block_mut_t kth_chain_merkle_block_construct_default(void) {
    return new kth::domain::message::merkle_block();
}

kth_error_code_t kth_chain_merkle_block_construct_from_data(uint8_t const* data, kth_size_t n, uint32_t version, KTH_OUT_OWNED kth_merkle_block_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, static_cast<size_t>(n)));
    auto result = kth::domain::message::merkle_block::from_data(data_cpp, version);
    if ( ! result) return static_cast<kth_error_code_t>(result.error().value());
    *out = kth::make_leaked(std::move(*result));
    return kth_ec_success;
}

kth_merkle_block_mut_t kth_chain_merkle_block_construct_from_header_total_transactions_hashes_flags(kth_header_const_t header, kth_size_t total_transactions, kth_hash_list_const_t hashes, uint8_t const* flags, kth_size_t n) {
    KTH_PRECONDITION(header != nullptr);
    KTH_PRECONDITION(hashes != nullptr);
    KTH_PRECONDITION(flags != nullptr || n == 0);
    auto const& header_cpp = kth_chain_header_const_cpp(header);
    auto const total_transactions_cpp = static_cast<size_t>(total_transactions);
    auto const& hashes_cpp = kth_core_hash_list_const_cpp(hashes);
    auto const flags_cpp = n != 0 ? kth::data_chunk(flags, flags + n) : kth::data_chunk{};
    return kth::make_leaked_if_valid(kth::domain::message::merkle_block(header_cpp, total_transactions_cpp, hashes_cpp, flags_cpp));
}

kth_merkle_block_mut_t kth_chain_merkle_block_construct_from_block(kth_block_const_t block) {
    KTH_PRECONDITION(block != nullptr);
    auto const& block_cpp = kth_chain_block_const_cpp(block);
    return kth::make_leaked_if_valid(kth::domain::message::merkle_block(block_cpp));
}


// Destructor

void kth_chain_merkle_block_destruct(kth_merkle_block_mut_t self) {
    if (self == nullptr) return;
    delete &kth_chain_merkle_block_mut_cpp(self);
}


// Copy

kth_merkle_block_mut_t kth_chain_merkle_block_copy(kth_merkle_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::message::merkle_block(kth_chain_merkle_block_const_cpp(self));
}


// Equality

kth_bool_t kth_chain_merkle_block_equals(kth_merkle_block_const_t self, kth_merkle_block_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::bool_to_int(kth_chain_merkle_block_const_cpp(self) == kth_chain_merkle_block_const_cpp(other));
}


// Serialization

uint8_t* kth_chain_merkle_block_to_data(kth_merkle_block_const_t self, uint32_t version, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const data = kth_chain_merkle_block_const_cpp(self).to_data(version);
    return kth::create_c_array(data, *out_size);
}

kth_size_t kth_chain_merkle_block_serialized_size(kth_merkle_block_const_t self, uint32_t version) {
    KTH_PRECONDITION(self != nullptr);
    return kth_chain_merkle_block_const_cpp(self).serialized_size(version);
}


// Getters

kth_header_const_t kth_chain_merkle_block_header(kth_merkle_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth_chain_merkle_block_const_cpp(self).header());
}

kth_size_t kth_chain_merkle_block_total_transactions(kth_merkle_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_chain_merkle_block_const_cpp(self).total_transactions();
}

kth_hash_list_const_t kth_chain_merkle_block_hashes(kth_merkle_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth_chain_merkle_block_const_cpp(self).hashes());
}

uint8_t* kth_chain_merkle_block_flags(kth_merkle_block_const_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const data = kth_chain_merkle_block_const_cpp(self).flags();
    return kth::create_c_array(data, *out_size);
}


// Setters

void kth_chain_merkle_block_set_header(kth_merkle_block_mut_t self, kth_header_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth_chain_header_const_cpp(value);
    kth_chain_merkle_block_mut_cpp(self).set_header(value_cpp);
}

void kth_chain_merkle_block_set_total_transactions(kth_merkle_block_mut_t self, kth_size_t value) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = static_cast<size_t>(value);
    kth_chain_merkle_block_mut_cpp(self).set_total_transactions(value_cpp);
}

void kth_chain_merkle_block_set_hashes(kth_merkle_block_mut_t self, kth_hash_list_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth_core_hash_list_const_cpp(value);
    kth_chain_merkle_block_mut_cpp(self).set_hashes(value_cpp);
}

void kth_chain_merkle_block_set_flags(kth_merkle_block_mut_t self, uint8_t const* value, kth_size_t n) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr || n == 0);
    auto const value_cpp = n != 0 ? kth::data_chunk(value, value + n) : kth::data_chunk{};
    kth_chain_merkle_block_mut_cpp(self).set_flags(value_cpp);
}


// Predicates

kth_bool_t kth_chain_merkle_block_is_valid(kth_merkle_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_merkle_block_const_cpp(self).is_valid());
}


// Operations

void kth_chain_merkle_block_reset(kth_merkle_block_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth_chain_merkle_block_mut_cpp(self).reset();
}

} // extern "C"
