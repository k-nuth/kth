// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is auto-generated. Do not edit manually.

// Wallet value types and enums (kth/domain/wallet).

#ifndef KTH_CAPI_CONVERSIONS_WALLET_HPP_
#define KTH_CAPI_CONVERSIONS_WALLET_HPP_

#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

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
inline kth_payment_t to_payment_t(kth::domain::wallet::payment const& x)
    noexcept(noexcept(to_c_value_array<kth_payment_t, &kth_payment_t::hash>(x))) {
    return to_c_value_array<kth_payment_t, &kth_payment_t::hash>(x);
}
inline kth::domain::wallet::payment payment_to_cpp(uint8_t const* x) noexcept {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::payment>>(x);
}
static_assert(noexcept(to_c_value_array<kth_payment_t, &kth_payment_t::hash>(std::declval<kth::domain::wallet::payment const&>())));
static_assert(noexcept(to_array_cpp<std::tuple_size_v<kth::domain::wallet::payment>>(std::declval<uint8_t const*>())));

// message_signature (65 bytes), BIP137 magic byte + ECDSA r,s
inline kth_message_signature_t to_message_signature_t(kth::domain::wallet::message_signature const& x)
    noexcept(noexcept(to_c_value_array<kth_message_signature_t, &kth_message_signature_t::data>(x))) {
    return to_c_value_array<kth_message_signature_t, &kth_message_signature_t::data>(x);
}
inline kth::domain::wallet::message_signature message_signature_to_cpp(uint8_t const* x) noexcept {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::message_signature>>(x);
}
static_assert(noexcept(to_c_value_array<kth_message_signature_t, &kth_message_signature_t::data>(std::declval<kth::domain::wallet::message_signature const&>())));
static_assert(noexcept(to_array_cpp<std::tuple_size_v<kth::domain::wallet::message_signature>>(std::declval<uint8_t const*>())));

// wif_compressed (38 bytes)
inline kth_wif_compressed_t to_wif_compressed_t(kth::domain::wallet::wif_compressed const& x)
    noexcept(noexcept(to_c_value_array<kth_wif_compressed_t, &kth_wif_compressed_t::data>(x))) {
    return to_c_value_array<kth_wif_compressed_t, &kth_wif_compressed_t::data>(x);
}
inline kth::domain::wallet::wif_compressed wif_compressed_to_cpp(uint8_t const* x) noexcept {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::wif_compressed>>(x);
}
static_assert(noexcept(to_c_value_array<kth_wif_compressed_t, &kth_wif_compressed_t::data>(std::declval<kth::domain::wallet::wif_compressed const&>())));
static_assert(noexcept(to_array_cpp<std::tuple_size_v<kth::domain::wallet::wif_compressed>>(std::declval<uint8_t const*>())));

// wif_uncompressed (37 bytes)
inline kth_wif_uncompressed_t to_wif_uncompressed_t(kth::domain::wallet::wif_uncompressed const& x)
    noexcept(noexcept(to_c_value_array<kth_wif_uncompressed_t, &kth_wif_uncompressed_t::data>(x))) {
    return to_c_value_array<kth_wif_uncompressed_t, &kth_wif_uncompressed_t::data>(x);
}
inline kth::domain::wallet::wif_uncompressed wif_uncompressed_to_cpp(uint8_t const* x) noexcept {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::wif_uncompressed>>(x);
}
static_assert(noexcept(to_c_value_array<kth_wif_uncompressed_t, &kth_wif_uncompressed_t::data>(std::declval<kth::domain::wallet::wif_uncompressed const&>())));
static_assert(noexcept(to_array_cpp<std::tuple_size_v<kth::domain::wallet::wif_uncompressed>>(std::declval<uint8_t const*>())));

// hd_key (82 bytes)
inline kth_hd_key_t to_hd_key_t(kth::domain::wallet::hd_key const& x)
    noexcept(noexcept(to_c_value_array<kth_hd_key_t, &kth_hd_key_t::data>(x))) {
    return to_c_value_array<kth_hd_key_t, &kth_hd_key_t::data>(x);
}
inline kth::domain::wallet::hd_key hd_key_to_cpp(uint8_t const* x) noexcept {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::hd_key>>(x);
}
static_assert(noexcept(to_c_value_array<kth_hd_key_t, &kth_hd_key_t::data>(std::declval<kth::domain::wallet::hd_key const&>())));
static_assert(noexcept(to_array_cpp<std::tuple_size_v<kth::domain::wallet::hd_key>>(std::declval<uint8_t const*>())));

