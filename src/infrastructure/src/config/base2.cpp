// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/base2.hpp>

#include <string>
#include <string_view>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/binary.hpp>

namespace kth::infrastructure::config {

// static
std::expected<base2, kth::code> base2::parse_from(std::string_view text) noexcept {
    if ( ! binary::is_base2(text)) {
        return std::unexpected(kth::error::illegal_value);
    }
    return base2{binary{text}};
}

std::string base2::to_string() const {
    return value_.encoded();
}

} // namespace kth::infrastructure::config
