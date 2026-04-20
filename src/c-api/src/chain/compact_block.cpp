// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/chain/compact_block.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/message/compact_block.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::message::compact_block;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_compact_block_mut_t kth_chain_compact_block_construct_default(void) {
    return kth::leak<cpp_t>();
}

kth_error_code_t kth_chain_compact_block_construct_from_data(uint8_t const* data, kth_size_t n, uint32_t version, KTH_OUT_OWNED kth_compact_block_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, kth::sz(n)));
    auto result = cpp_t::from_data(data_cpp, version);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_compact_block_mut_t kth_chain_compact_block_construct(kth_header_const_t header, uint64_t nonce, kth_u64_list_const_t short_ids, kth_prefilled_transaction_list_const_t transactions) {
    KTH_PRECONDITION(header != nullptr);
    KTH_PRECONDITION(short_ids != nullptr);
    KTH_PRECONDITION(transactions != nullptr);
    auto const& header_cpp = kth::cpp_ref<kth::domain::chain::header>(header);
    auto const& short_ids_cpp = kth::cpp_ref<std::vector<uint64_t>>(short_ids);
    auto const& transactions_cpp = kth::cpp_ref<kth::domain::message::prefilled_transaction::list>(transactions);
    return kth::leak<cpp_t>(header_cpp, nonce, short_ids_cpp, transactions_cpp);
}


// Destructor

void kth_chain_compact_block_destruct(kth_compact_block_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_compact_block_mut_t kth_chain_compact_block_copy(kth_compact_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_chain_compact_block_equals(kth_compact_block_const_t self, kth_compact_block_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}


// Serialization

uint8_t* kth_chain_compact_block_to_data(kth_compact_block_const_t self, uint32_t version, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const data = kth::cpp_ref<cpp_t>(self).to_data(version);
    return kth::create_c_array(data, *out_size);
}

kth_size_t kth_chain_compact_block_serialized_size(kth_compact_block_const_t self, uint32_t version) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).serialized_size(version);
}


// Getters

kth_header_const_t kth_chain_compact_block_header(kth_compact_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).header());
}

uint64_t kth_chain_compact_block_nonce(kth_compact_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).nonce();
}

kth_u64_list_mut_t kth_chain_compact_block_short_ids(kth_compact_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const cpp_result = kth::cpp_ref<cpp_t>(self).short_ids();
    return kth::leak_list<uint64_t>(cpp_result.begin(), cpp_result.end());
}

kth_prefilled_transaction_list_const_t kth_chain_compact_block_transactions(kth_compact_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).transactions());
}


// Setters

void kth_chain_compact_block_set_header(kth_compact_block_mut_t self, kth_header_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth::cpp_ref<kth::domain::chain::header>(value);
    kth::cpp_ref<cpp_t>(self).set_header(value_cpp);
}

void kth_chain_compact_block_set_nonce(kth_compact_block_mut_t self, uint64_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).set_nonce(value);
}

void kth_chain_compact_block_set_short_ids(kth_compact_block_mut_t self, kth_u64_list_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth::cpp_ref<std::vector<uint64_t>>(value);
    kth::cpp_ref<cpp_t>(self).set_short_ids(value_cpp);
}

void kth_chain_compact_block_set_transactions(kth_compact_block_mut_t self, kth_prefilled_transaction_list_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth::cpp_ref<kth::domain::message::prefilled_transaction::list>(value);
    kth::cpp_ref<cpp_t>(self).set_transactions(value_cpp);
}


// Predicates

kth_bool_t kth_chain_compact_block_is_valid(kth_compact_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_valid());
}


// Operations

void kth_chain_compact_block_reset(kth_compact_block_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).reset();
}

} // extern "C"
