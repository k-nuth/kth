// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/base64.hpp>

#include <iostream>
#include <sstream>
#include <string>

#if ! defined(__EMSCRIPTEN__)
#include <boost/program_options.hpp>
#endif

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/formats/base_64.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::infrastructure::config {

base64::base64(std::string_view base64) {
    std::stringstream(std::string(base64)) >> *this;
}

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

std::istream& operator>>(std::istream& input, base64& argument) {
    std::string base64;
    input >> base64;

    if ( ! decode_base64(argument.value_, base64)) {
#if ! defined(__EMSCRIPTEN__)
        using namespace boost::program_options;
        BOOST_THROW_EXCEPTION(invalid_option_value(base64));
#else
        throw std::invalid_argument(base64);
#endif
    }

    return input;
}

std::ostream& operator<<(std::ostream& output, base64 const& argument) {
    output << encode_base64(argument.value_);
    return output;
}

} // namespace kth::infrastructure::config
