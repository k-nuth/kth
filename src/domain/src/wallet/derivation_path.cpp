// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/derivation_path.hpp>

#include <array>

#include <kth/infrastructure/error.hpp>

namespace kth::domain::wallet {

expect<std::vector<uint32_t>> parse_derivation_path(std::string_view path) {
    // BIP32 bounds the scratch space; no heap allocation occurs until validation succeeds.
    std::array<uint32_t, 255> indexes;
    auto const count = parse_derivation_path(path, indexes);
    if ( ! count) return std::unexpected(kth::error::illegal_value);
    return std::vector<uint32_t>(indexes.begin(), indexes.begin() + *count);
}

} // namespace kth::domain::wallet
