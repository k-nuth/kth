// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/base2.hpp>

#include <iostream>
#include <sstream>
#include <string>

#if ! defined(__EMSCRIPTEN__)
#include <boost/program_options.hpp>
#endif

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/binary.hpp>

namespace kth::infrastructure::config {

base2::base2(binary const& value)
    : value_(value)
{}

size_t base2::size() const noexcept {
    return value_.size();
}

base2::operator binary const&() const noexcept {
    return value_;
}

std::expected<base2, std::error_code> base2::from_string(std::string_view text) noexcept {
    if ( ! binary::is_base2(text)) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    return base2(binary(text));
}

std::string base2::to_string() const {
    return value_.encoded();
}

} // namespace kth::infrastructure::config
