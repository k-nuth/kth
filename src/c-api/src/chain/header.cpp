// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <kth/capi/chain/header.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>

// Conversion functions
kth::domain::chain::header& kth_chain_header_cpp(kth_header_mut_t o) {
    return *static_cast<kth::domain::chain::header*>(o);
}
kth::domain::chain::header const& kth_chain_header_const_cpp(kth_header_const_t o) {
    return *static_cast<kth::domain::chain::header const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

kth_header_mut_t kth_chain_header_construct_default() {
    return new kth::domain::chain::header();
}

kth_header_mut_t kth_chain_header_construct(uint32_t version, uint8_t const* previous_block_hash, uint8_t const* merkle, uint32_t timestamp, uint32_t bits, uint32_t nonce) {
    auto previous_block_hash_cpp = kth::hash_to_cpp(previous_block_hash);
    auto merkle_cpp = kth::hash_to_cpp(merkle);
    return new kth::domain::chain::header(version, previous_block_hash_cpp, merkle_cpp, timestamp, bits, nonce);
}

void kth_chain_header_destruct(kth_header_mut_t header) {
    if (header == nullptr) return;
    delete &kth_chain_header_cpp(header);
}

kth_header_mut_t kth_chain_header_copy(kth_header_const_t other) {
    return new kth::domain::chain::header(kth_chain_header_const_cpp(other));
}

kth_bool_t kth_chain_header_is_valid(kth_header_const_t header) {
    return kth::bool_to_int(kth_chain_header_const_cpp(header).is_valid());
}

char const* kth_chain_header_proof_str(kth_header_const_t header) {
    auto proof_str = kth_chain_header_const_cpp(header).proof().str();
    return kth::create_c_str(proof_str);
}

uint32_t kth_chain_header_version(kth_header_const_t header) {
    return kth_chain_header_const_cpp(header).version();
}

kth_hash_t kth_chain_header_previous_block_hash(kth_header_const_t header) {
    auto hash_cpp = kth_chain_header_const_cpp(header).previous_block_hash();
    return kth::to_hash_t(hash_cpp);
}

void kth_chain_header_previous_block_hash_out(kth_header_const_t header, kth_hash_t* out_previous_block_hash) {
    auto hash_cpp = kth_chain_header_const_cpp(header).previous_block_hash();
    kth::copy_c_hash(hash_cpp, out_previous_block_hash);
}

kth_hash_t kth_chain_header_merkle(kth_header_const_t header) {
    auto hash_cpp = kth_chain_header_const_cpp(header).merkle();
    return kth::to_hash_t(hash_cpp);
}

void kth_chain_header_merkle_out(kth_header_const_t header, kth_hash_t* out_merkle) {
    auto hash_cpp = kth_chain_header_const_cpp(header).merkle();
    kth::copy_c_hash(hash_cpp, out_merkle);
}

uint32_t kth_chain_header_timestamp(kth_header_const_t header) {
    return kth_chain_header_const_cpp(header).timestamp();
}

uint32_t kth_chain_header_bits(kth_header_const_t header) {
    return kth_chain_header_const_cpp(header).bits();
}

uint32_t kth_chain_header_nonce(kth_header_const_t header) {
    return kth_chain_header_const_cpp(header).nonce();
}

kth_bool_t kth_chain_header_is_valid_timestamp(kth_header_const_t header) {
    return kth::bool_to_int(kth_chain_header_const_cpp(header).is_valid_timestamp());
}

kth_error_code_t kth_chain_header_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, kth_header_mut_t* out_result) {
    KTH_PRECONDITION(data != nullptr);
    KTH_PRECONDITION(out_result != nullptr);
    kth::byte_reader reader({data, n});
    auto res = kth::domain::chain::header::from_data(reader, kth::int_to_bool(wire));
    if ( ! res) {
        *out_result = nullptr;
        return kth::to_c_err(res.error());
    }
    *out_result = kth::move_or_copy_and_leak(std::move(*res));
    return kth_ec_success;
}

uint8_t const* kth_chain_header_to_data(kth_header_const_t header, kth_bool_t wire, kth_size_t* out_size) {
    KTH_PRECONDITION(out_size != nullptr);
    auto data = kth_chain_header_const_cpp(header).to_data(kth::int_to_bool(wire));
    *out_size = data.size();
    return kth::create_c_array(data);
}

kth_size_t kth_chain_header_serialized_size(kth_header_const_t header, kth_bool_t wire) {
    return kth_chain_header_const_cpp(header).serialized_size(kth::int_to_bool(wire));
}

void kth_chain_header_set_version(kth_header_mut_t header, uint32_t value) {
    kth_chain_header_cpp(header).set_version(value);
}

void kth_chain_header_set_previous_block_hash(kth_header_mut_t header, uint8_t const* hash) {
    kth_chain_header_cpp(header).set_previous_block_hash(kth::hash_to_cpp(hash));
}

void kth_chain_header_set_merkle(kth_header_mut_t header, uint8_t const* hash) {
    kth_chain_header_cpp(header).set_merkle(kth::hash_to_cpp(hash));
}

void kth_chain_header_set_timestamp(kth_header_mut_t header, uint32_t value) {
    kth_chain_header_cpp(header).set_timestamp(value);
}

void kth_chain_header_set_bits(kth_header_mut_t header, uint32_t value) {
    kth_chain_header_cpp(header).set_bits(value);
}

void kth_chain_header_set_nonce(kth_header_mut_t header, uint32_t value) {
    kth_chain_header_cpp(header).set_nonce(value);
}

kth_hash_t kth_chain_header_hash_pow(kth_header_const_t header) {
    auto hash_cpp = kth_chain_header_const_cpp(header).hash_pow();
    return kth::to_hash_t(hash_cpp);
}

#if defined(KTH_CURRENCY_LTC)
kth_hash_t kth_chain_header_litecoin_proof_of_work_hash(kth_header_const_t header) {
    auto hash_cpp = kth_chain_header_const_cpp(header).litecoin_proof_of_work_hash();
    return kth::to_hash_t(hash_cpp);
}
#endif

kth_bool_t kth_chain_header_is_valid_proof_of_work(kth_header_const_t header, kth_bool_t retarget) {
    return kth::bool_to_int(kth_chain_header_const_cpp(header).is_valid_proof_of_work(kth::int_to_bool(retarget)));
}

kth_error_code_t kth_chain_header_check(kth_header_const_t header, kth_bool_t retarget) {
    return kth::to_c_err(kth_chain_header_const_cpp(header).check(kth::int_to_bool(retarget)));
}

kth_error_code_t kth_chain_header_accept(kth_header_const_t header, kth_chain_state_const_t state) {
    return kth::to_c_err(kth_chain_header_const_cpp(header).accept(kth_chain_chain_state_const_cpp(state)));
}

void kth_chain_header_reset(kth_header_mut_t header) {
    kth_chain_header_cpp(header).reset();
}

kth_hash_t kth_chain_header_hash(kth_header_const_t header) {
    auto hash_cpp = kth_chain_header_const_cpp(header).hash();
    return kth::to_hash_t(hash_cpp);
}

void kth_chain_header_hash_out(kth_header_const_t header, kth_hash_t* out_hash) {
    auto hash_cpp = kth_chain_header_const_cpp(header).hash();
    kth::copy_c_hash(hash_cpp, out_hash);
}

} // extern "C"
