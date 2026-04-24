// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstring>

#include <kth/capi/wallet/encrypted_keys.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/encrypted_keys.hpp>

// ---------------------------------------------------------------------------
extern "C" {

// Static utilities

kth_bool_t kth_wallet_encrypted_keys_create_key_pair(kth_encrypted_private_t* out_private, kth_ec_compressed_t* out_point, kth_encrypted_token_t const* token, kth_ek_seed_t const* seed, uint8_t version, kth_bool_t compressed) {
    KTH_PRECONDITION(out_private != nullptr);
    KTH_PRECONDITION(out_point != nullptr);
    KTH_PRECONDITION(token != nullptr);
    KTH_PRECONDITION(seed != nullptr);
    kth::domain::wallet::encrypted_private out_private_cpp;
    kth::ec_compressed out_point_cpp;
    auto const token_cpp = kth::encrypted_token_to_cpp(token->data);
    auto const seed_cpp = kth::ek_seed_to_cpp(seed->data);
    auto const compressed_cpp = kth::int_to_bool(compressed);
    auto const cpp_result = kth::domain::wallet::create_key_pair(out_private_cpp, out_point_cpp, token_cpp, seed_cpp, version, compressed_cpp);
    if (cpp_result) {
        std::memcpy(out_private->data, out_private_cpp.data(), out_private_cpp.size());
        std::memcpy(out_point->data, out_point_cpp.data(), out_point_cpp.size());
    }
    return kth::bool_to_int(cpp_result);
}

kth_bool_t kth_wallet_encrypted_keys_create_key_pair_unsafe(kth_encrypted_private_t* out_private, kth_ec_compressed_t* out_point, uint8_t const* token, uint8_t const* seed, uint8_t version, kth_bool_t compressed) {
    KTH_PRECONDITION(out_private != nullptr);
    KTH_PRECONDITION(out_point != nullptr);
    KTH_PRECONDITION(token != nullptr);
    KTH_PRECONDITION(seed != nullptr);
    kth::domain::wallet::encrypted_private out_private_cpp;
    kth::ec_compressed out_point_cpp;
    auto const token_cpp = kth::encrypted_token_to_cpp(token);
    auto const seed_cpp = kth::ek_seed_to_cpp(seed);
    auto const compressed_cpp = kth::int_to_bool(compressed);
    auto const cpp_result = kth::domain::wallet::create_key_pair(out_private_cpp, out_point_cpp, token_cpp, seed_cpp, version, compressed_cpp);
    if (cpp_result) {
        std::memcpy(out_private->data, out_private_cpp.data(), out_private_cpp.size());
        std::memcpy(out_point->data, out_point_cpp.data(), out_point_cpp.size());
    }
    return kth::bool_to_int(cpp_result);
}

} // extern "C"
