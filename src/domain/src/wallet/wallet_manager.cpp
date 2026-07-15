// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/wallet_manager.hpp>

#include <kth/infrastructure/utility/pseudo_random.hpp>
#include <kth/domain/wallet/dictionary.hpp>
#include <kth/domain/wallet/mnemonic.hpp>

#include <expected>


#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/domain/wallet/hd_public.hpp>
#include <kth/domain/wallet/hd_private.hpp>


namespace kth::domain::wallet {

hash_digest derive_key(std::string const& password, std::array<uint8_t, default_salt_size> const& salt) {
    constexpr size_t iterations = 100000; // 100k is a good security point
    constexpr size_t key_length = AES_KEYLEN; // 32 bytes for AES-256

    std::vector<uint8_t> password_bytes(password.begin(), password.end());
    auto long_key = pkcs5_pbkdf2_hmac_sha512(password_bytes, salt, iterations);

    hash_digest key;
    static_assert(key.size() == key_length, "Derived key size does not match expected size");
    std::copy_n(long_key.begin(), key.size(), key.begin());
    return key;
}

std::expected<wallet_data, std::error_code>
create(
    std::string const& password,
    std::string const& normalized_passphrase,
    kth::domain::wallet::dictionary const& lexicon) {

    using kth::domain::wallet::create_mnemonic;
    using kth::domain::wallet::decode_mnemonic;
    using kth::domain::wallet::decode_mnemonic_normalized_passphrase;
    using kth::domain::wallet::hd_first_hardened_key;
    using kth::domain::wallet::hd_private;
    using kth::domain::wallet::hd_public;

    // 256 bits of entropy, on the stack: a data_chunk would put the seed
    // material on the heap, where a reallocation could strand a copy we never
    // get to wipe.
    auto entropy = pseudo_random::generate<byte_array<32>>();

    auto mnemonics = create_mnemonic(entropy, lexicon);

    // Erase the seed material. std::fill here compiled to nothing at -O2:
    // nothing reads the buffer afterwards, so the store was dead and the
    // optimizer dropped it.
    pseudo_random::wipe(entropy);

    auto seed = normalized_passphrase.empty() ?
        decode_mnemonic(mnemonics) :
        decode_mnemonic_normalized_passphrase(mnemonics, normalized_passphrase);

    data_chunk seed_chunk(seed.begin(), seed.end());

    auto m = hd_private::from_seed(seed_chunk, hd_private::mainnet);
    std::fill(seed_chunk.begin(), seed_chunk.end(), 0);
    if ( ! m) return std::unexpected(m.error());

    auto m44h = m->derive_private(44 + hd_first_hardened_key);
    if ( ! m44h) return std::unexpected(m44h.error());

    auto m44h145h = m44h->derive_private(145 + hd_first_hardened_key);
    if ( ! m44h145h) return std::unexpected(m44h145h.error());

    auto m44h145h0h = m44h145h->derive_private(0 + hd_first_hardened_key);
    if ( ! m44h145h0h) return std::unexpected(m44h145h0h.error());

    hd_public pub = m44h145h0h->to_public();

    m->wipe();
    m44h->wipe();
    m44h145h->wipe();
    m44h145h0h->wipe();

    auto const salt = generate_salt();
    auto const iv = generate_salt<default_iv_size>();
    auto aes_key = derive_key(password, salt);

    AES_ctx ctx;
    AES_init_ctx_iv(&ctx, aes_key.data(), iv.data());
    AES_CBC_encrypt_buffer(&ctx, seed.data(), seed.size());
    std::fill(aes_key.begin(), aes_key.end(), 0);

    wallet_data data {
        std::move(mnemonics),
        std::move(pub),
        {}
    };

    std::copy(salt.begin(), salt.end(), data.encrypted_seed.begin());
    std::copy(iv.begin(), iv.end(), data.encrypted_seed.begin() + salt.size());
    std::copy(seed.begin(), seed.end(), data.encrypted_seed.begin() + salt.size() + iv.size());

    return data;
}

std::expected<long_hash, std::error_code>
decrypt_seed(
    std::string const& password,
    encrypted_seed_t const& encrypted_seed) {

    std::array<uint8_t, default_salt_size> salt;
    std::array<uint8_t, default_iv_size> iv;
    std::array<uint8_t, encrypted_seed_size> seed; // = {};

    static_assert(encrypted_seed_size == 64, "encrypted_seed_size must be equal to 64");

    std::copy(encrypted_seed.begin(), encrypted_seed.begin() + default_salt_size, salt.begin());
    std::copy(encrypted_seed.begin() + default_salt_size, encrypted_seed.begin() + default_salt_size + default_iv_size, iv.begin());
    std::copy(encrypted_seed.begin() + default_salt_size + default_iv_size, encrypted_seed.end(), seed.begin());

    auto const aes_key = derive_key(password, salt);

    AES_ctx ctx;
    AES_init_ctx_iv(&ctx, aes_key.data(), iv.data());
    AES_CBC_decrypt_buffer(&ctx, seed.data(), seed.size());

    return seed;
}

} // namespace kth::domain::wallet