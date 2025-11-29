// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/config/transaction.hpp>

#include <iostream>
#include <sstream>
#include <string>

#include <boost/program_options.hpp>

#include <kth/domain/chain/transaction.hpp>
#include <kth/infrastructure/config/base16.hpp>
#include <kth/infrastructure/formats/base_16.hpp>

namespace kth::domain::config {

using namespace boost::program_options;
using namespace infrastructure::config;

transaction::transaction(chain::transaction const& value)
    : value_(value) {
}

transaction::transaction(transaction const& x)
    : transaction(x.value_) {
}

chain::transaction& transaction::data() {
    return value_;
}

transaction::operator chain::transaction const&() const {
    return value_;
}

std::expected<transaction, std::error_code> transaction::from_string(std::string_view text) noexcept {
    auto const bytes_result = base16::from_string(text);
    if ( ! bytes_result) {
        return std::unexpected(bytes_result.error());
    }
    data_chunk const& bytes = *bytes_result;
    byte_reader reader(bytes);
    auto transaction_exp = chain::transaction::from_data(reader, true);
    if ( ! transaction_exp) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    return transaction(std::move(*transaction_exp));
}

std::string transaction::to_string() const {
    return kth::encode_base16(value_.to_data());
}

} // namespace kth::domain::config
