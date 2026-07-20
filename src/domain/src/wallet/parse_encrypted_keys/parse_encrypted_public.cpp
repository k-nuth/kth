// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "parse_encrypted_public.hpp"

#include <cstddef>
#include <cstdint>

#include <kth/domain/wallet/encrypted_keys.hpp>
#include <kth/infrastructure/math/checksum.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

#include "parse_encrypted_key.hpp"
#include "parse_encrypted_prefix.hpp"

namespace kth::domain::wallet {

// This prefix results in the prefix "cfrm" in the base58 encoding but is
// modified when the payment address is Bitcoin mainnet (0).
const byte_array<parse_encrypted_public::magic_size>
    parse_encrypted_public::magic_{
        {0x64, 0x3b, 0xf6, 0xa8}};

byte_array<parse_encrypted_public::prefix_size>
parse_encrypted_public::prefix_factory(uint8_t address) {
    auto const context = default_context_ + address;
    return splice(magic_, to_array(context));
}

parse_encrypted_public::parse_encrypted_public(encrypted_public const& key)
    : parse_encrypted_key<prefix_size>(
          slice<0, 5>(key),
          slice<5, 6>(key),
          slice<6, 10>(key),
          slice<10, 18>(key)),
      sign_(slice<18, 19>(key)),
      data_(slice<19, 51>(key)) {
    valid(verify_magic() && verify_checksum(key));
}

uint8_t parse_encrypted_public::address_version() const {
    return context() - default_context_;
}

hash_digest parse_encrypted_public::data() const {
    return data_;
}

one_byte parse_encrypted_public::sign() const {
    return sign_;
}

bool parse_encrypted_public::verify_magic() const {
    return slice<0, magic_size>(prefix()) == magic_;
}

} // namespace kth::domain::wallet
