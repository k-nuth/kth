// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_WALLET_WALLET_MANAGER_HPP
#define KTH_WALLET_WALLET_MANAGER_HPP

// #include <cstdint>
// #include <iostream>
// #include <map>
// #include <optional>
#include <string>
#include <vector>

#include <kth/domain/define.hpp>

#include <kth/infrastructure/utility/random.hpp>
#include <kth/infrastructure/wallet/dictionary.hpp>
#include <kth/infrastructure/wallet/hd_public.hpp>
#include <kth/infrastructure/wallet/mnemonic.hpp>

#include <expected>

#define AES256 1
#include <aes.hpp>

namespace kth::domain::wallet {

static_assert(AES_BLOCKLEN == 16, "AES_BLOCKLEN must be equal to 16");
static_assert(std::tuple_size_v<long_hash> == 64, "long_hash must be equal to 64");


template <size_t N>
constexpr
size_t compute_padded_size() {
    return ((N + AES_BLOCKLEN - 1) / AES_BLOCKLEN) * AES_BLOCKLEN;
}

constexpr size_t default_salt_size = 16;
constexpr size_t default_iv_size = AES_BLOCKLEN;
constexpr size_t encrypted_seed_size = compute_padded_size<std::tuple_size_v<long_hash>>();
constexpr size_t total_size = default_salt_size + default_iv_size + encrypted_seed_size;
static_assert(total_size == 96, "total_size must be equal to 96");

using encrypted_seed_t = std::array<uint8_t, total_size>;

template <size_t N = default_salt_size>
constexpr
std::array<uint8_t, N> generate_salt() {
    std::array<uint8_t, N> salt;
    pseudo_random_fill(salt.data(), N);
    return salt;
}

static_assert(encrypted_seed_size == std::tuple_size_v<long_hash>, "encrypted_seed_size must be equal to the size of long_hash");
static_assert(encrypted_seed_size == 64, "encrypted_seed_size must be equal to 64");

struct wallet_data {
    std::vector<std::string> mnemonics;
    kth::infrastructure::wallet::hd_public xpub;
    encrypted_seed_t encrypted_seed;
};

std::expected<wallet_data, std::error_code>
create_wallet(
    std::string const& password,
    std::string const& normalized_passphrase,
    kth::infrastructure::wallet::dictionary const& lexicon=kth::infrastructure::wallet::language::en);

std::expected<long_hash, std::error_code>
decrypt_seed(
    std::string const& password,
    encrypted_seed_t const& encrypted_seed);


} // namespace kth::domain::wallet

#endif // KTH_WALLET_WALLET_MANAGER_HPP
