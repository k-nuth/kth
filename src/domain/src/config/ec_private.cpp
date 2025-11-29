// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/config/ec_private.hpp>

#include <iostream>
#include <sstream>
#include <string>

#include <boost/program_options.hpp>

// #include <kth/domain.hpp>
// #include <kth/domain/define.hpp>
#include <kth/infrastructure/formats/base_16.hpp>

namespace kth::domain::config {

// ec_secret base16 format is private to bx.
static
bool decode_secret(ec_secret& secret, std::string const& encoded) {
    auto result = decode_base16<ec_secret_size>(encoded);
    if ( ! result) {
        return false;
    }
    secret = *result;
    return verify(secret);
}

ec_private::ec_private(std::string const& hexcode) {
    //TODO(fernando): Eliminate std::stringstream everywhere (performance)
    std::stringstream(hexcode) >> *this;
}

ec_private::ec_private(ec_secret const& secret)
    : value_(secret) {
}

ec_private::operator ec_secret const&() const {
    return value_;
}

std::istream& operator>>(std::istream& input, ec_private& argument) {
    std::string hexcode;
    input >> hexcode;

    if ( ! decode_secret(argument.value_, hexcode)) {
        BOOST_THROW_EXCEPTION(boost::program_options::invalid_option_value(hexcode));
    }

    return input;
}

std::ostream& operator<<(std::ostream& output, ec_private const& argument) {
    output << encode_base16(argument.value_);
    return output;
}

} // namespace kth::domain::config
