// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstring>

#include <kth/capi/wallet/message.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/message.hpp>

// ---------------------------------------------------------------------------
extern "C" {

// Static utilities

kth_hash_t kth_wallet_message_hash_message(uint8_t const* message, kth_size_t n) {
    KTH_PRECONDITION(message != nullptr || n == 0);
    auto const message_cpp = kth::byte_span(message, kth::sz(n));
    return kth::to_hash_t(kth::domain::wallet::hash_message(message_cpp));
}

kth_bool_t kth_wallet_message_sign_message_ec_private(kth_message_signature_t* out_signature, uint8_t const* message, kth_size_t n, kth_ec_private_const_t secret) {
    KTH_PRECONDITION(out_signature != nullptr);
    KTH_PRECONDITION(message != nullptr || n == 0);
    KTH_PRECONDITION(secret != nullptr);
    std::array<uint8_t, 65> out_signature_cpp;
    auto const message_cpp = kth::byte_span(message, kth::sz(n));
    auto const& secret_cpp = kth::cpp_ref<kth::domain::wallet::ec_private>(secret);
    auto const cpp_result = kth::domain::wallet::sign_message(out_signature_cpp, message_cpp, secret_cpp);
    if (cpp_result) {
        std::memcpy(out_signature->data, out_signature_cpp.data(), out_signature_cpp.size());
    }
    return kth::bool_to_int(cpp_result);
}

kth_bool_t kth_wallet_message_sign_message_string(kth_message_signature_t* out_signature, uint8_t const* message, kth_size_t n, char const* wif) {
    KTH_PRECONDITION(out_signature != nullptr);
    KTH_PRECONDITION(message != nullptr || n == 0);
    KTH_PRECONDITION(wif != nullptr);
    std::array<uint8_t, 65> out_signature_cpp;
    auto const message_cpp = kth::byte_span(message, kth::sz(n));
    auto const wif_cpp = std::string(wif);
    auto const cpp_result = kth::domain::wallet::sign_message(out_signature_cpp, message_cpp, wif_cpp);
    if (cpp_result) {
        std::memcpy(out_signature->data, out_signature_cpp.data(), out_signature_cpp.size());
    }
    return kth::bool_to_int(cpp_result);
}

kth_bool_t kth_wallet_message_sign_message_hash(kth_message_signature_t* out_signature, uint8_t const* message, kth_size_t n, kth_hash_t const* secret, kth_bool_t compressed) {
    KTH_PRECONDITION(out_signature != nullptr);
    KTH_PRECONDITION(message != nullptr || n == 0);
    KTH_PRECONDITION(secret != nullptr);
    std::array<uint8_t, 65> out_signature_cpp;
    auto const message_cpp = kth::byte_span(message, kth::sz(n));
    auto secret_cpp = kth::hash_to_cpp(secret->hash);
    kth::secure_scrub secret_cpp_scrub{&secret_cpp, sizeof(secret_cpp)};
    auto const compressed_cpp = kth::int_to_bool(compressed);
    auto const cpp_result = kth::domain::wallet::sign_message(out_signature_cpp, message_cpp, secret_cpp, compressed_cpp);
    if (cpp_result) {
        std::memcpy(out_signature->data, out_signature_cpp.data(), out_signature_cpp.size());
    }
    return kth::bool_to_int(cpp_result);
}

kth_bool_t kth_wallet_message_sign_message_hash_unsafe(kth_message_signature_t* out_signature, uint8_t const* message, kth_size_t n, uint8_t const* secret, kth_bool_t compressed) {
    KTH_PRECONDITION(out_signature != nullptr);
    KTH_PRECONDITION(message != nullptr || n == 0);
    KTH_PRECONDITION(secret != nullptr);
    std::array<uint8_t, 65> out_signature_cpp;
    auto const message_cpp = kth::byte_span(message, kth::sz(n));
    auto secret_cpp = kth::hash_to_cpp(secret);
    kth::secure_scrub secret_cpp_scrub{&secret_cpp, sizeof(secret_cpp)};
    auto const compressed_cpp = kth::int_to_bool(compressed);
    auto const cpp_result = kth::domain::wallet::sign_message(out_signature_cpp, message_cpp, secret_cpp, compressed_cpp);
    if (cpp_result) {
        std::memcpy(out_signature->data, out_signature_cpp.data(), out_signature_cpp.size());
    }
    return kth::bool_to_int(cpp_result);
}

kth_bool_t kth_wallet_message_verify_message(uint8_t const* message, kth_size_t n, kth_payment_address_const_t address, kth_message_signature_t const* signature) {
    KTH_PRECONDITION(message != nullptr || n == 0);
    KTH_PRECONDITION(address != nullptr);
    KTH_PRECONDITION(signature != nullptr);
    auto const message_cpp = kth::byte_span(message, kth::sz(n));
    auto const& address_cpp = kth::cpp_ref<kth::domain::wallet::payment_address>(address);
    auto const signature_cpp = kth::message_signature_to_cpp(signature->data);
    return kth::bool_to_int(kth::domain::wallet::verify_message(message_cpp, address_cpp, signature_cpp));
}

kth_bool_t kth_wallet_message_verify_message_unsafe(uint8_t const* message, kth_size_t n, kth_payment_address_const_t address, uint8_t const* signature) {
    KTH_PRECONDITION(message != nullptr || n == 0);
    KTH_PRECONDITION(address != nullptr);
    KTH_PRECONDITION(signature != nullptr);
    auto const message_cpp = kth::byte_span(message, kth::sz(n));
    auto const& address_cpp = kth::cpp_ref<kth::domain::wallet::payment_address>(address);
    auto const signature_cpp = kth::message_signature_to_cpp(signature);
    return kth::bool_to_int(kth::domain::wallet::verify_message(message_cpp, address_cpp, signature_cpp));
}

kth_bool_t kth_wallet_message_recovery_id_to_magic(uint8_t* out_magic, uint8_t recovery_id, kth_bool_t compressed) {
    KTH_PRECONDITION(out_magic != nullptr);
    uint8_t out_magic_cpp = 0;
    auto const compressed_cpp = kth::int_to_bool(compressed);
    auto const cpp_result = kth::domain::wallet::recovery_id_to_magic(out_magic_cpp, recovery_id, compressed_cpp);
    if (cpp_result) {
        *out_magic = static_cast<uint8_t>(out_magic_cpp);
    }
    return kth::bool_to_int(cpp_result);
}

kth_bool_t kth_wallet_message_magic_to_recovery_id(uint8_t* out_recovery_id, kth_bool_t* out_compressed, uint8_t magic) {
    KTH_PRECONDITION(out_recovery_id != nullptr);
    KTH_PRECONDITION(out_compressed != nullptr);
    uint8_t out_recovery_id_cpp = 0;
    bool out_compressed_cpp = false;
    auto const cpp_result = kth::domain::wallet::magic_to_recovery_id(out_recovery_id_cpp, out_compressed_cpp, magic);
    if (cpp_result) {
        *out_recovery_id = static_cast<uint8_t>(out_recovery_id_cpp);
        *out_compressed = kth::bool_to_int(out_compressed_cpp);
    }
    return kth::bool_to_int(cpp_result);
}

} // extern "C"
