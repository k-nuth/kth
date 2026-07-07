// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/ek_public.hpp>

#include <string>
#include <string_view>

#include <kth/domain/deserialization.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_58.hpp>
#include <kth/infrastructure/math/checksum.hpp>

namespace kth::domain::wallet {

// static
expect<ek_public> ek_public::parse_from(std::string_view encoded) {
    encrypted_public key;
    if ( ! decode_base58(key, std::string{encoded}) || ! verify_checksum(key)) {
        return std::unexpected(kth::error::illegal_value);
    }
    return ek_public{key};
}

std::string ek_public::to_string() const {
    return encode_base58(public_);
}


} // namespace kth::domain::wallet
