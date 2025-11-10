// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/wallet_manager.hpp>

#include <kth/infrastructure/utility/random.hpp>
#include <kth/infrastructure/wallet/dictionary.hpp>
#include <kth/infrastructure/wallet/mnemonic.hpp>

#include <expected>


#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/wallet/hd_public.hpp>
#include <kth/infrastructure/wallet/hd_private.hpp>


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

template <typename T>
void clear_hd(T& hd) {
    using std::swap;
    T tmp;
    swap(hd, tmp);
}

std::expected<wallet_data, std::error_code>
create_wallet(
    std::string const& password,
    std::string const& normalized_passphrase,
    kth::infrastructure::wallet::dictionary const& lexicon /* =kth::infrastructure::wallet::language::en */) {

    using kth::infrastructure::wallet::create_mnemonic;
    using kth::infrastructure::wallet::decode_mnemonic;
    using kth::infrastructure::wallet::decode_mnemonic_normalized_passphrase;
    using kth::infrastructure::wallet::hd_first_hardened_key;
    using kth::infrastructure::wallet::hd_private;
    using kth::infrastructure::wallet::hd_public;

    data_chunk entropy(32); // 256 bits entropy
    pseudo_random_fill(entropy);

    auto mnemonics = create_mnemonic(entropy, lexicon);

    // entropy is a vector of 32 bytes, after using it, fill it with 0
    std::fill(entropy.begin(), entropy.end(), 0);

    auto seed = normalized_passphrase.empty() ?
        decode_mnemonic(mnemonics) :
        decode_mnemonic_normalized_passphrase(mnemonics, normalized_passphrase);

    data_chunk seed_chunk(seed.begin(), seed.end());

    hd_private m(seed_chunk, hd_private::mainnet);
    std::fill(seed_chunk.begin(), seed_chunk.end(), 0);
    hd_private m44h = m.derive_private(44 + hd_first_hardened_key);
    hd_private m44h145h = m44h.derive_private(145 + hd_first_hardened_key);
    hd_private m44h145h0h = m44h145h.derive_private(0 + hd_first_hardened_key);
    hd_public pub = m44h145h0h.to_public();

    // erase all the intermediate hd_private and hd_public objects
    clear_hd(m);
    clear_hd(m44h);
    clear_hd(m44h145h);
    clear_hd(m44h145h0h);

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