// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is auto-generated. Do not edit manually.

// Wallet value types and enums (kth/domain/wallet).

#ifndef KTH_CAPI_CONVERSIONS_WALLET_HPP_
#define KTH_CAPI_CONVERSIONS_WALLET_HPP_

#include <cstdint>
#include <tuple>

#include <kth/capi/detail/enum_cast.hpp>
#include <kth/capi/detail/value_conversions.hpp>

#include <kth/capi/handles.h>
#include <kth/capi/wallet/coin_selection_algorithm.h>
#include <kth/capi/wallet/coin_selection_strategy.h>
#include <kth/capi/wallet/primitives.h>

#include <kth/domain/wallet/coin_selection.hpp>
#include <kth/domain/wallet/ec_private.hpp>
#include <kth/domain/wallet/encrypted_keys.hpp>
#include <kth/domain/wallet/hd_public.hpp>
#include <kth/domain/wallet/message.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/domain/wallet/wallet_manager.hpp>

namespace kth {

// payment (25 bytes)
inline kth_payment_t to_payment_t(kth::domain::wallet::payment const& x) {
    return to_c_value_array<kth_payment_t, &kth_payment_t::hash>(x);
}
inline kth::domain::wallet::payment payment_to_cpp(uint8_t const* x) {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::payment>>(x);
}

// message_signature (65 bytes), BIP137 magic byte + ECDSA r,s
inline kth_message_signature_t to_message_signature_t(kth::domain::wallet::message_signature const& x) {
    return to_c_value_array<kth_message_signature_t, &kth_message_signature_t::data>(x);
}
inline kth::domain::wallet::message_signature message_signature_to_cpp(uint8_t const* x) {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::message_signature>>(x);
}

// wif_compressed (38 bytes)
inline kth_wif_compressed_t to_wif_compressed_t(kth::domain::wallet::wif_compressed const& x) {
    return to_c_value_array<kth_wif_compressed_t, &kth_wif_compressed_t::data>(x);
}
inline kth::domain::wallet::wif_compressed wif_compressed_to_cpp(uint8_t const* x) {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::wif_compressed>>(x);
}

// wif_uncompressed (37 bytes)
inline kth_wif_uncompressed_t to_wif_uncompressed_t(kth::domain::wallet::wif_uncompressed const& x) {
    return to_c_value_array<kth_wif_uncompressed_t, &kth_wif_uncompressed_t::data>(x);
}
inline kth::domain::wallet::wif_uncompressed wif_uncompressed_to_cpp(uint8_t const* x) {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::wif_uncompressed>>(x);
}

// hd_key (82 bytes)
inline kth_hd_key_t to_hd_key_t(kth::domain::wallet::hd_key const& x) {
    return to_c_value_array<kth_hd_key_t, &kth_hd_key_t::data>(x);
}
inline kth::domain::wallet::hd_key hd_key_to_cpp(uint8_t const* x) {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::hd_key>>(x);
}

// encrypted_seed (96 bytes)
inline kth_encrypted_seed_t to_encrypted_seed_t(kth::domain::wallet::encrypted_seed_t const& x) {
    return to_c_value_array<kth_encrypted_seed_t, &kth_encrypted_seed_t::hash>(x);
}
inline kth::domain::wallet::encrypted_seed_t encrypted_seed_to_cpp(uint8_t const* x) {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::encrypted_seed_t>>(x);
}

// encrypted_private (43 bytes), BIP38
inline kth_encrypted_private_t to_encrypted_private_t(kth::domain::wallet::encrypted_private const& x) {
    return to_c_value_array<kth_encrypted_private_t, &kth_encrypted_private_t::data>(x);
}
inline kth::domain::wallet::encrypted_private encrypted_private_to_cpp(uint8_t const* x) {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::encrypted_private>>(x);
}

// encrypted_public (55 bytes), BIP38
inline kth_encrypted_public_t to_encrypted_public_t(kth::domain::wallet::encrypted_public const& x) {
    return to_c_value_array<kth_encrypted_public_t, &kth_encrypted_public_t::data>(x);
}
inline kth::domain::wallet::encrypted_public encrypted_public_to_cpp(uint8_t const* x) {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::encrypted_public>>(x);
}

// encrypted_token (53 bytes), BIP38
inline kth_encrypted_token_t to_encrypted_token_t(kth::domain::wallet::encrypted_token const& x) {
    return to_c_value_array<kth_encrypted_token_t, &kth_encrypted_token_t::data>(x);
}
inline kth::domain::wallet::encrypted_token encrypted_token_to_cpp(uint8_t const* x) {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::encrypted_token>>(x);
}

// ek_seed (24 bytes), BIP38 caller-provided key-pair randomness
inline kth_ek_seed_t to_ek_seed_t(kth::domain::wallet::ek_seed const& x) {
    return to_c_value_array<kth_ek_seed_t, &kth_ek_seed_t::data>(x);
}
inline kth::domain::wallet::ek_seed ek_seed_to_cpp(uint8_t const* x) {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::ek_seed>>(x);
}

// ek_salt (4 bytes), BIP38 lot+sequence-mode salt input
inline kth_ek_salt_t to_ek_salt_t(kth::domain::wallet::ek_salt const& x) {
    return to_c_value_array<kth_ek_salt_t, &kth_ek_salt_t::data>(x);
}
inline kth::domain::wallet::ek_salt ek_salt_to_cpp(uint8_t const* x) {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::ek_salt>>(x);
}

// ek_entropy (8 bytes), BIP38 passphrase-mode token randomness
inline kth_ek_entropy_t to_ek_entropy_t(kth::domain::wallet::ek_entropy const& x) {
    return to_c_value_array<kth_ek_entropy_t, &kth_ek_entropy_t::data>(x);
}
inline kth::domain::wallet::ek_entropy ek_entropy_to_cpp(uint8_t const* x) {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::ek_entropy>>(x);
}

// Out-of-range C values map to `smallest_first`.
inline constexpr kth::domain::wallet::coin_selection_algorithm coin_selection_algorithm_to_cpp(kth_coin_selection_algorithm_t x) {
    switch (x) {
        case kth_coin_selection_algorithm_largest_first:
            return kth::domain::wallet::coin_selection_algorithm::largest_first;
        case kth_coin_selection_algorithm_manual:
            return kth::domain::wallet::coin_selection_algorithm::manual;
        case kth_coin_selection_algorithm_send_all:
            return kth::domain::wallet::coin_selection_algorithm::send_all;
        default:
        case kth_coin_selection_algorithm_smallest_first:
            return kth::domain::wallet::coin_selection_algorithm::smallest_first;
    }
}

inline constexpr kth_coin_selection_algorithm_t coin_selection_algorithm_to_c(kth::domain::wallet::coin_selection_algorithm x) {
    return ecast<kth_coin_selection_algorithm_t>(x);
}

// Out-of-range C values map to `clean`.
inline constexpr kth::domain::wallet::coin_selection_strategy coin_selection_strategy_to_cpp(kth_coin_selection_strategy_t x) {
    switch (x) {
        case kth_coin_selection_strategy_mixed:
            return kth::domain::wallet::coin_selection_strategy::mixed;
        default:
        case kth_coin_selection_strategy_clean:
            return kth::domain::wallet::coin_selection_strategy::clean;
    }
}

inline constexpr kth_coin_selection_strategy_t coin_selection_strategy_to_c(kth::domain::wallet::coin_selection_strategy x) {
    return ecast<kth_coin_selection_strategy_t>(x);
}

} // namespace kth

#endif /* KTH_CAPI_CONVERSIONS_WALLET_HPP_ */
