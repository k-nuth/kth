// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/ek_token.hpp>

#include <string>
#include <string_view>

#include <kth/domain/deserialization.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_58.hpp>
#include <kth/infrastructure/math/checksum.hpp>

namespace kth::domain::wallet {

// static
expect<ek_token> ek_token::parse_from(std::string_view encoded) {
    auto const key = decode_base58<encrypted_token_decoded_size>(encoded);
    if ( ! key || ! verify_checksum(*key)) {
        return std::unexpected(kth::error::illegal_value);
    }
    return ek_token{*key};
}

std::string ek_token::to_string() const {
    return encode_base58(token_);
}


} // namespace kth::domain::wallet
