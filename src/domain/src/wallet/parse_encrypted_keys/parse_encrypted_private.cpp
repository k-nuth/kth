// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "parse_encrypted_private.hpp"

#include <cstddef>
#include <cstdint>

#include <kth/domain/wallet/encrypted_keys.hpp>
#include <kth/infrastructure/math/checksum.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

#include "parse_encrypted_key.hpp"
#include "parse_encrypted_prefix.hpp"

namespace kth::domain::wallet {

const byte_array<parse_encrypted_private::magic_size>
    parse_encrypted_private::magic_{
        {0x01}};

byte_array<parse_encrypted_private::prefix_size>
parse_encrypted_private::prefix_factory(uint8_t address, bool multiplied) {
    auto const base = multiplied ? multiplied_context_ : default_context_;
    auto const context = base + address;
    return splice(magic_, to_array(context));
}

parse_encrypted_private::parse_encrypted_private(encrypted_private const& key)
    : parse_encrypted_key<prefix_size>(
          slice<0, 2>(key),
          slice<2, 3>(key),
          slice<3, 7>(key),
          slice<7, 15>(key)),
      data1_(slice<15, 23>(key)),
      data2_(slice<23, 39>(key)) {
    valid(verify_magic() && verify_checksum(key));
}

uint8_t parse_encrypted_private::address_version() const {
    auto const base = multiplied() ? multiplied_context_ : default_context_;
    return context() - base;
}

quarter_hash parse_encrypted_private::data1() const {
    return data1_;
}

half_hash parse_encrypted_private::data2() const {
    return data2_;
}

bool parse_encrypted_private::multiplied() const {
    // This is a double negative (multiplied = not not multiplied).
    return (flags() & ek_flag::ec_non_multiplied) == 0;
}

bool parse_encrypted_private::verify_magic() const {
    return slice<0, magic_size>(prefix()) == magic_;
}

} // namespace kth::domain::wallet
