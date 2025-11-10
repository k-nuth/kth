// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/hash160.hpp>

#include <iostream>
#include <sstream>
#include <string>

#if ! defined(__EMSCRIPTEN__)
#include <boost/program_options.hpp>
#endif

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth::infrastructure::config {

hash160::hash160(std::string_view hexcode) {
    std::stringstream(std::string(hexcode)) >> *this;
}

hash160::hash160(short_hash const& value)
    : value_(value)
{}

// hash160::hash160(byte_span value) {
//     if (value.size() != value_.size()) {
//          using namespace boost::program_options;
//         BOOST_THROW_EXCEPTION(invalid_option_value("byte array size is not 20"));
//     }

//     std::copy_n(value.begin(), value_.size(), value_.begin());
// }

hash160::operator short_hash const&() const noexcept {
    return value_;
}

std::istream& operator>>(std::istream& input, hash160& argument) {
    std::string hexcode;
    input >> hexcode;

    if ( ! decode_base16(argument.value_, hexcode)) {
#if ! defined(__EMSCRIPTEN__)
        using namespace boost::program_options;
        BOOST_THROW_EXCEPTION(invalid_option_value(hexcode));
#else
        throw std::invalid_argument(hexcode);
#endif
    }

    return input;
}

std::ostream& operator<<(std::ostream& output, hash160 const& argument) {
    output << encode_base16(argument.value_);
    return output;
}

} // namespace kth::infrastructure::config
