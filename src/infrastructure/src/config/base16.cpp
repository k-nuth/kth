// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/base16.hpp>

#include <string>

#include <kth/infrastructure/formats/base_16.hpp>

namespace kth::infrastructure::config {

base16::base16(data_chunk const& value)
    : value_(value)
{}

base16::base16(data_chunk&& value)
    : value_(std::move(value))
{}

base16::operator data_chunk const&() const noexcept {
    return value_;
}

base16::operator data_slice() const noexcept {
    return value_;
}

data_chunk const& base16::data() const noexcept {
    return value_;
}

data_slice base16::as_slice() const noexcept {
    return value_;
}

std::expected<base16, std::error_code> base16::from_string(std::string_view text) noexcept {
    auto result = decode_base16(text);
    if ( ! result) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    return base16(std::move(*result));
}

std::string base16::to_string() const {
    return encode_base16(value_);
}

} // namespace kth::infrastructure::config
