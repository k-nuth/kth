// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/base64.hpp>

#include <string>

#include <kth/infrastructure/formats/base_64.hpp>

namespace kth::infrastructure::config {

base64::base64(data_chunk const& value)
    : value_(value)
{}

base64::base64(data_chunk&& value)
    : value_(std::move(value))
{}

base64::operator data_chunk const&() const noexcept {
    return value_;
}

base64::operator data_slice() const noexcept {
    return value_;
}

data_chunk const& base64::data() const noexcept {
    return value_;
}

data_slice base64::as_slice() const noexcept {
    return value_;
}

std::expected<base64, std::error_code> base64::from_string(std::string_view text) noexcept {
    data_chunk value;
    if ( ! decode_base64(value, text)) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    return base64(std::move(value));
}

std::string base64::to_string() const {
    return encode_base64(value_);
}

} // namespace kth::infrastructure::config
