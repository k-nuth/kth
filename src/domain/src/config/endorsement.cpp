// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/config/endorsement.hpp>

#include <string>
#include <string_view>
#include <utility>

#include <kth/domain.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/error.hpp>

namespace kth::domain::config {

// static
expect<endorsement> endorsement::parse_from(std::string_view hexcode) {
    auto decoded = decode_base16(std::string{hexcode});
    if ( ! decoded || decoded->size() > max_endorsement_size) {
        return std::unexpected(kth::error::illegal_value);
    }
    return endorsement{std::move(*decoded)};
}

std::string endorsement::to_string() const {
    return encode_base16(value_);
}

} // namespace kth::domain::config