// encrypted_seed (96 bytes)
inline kth_encrypted_seed_t to_encrypted_seed_t(kth::domain::wallet::encrypted_seed_t const& x)
    noexcept(noexcept(to_c_value_array<kth_encrypted_seed_t, &kth_encrypted_seed_t::hash>(x))) {
    return to_c_value_array<kth_encrypted_seed_t, &kth_encrypted_seed_t::hash>(x);
}
inline kth::domain::wallet::encrypted_seed_t encrypted_seed_to_cpp(uint8_t const* x) noexcept {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::encrypted_seed_t>>(x);
}
static_assert(noexcept(to_c_value_array<kth_encrypted_seed_t, &kth_encrypted_seed_t::hash>(std::declval<kth::domain::wallet::encrypted_seed_t const&>())));
static_assert(noexcept(to_array_cpp<std::tuple_size_v<kth::domain::wallet::encrypted_seed_t>>(std::declval<uint8_t const*>())));

// encrypted_private (43 bytes), BIP38
inline kth_encrypted_private_t to_encrypted_private_t(kth::domain::wallet::encrypted_private const& x)
    noexcept(noexcept(to_c_value_array<kth_encrypted_private_t, &kth_encrypted_private_t::data>(x))) {
    return to_c_value_array<kth_encrypted_private_t, &kth_encrypted_private_t::data>(x);
}
inline kth::domain::wallet::encrypted_private encrypted_private_to_cpp(uint8_t const* x) noexcept {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::encrypted_private>>(x);
}
static_assert(noexcept(to_c_value_array<kth_encrypted_private_t, &kth_encrypted_private_t::data>(std::declval<kth::domain::wallet::encrypted_private const&>())));
static_assert(noexcept(to_array_cpp<std::tuple_size_v<kth::domain::wallet::encrypted_private>>(std::declval<uint8_t const*>())));

// encrypted_public (55 bytes), BIP38
inline kth_encrypted_public_t to_encrypted_public_t(kth::domain::wallet::encrypted_public const& x)
    noexcept(noexcept(to_c_value_array<kth_encrypted_public_t, &kth_encrypted_public_t::data>(x))) {
    return to_c_value_array<kth_encrypted_public_t, &kth_encrypted_public_t::data>(x);
}
inline kth::domain::wallet::encrypted_public encrypted_public_to_cpp(uint8_t const* x) noexcept {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::encrypted_public>>(x);
}
static_assert(noexcept(to_c_value_array<kth_encrypted_public_t, &kth_encrypted_public_t::data>(std::declval<kth::domain::wallet::encrypted_public const&>())));
static_assert(noexcept(to_array_cpp<std::tuple_size_v<kth::domain::wallet::encrypted_public>>(std::declval<uint8_t const*>())));

// encrypted_token (53 bytes), BIP38
inline kth_encrypted_token_t to_encrypted_token_t(kth::domain::wallet::encrypted_token const& x)
    noexcept(noexcept(to_c_value_array<kth_encrypted_token_t, &kth_encrypted_token_t::data>(x))) {
    return to_c_value_array<kth_encrypted_token_t, &kth_encrypted_token_t::data>(x);
}
inline kth::domain::wallet::encrypted_token encrypted_token_to_cpp(uint8_t const* x) noexcept {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::encrypted_token>>(x);
}
static_assert(noexcept(to_c_value_array<kth_encrypted_token_t, &kth_encrypted_token_t::data>(std::declval<kth::domain::wallet::encrypted_token const&>())));
static_assert(noexcept(to_array_cpp<std::tuple_size_v<kth::domain::wallet::encrypted_token>>(std::declval<uint8_t const*>())));

// ek_seed (24 bytes), BIP38 caller-provided key-pair randomness
inline kth_ek_seed_t to_ek_seed_t(kth::domain::wallet::ek_seed const& x)
    noexcept(noexcept(to_c_value_array<kth_ek_seed_t, &kth_ek_seed_t::data>(x))) {
    return to_c_value_array<kth_ek_seed_t, &kth_ek_seed_t::data>(x);
}
inline kth::domain::wallet::ek_seed ek_seed_to_cpp(uint8_t const* x) noexcept {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::ek_seed>>(x);
}
static_assert(noexcept(to_c_value_array<kth_ek_seed_t, &kth_ek_seed_t::data>(std::declval<kth::domain::wallet::ek_seed const&>())));
static_assert(noexcept(to_array_cpp<std::tuple_size_v<kth::domain::wallet::ek_seed>>(std::declval<uint8_t const*>())));

// ek_salt (4 bytes), BIP38 lot+sequence-mode salt input
inline kth_ek_salt_t to_ek_salt_t(kth::domain::wallet::ek_salt const& x)
    noexcept(noexcept(to_c_value_array<kth_ek_salt_t, &kth_ek_salt_t::data>(x))) {
    return to_c_value_array<kth_ek_salt_t, &kth_ek_salt_t::data>(x);
}
inline kth::domain::wallet::ek_salt ek_salt_to_cpp(uint8_t const* x) noexcept {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::ek_salt>>(x);
}
static_assert(noexcept(to_c_value_array<kth_ek_salt_t, &kth_ek_salt_t::data>(std::declval<kth::domain::wallet::ek_salt const&>())));
static_assert(noexcept(to_array_cpp<std::tuple_size_v<kth::domain::wallet::ek_salt>>(std::declval<uint8_t const*>())));

