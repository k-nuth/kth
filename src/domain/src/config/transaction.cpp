// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/config/transaction.hpp>

#include <string>
#include <string_view>
#include <utility>

#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/infrastructure/config/base16.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_16.hpp>

namespace kth::domain::config {

// static
expect<transaction> transaction::parse_from(std::string_view text) noexcept {
    auto const bytes_result = infrastructure::config::base16::parse_from(text);
    if ( ! bytes_result) {
        return std::unexpected(bytes_result.error());
    }
    byte_reader reader{bytes_result->value()};
    auto tx = chain::transaction::from_data(reader, true);
    if ( ! tx) {
        return std::unexpected(tx.error());
    }
    return transaction{std::move(*tx)};
}

std::string transaction::to_string() const {
    return kth::encode_base16(kth::to_data_chunk(value_, true));
}

} // namespace kth::domain::config
