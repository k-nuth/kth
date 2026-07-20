// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "parse_encrypted_token.hpp"

#include <cstddef>
#include <cstdint>

#include <kth/domain/wallet/encrypted_keys.hpp>
#include <kth/infrastructure/math/checksum.hpp>
#include <kth/infrastructure/utility/data.hpp>

#include "parse_encrypted_prefix.hpp"

namespace kth::domain::wallet {

// This prefix results in the prefix "passphrase" in the base58 encoding.
// The prefix is not modified as the result of variations to address.
const byte_array<parse_encrypted_token::magic_size> parse_encrypted_token::magic_{
    {0x2c, 0xe9, 0xb3, 0xe1, 0xff, 0x39, 0xe2}};

byte_array<parse_encrypted_token::prefix_size>
parse_encrypted_token::prefix_factory(bool lot_sequence) {
    auto const context = lot_sequence ? lot_context_ : default_context_;
    return splice(magic_, to_array(context));
}

parse_encrypted_token::parse_encrypted_token(encrypted_token const& value)
    : parse_encrypted_prefix(slice<0, 8>(value)),
      entropy_(slice<8, 16>(value)),
      sign_(slice<16, 17>(value)),
      data_(slice<17, 49>(value)) {
    valid(verify_magic() && verify_context() && verify_checksum(value));
}

hash_digest parse_encrypted_token::data() const {
    return data_;
}

ek_entropy parse_encrypted_token::entropy() const {
    return entropy_;
}

bool parse_encrypted_token::lot_sequence() const {
    // There is no "flags" byte in token, we rely on prefix context.
    return context() == lot_context_;
}

one_byte parse_encrypted_token::sign() const {
    return sign_;
}

bool parse_encrypted_token::verify_context() const {
    return context() == default_context_ || context() == lot_context_;
}

bool parse_encrypted_token::verify_magic() const {
    return slice<0, magic_size>(prefix()) == magic_;
}

} // namespace kth::domain::wallet
