// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/chain/merkle_block.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/message/merkle_block.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::message::merkle_block;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_error_code_t kth_chain_merkle_block_construct_from_data(uint8_t const* data, kth_size_t n, uint32_t version, KTH_OUT_OWNED kth_merkle_block_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, kth::sz(n)));
    auto result = cpp_t::from_data(data_cpp, version);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_merkle_block_mut_t kth_chain_merkle_block_construct(kth_block_const_t block) {
    KTH_PRECONDITION(block != nullptr);
    auto const& block_cpp = kth::cpp_ref<kth::domain::chain::block>(block);
    return kth::leak<cpp_t>(block_cpp);
}

kth_error_code_t kth_chain_merkle_block_create(kth_header_const_t header, kth_size_t total_transactions, kth_hash_list_const_t hashes, uint8_t const* flags, kth_size_t n, KTH_OUT_OWNED kth_merkle_block_mut_t* out) {
    KTH_PRECONDITION(header != nullptr);
    KTH_PRECONDITION(hashes != nullptr);
    KTH_PRECONDITION(flags != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const& header_cpp = kth::cpp_ref<kth::domain::chain::header>(header);
    auto const total_transactions_cpp = kth::sz(total_transactions);
    auto const& hashes_cpp = kth::cpp_ref<kth::hash_list>(hashes);
    auto const flags_cpp = n != 0 ? kth::data_chunk(flags, flags + n) : kth::data_chunk{};
    auto result = cpp_t::create(header_cpp, total_transactions_cpp, hashes_cpp, flags_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}


// Destructor

void kth_chain_merkle_block_destruct(kth_merkle_block_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_merkle_block_mut_t kth_chain_merkle_block_copy(kth_merkle_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_chain_merkle_block_equals(kth_merkle_block_const_t self, kth_merkle_block_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}

kth_bool_t kth_chain_merkle_block_not_equal(kth_merkle_block_const_t self, kth_merkle_block_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::ne<cpp_t>(self, other);
}


// Serialization

uint8_t* kth_chain_merkle_block_to_data(kth_merkle_block_const_t self, uint32_t version, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    return kth::to_c_array_from(kth::cpp_ref<cpp_t>(self), *out_size, version);
}

kth_size_t kth_chain_merkle_block_serialized_size(kth_merkle_block_const_t self, uint32_t version) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).serialized_size(version);
}


// Getters

kth_header_const_t kth_chain_merkle_block_header(kth_merkle_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).header());
}

kth_size_t kth_chain_merkle_block_total_transactions(kth_merkle_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).total_transactions();
}

kth_hash_list_const_t kth_chain_merkle_block_hashes(kth_merkle_block_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).hashes());
}

uint8_t* kth_chain_merkle_block_flags(kth_merkle_block_const_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const& data = kth::cpp_ref<cpp_t>(self).flags();
    return kth::create_c_array(data, *out_size);
}

} // extern "C"
