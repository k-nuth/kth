// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/chain/token_data.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/chain/token_data.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::chain::token_data_t;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_error_code_t kth_chain_token_construct_from_data(uint8_t const* data, kth_size_t n, KTH_OUT_OWNED kth_token_data_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, kth::sz(n)));
    auto result = kth::domain::chain::token::encoding::from_data(data_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}


// Static factories

kth_token_data_mut_t kth_chain_token_make_fungible(kth_hash_t const* id, uint64_t amount) {
    KTH_PRECONDITION(id != nullptr);
    auto const id_cpp = kth::hash_to_cpp(id->hash);
    return kth::leak_if_valid(kth::domain::chain::make_fungible(id_cpp, amount));
}

kth_token_data_mut_t kth_chain_token_make_fungible_unsafe(uint8_t const* id, uint64_t amount) {
    KTH_PRECONDITION(id != nullptr);
    auto const id_cpp = kth::hash_to_cpp(id);
    return kth::leak_if_valid(kth::domain::chain::make_fungible(id_cpp, amount));
}

kth_token_data_mut_t kth_chain_token_make_non_fungible(kth_hash_t const* id, kth_token_capability_t capability, uint8_t const* commitment, kth_size_t n) {
    KTH_PRECONDITION(id != nullptr);
    KTH_PRECONDITION(commitment != nullptr || n == 0);
    auto const id_cpp = kth::hash_to_cpp(id->hash);
    auto const capability_cpp = kth::token_capability_to_cpp(capability);
    auto const commitment_cpp = n != 0 ? kth::data_chunk(commitment, commitment + n) : kth::data_chunk{};
    return kth::leak_if_valid(kth::domain::chain::make_non_fungible(id_cpp, capability_cpp, commitment_cpp));
}

kth_token_data_mut_t kth_chain_token_make_non_fungible_unsafe(uint8_t const* id, kth_token_capability_t capability, uint8_t const* commitment, kth_size_t n) {
    KTH_PRECONDITION(id != nullptr);
    KTH_PRECONDITION(commitment != nullptr || n == 0);
    auto const id_cpp = kth::hash_to_cpp(id);
    auto const capability_cpp = kth::token_capability_to_cpp(capability);
    auto const commitment_cpp = n != 0 ? kth::data_chunk(commitment, commitment + n) : kth::data_chunk{};
    return kth::leak_if_valid(kth::domain::chain::make_non_fungible(id_cpp, capability_cpp, commitment_cpp));
}

kth_token_data_mut_t kth_chain_token_make_both(kth_hash_t const* id, uint64_t amount, kth_token_capability_t capability, uint8_t const* commitment, kth_size_t n) {
    KTH_PRECONDITION(id != nullptr);
    KTH_PRECONDITION(commitment != nullptr || n == 0);
    auto const id_cpp = kth::hash_to_cpp(id->hash);
    auto const capability_cpp = kth::token_capability_to_cpp(capability);
    auto const commitment_cpp = n != 0 ? kth::data_chunk(commitment, commitment + n) : kth::data_chunk{};
    return kth::leak_if_valid(kth::domain::chain::make_both(id_cpp, amount, capability_cpp, commitment_cpp));
}

kth_token_data_mut_t kth_chain_token_make_both_unsafe(uint8_t const* id, uint64_t amount, kth_token_capability_t capability, uint8_t const* commitment, kth_size_t n) {
    KTH_PRECONDITION(id != nullptr);
    KTH_PRECONDITION(commitment != nullptr || n == 0);
    auto const id_cpp = kth::hash_to_cpp(id);
    auto const capability_cpp = kth::token_capability_to_cpp(capability);
    auto const commitment_cpp = n != 0 ? kth::data_chunk(commitment, commitment + n) : kth::data_chunk{};
    return kth::leak_if_valid(kth::domain::chain::make_both(id_cpp, amount, capability_cpp, commitment_cpp));
}


// Destructor

void kth_chain_token_data_destruct(kth_token_data_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_token_data_mut_t kth_chain_token_data_copy(kth_token_data_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_chain_token_data_equals(kth_token_data_const_t self, kth_token_data_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}


// Serialization

kth_size_t kth_chain_token_data_serialized_size(kth_token_data_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::domain::chain::token::encoding::serialized_size(kth::cpp_ref<cpp_t>(self));
}

uint8_t* kth_chain_token_data_to_data(kth_token_data_const_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const data = kth::domain::chain::token::encoding::to_data(kth::cpp_ref<cpp_t>(self));
    return kth::create_c_array(data, *out_size);
}


// Getters

kth_hash_t kth_chain_token_data_id(kth_token_data_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<cpp_t>(self).id);
}

kth_token_kind_t kth_chain_token_data_get_kind(kth_token_data_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return static_cast<kth_token_kind_t>(kth::domain::chain::get_kind(kth::cpp_ref<cpp_t>(self)));
}

int64_t kth_chain_token_data_get_amount(kth_token_data_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::domain::chain::get_amount(kth::cpp_ref<cpp_t>(self));
}

kth_token_capability_t kth_chain_token_data_get_nft_capability(kth_token_data_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::token_capability_to_c(kth::domain::chain::get_nft_capability(kth::cpp_ref<cpp_t>(self)));
}

uint8_t* kth_chain_token_data_get_nft_commitment(kth_token_data_const_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const data = kth::domain::chain::get_nft_commitment(kth::cpp_ref<cpp_t>(self));
    return kth::create_c_array(data, *out_size);
}

uint8_t kth_chain_token_data_bitfield(kth_token_data_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::domain::chain::token::encoding::bitfield(kth::cpp_ref<cpp_t>(self));
}


// Setters

void kth_chain_token_data_set_id(kth_token_data_mut_t self, kth_hash_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value->hash);
    kth::cpp_ref<cpp_t>(self).id = value_cpp;
}

void kth_chain_token_data_set_id_unsafe(kth_token_data_mut_t self, uint8_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value);
    kth::cpp_ref<cpp_t>(self).id = value_cpp;
}


// Predicates

kth_bool_t kth_chain_token_data_is_valid(kth_token_data_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::domain::chain::is_valid(kth::cpp_ref<cpp_t>(self)));
}

kth_bool_t kth_chain_token_data_has_nft(kth_token_data_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::domain::chain::has_nft(kth::cpp_ref<cpp_t>(self)));
}

kth_bool_t kth_chain_token_data_is_fungible_only(kth_token_data_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::domain::chain::is_fungible_only(kth::cpp_ref<cpp_t>(self)));
}

kth_bool_t kth_chain_token_data_is_immutable_nft(kth_token_data_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::domain::chain::is_immutable_nft(kth::cpp_ref<cpp_t>(self)));
}

kth_bool_t kth_chain_token_data_is_mutable_nft(kth_token_data_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::domain::chain::is_mutable_nft(kth::cpp_ref<cpp_t>(self)));
}

kth_bool_t kth_chain_token_data_is_minting_nft(kth_token_data_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::domain::chain::is_minting_nft(kth::cpp_ref<cpp_t>(self)));
}

} // extern "C"
