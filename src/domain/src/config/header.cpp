// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/config/header.hpp>

#include <iostream>
#include <sstream>
#include <string>

#include <boost/program_options.hpp>

#include <kth/domain/chain/header.hpp>
#include <kth/infrastructure/config/base16.hpp>
#include <kth/infrastructure/formats/base_16.hpp>

namespace kth::domain::config {

using namespace boost::program_options;
using namespace infrastructure::config;

header::header(chain::header const& value)
    : value_(value) {
}

header::header(header const& x)
    : header(x.value_) {
}

header::operator chain::header const&() const {
    return value_;
}

std::expected<header, std::error_code> header::from_string(std::string_view text) noexcept {
    auto const bytes_result = base16::from_string(text);
    if ( ! bytes_result) {
        return std::unexpected(bytes_result.error());
    }
    data_chunk const& bytes = *bytes_result;
    byte_reader reader(bytes);
    auto header_exp = chain::header::from_data(reader);
    if ( ! header_exp) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    return header(std::move(*header_exp));
}

std::string header::to_string() const {
    return kth::encode_base16(value_.to_data());
}

} // namespace kth::domain::config
