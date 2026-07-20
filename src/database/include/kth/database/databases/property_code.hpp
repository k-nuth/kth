// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_PROPERTY_CODE_HPP_
#define KTH_DATABASE_PROPERTY_CODE_HPP_

#include <algorithm>
#include <cctype>
#include <expected>
#include <iterator>
#include <string>
#include <string_view>

#include <kth/infrastructure/error.hpp>

namespace kth::database {

/// Named keys for the internal database's `properties` bucket. Values
/// are the on-disk identifiers, so append-only (never renumber).
enum class property_code {
    db_mode            = 0,
    last_header_height = 1,
    last_block_height  = 2,
    utxo_built_height  = 3,  // Last block height for which UTXO set was built
};

enum class db_mode_type {
    pruned,
    blocks,
    full,
};

[[nodiscard]] inline
std::expected<db_mode_type, kth::code> parse_db_mode(std::string_view text) {
    std::string upper;
    upper.reserve(text.size());
    std::transform(text.begin(), text.end(), std::back_inserter(upper),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    if (upper == "PRUNED") return db_mode_type::pruned;
    if (upper == "BLOCKS") return db_mode_type::blocks;
    if (upper == "FULL")   return db_mode_type::full;
    return std::unexpected(kth::error::illegal_value);
}

} // namespace kth::database

#endif // KTH_DATABASE_PROPERTY_CODE_HPP_
