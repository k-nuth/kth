// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/base58.hpp>

#include <string>

#include <kth/infrastructure/formats/base_58.hpp>

namespace kth::infrastructure::config {

base58::base58(data_chunk const& value)
    : value_(value)
{}

base58::base58(data_chunk&& value)
    : value_(std::move(value))
{}

base58::operator data_chunk const&() const noexcept {
    return value_;
}

base58::operator data_slice() const noexcept {
    return value_;
}

data_chunk const& base58::data() const noexcept {
    return value_;
}

data_slice base58::as_slice() const noexcept {
    return value_;
}

std::expected<base58, std::error_code> base58::from_string(std::string_view text) noexcept {
    data_chunk value;
    if ( ! decode_base58(value, text)) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    return base58(std::move(value));
}

std::string base58::to_string() const {
    return encode_base58(value_);
}

} // namespace kth::infrastructure::config