// ek_entropy (8 bytes), BIP38 passphrase-mode token randomness
inline kth_ek_entropy_t to_ek_entropy_t(kth::domain::wallet::ek_entropy const& x)
    noexcept(noexcept(to_c_value_array<kth_ek_entropy_t, &kth_ek_entropy_t::data>(x))) {
    return to_c_value_array<kth_ek_entropy_t, &kth_ek_entropy_t::data>(x);
}
inline kth::domain::wallet::ek_entropy ek_entropy_to_cpp(uint8_t const* x) noexcept {
    return to_array_cpp<std::tuple_size_v<kth::domain::wallet::ek_entropy>>(x);
}
static_assert(noexcept(to_c_value_array<kth_ek_entropy_t, &kth_ek_entropy_t::data>(std::declval<kth::domain::wallet::ek_entropy const&>())));
static_assert(noexcept(to_array_cpp<std::tuple_size_v<kth::domain::wallet::ek_entropy>>(std::declval<uint8_t const*>())));

// Out-of-range C values map to `smallest_first`.
inline constexpr kth::domain::wallet::coin_selection_algorithm coin_selection_algorithm_to_cpp(kth_coin_selection_algorithm_t x) noexcept {
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

inline constexpr kth_coin_selection_algorithm_t coin_selection_algorithm_to_c(kth::domain::wallet::coin_selection_algorithm x) noexcept {
    return ecast<kth_coin_selection_algorithm_t>(x);
}

// Numeric ABI: every named enumerator converts to its counterpart, so the C and
// C++ values cannot drift apart while the names keep matching.
static_assert(sizeof(std::underlying_type_t<kth::domain::wallet::coin_selection_algorithm>) <= sizeof(kth_coin_selection_algorithm_t));
static_assert(coin_selection_algorithm_to_c(kth::domain::wallet::coin_selection_algorithm::smallest_first) == kth_coin_selection_algorithm_smallest_first);
static_assert(coin_selection_algorithm_to_cpp(kth_coin_selection_algorithm_smallest_first) == kth::domain::wallet::coin_selection_algorithm::smallest_first);
static_assert(coin_selection_algorithm_to_c(kth::domain::wallet::coin_selection_algorithm::largest_first) == kth_coin_selection_algorithm_largest_first);
static_assert(coin_selection_algorithm_to_cpp(kth_coin_selection_algorithm_largest_first) == kth::domain::wallet::coin_selection_algorithm::largest_first);
static_assert(coin_selection_algorithm_to_c(kth::domain::wallet::coin_selection_algorithm::manual) == kth_coin_selection_algorithm_manual);
static_assert(coin_selection_algorithm_to_cpp(kth_coin_selection_algorithm_manual) == kth::domain::wallet::coin_selection_algorithm::manual);
static_assert(coin_selection_algorithm_to_c(kth::domain::wallet::coin_selection_algorithm::send_all) == kth_coin_selection_algorithm_send_all);
static_assert(coin_selection_algorithm_to_cpp(kth_coin_selection_algorithm_send_all) == kth::domain::wallet::coin_selection_algorithm::send_all);

// Out-of-range C values map to `clean`.
inline constexpr kth::domain::wallet::coin_selection_strategy coin_selection_strategy_to_cpp(kth_coin_selection_strategy_t x) noexcept {
    switch (x) {
        case kth_coin_selection_strategy_mixed:
            return kth::domain::wallet::coin_selection_strategy::mixed;
        default:
        case kth_coin_selection_strategy_clean:
            return kth::domain::wallet::coin_selection_strategy::clean;
    }
}

inline constexpr kth_coin_selection_strategy_t coin_selection_strategy_to_c(kth::domain::wallet::coin_selection_strategy x) noexcept {
    return ecast<kth_coin_selection_strategy_t>(x);
}

// Numeric ABI: every named enumerator converts to its counterpart, so the C and
// C++ values cannot drift apart while the names keep matching.
static_assert(sizeof(std::underlying_type_t<kth::domain::wallet::coin_selection_strategy>) <= sizeof(kth_coin_selection_strategy_t));
static_assert(coin_selection_strategy_to_c(kth::domain::wallet::coin_selection_strategy::clean) == kth_coin_selection_strategy_clean);
static_assert(coin_selection_strategy_to_cpp(kth_coin_selection_strategy_clean) == kth::domain::wallet::coin_selection_strategy::clean);
static_assert(coin_selection_strategy_to_c(kth::domain::wallet::coin_selection_strategy::mixed) == kth_coin_selection_strategy_mixed);
static_assert(coin_selection_strategy_to_cpp(kth_coin_selection_strategy_mixed) == kth::domain::wallet::coin_selection_strategy::mixed);

} // namespace kth

#endif /* KTH_CAPI_CONVERSIONS_WALLET_HPP_ */
