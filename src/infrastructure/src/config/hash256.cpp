// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/hash256.hpp>

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

hash256::hash256(std::string_view hexcode) {
    std::stringstream(std::string(hexcode)) >> *this;
}

hash256::hash256(hash_digest const& value)
    : value_(value)
{}

std::string hash256::to_string() const {
    std::stringstream value;
    value << *this;
    return value.str();
}

hash256::operator hash_digest const&() const noexcept {
    return value_;
}

std::istream& operator>>(std::istream& input, hash256& argument) {
    std::string hexcode;
    input >> hexcode;

    if ( ! decode_hash(argument.value_, hexcode)) {
#if ! defined(__EMSCRIPTEN__)
        using namespace boost::program_options;
        BOOST_THROW_EXCEPTION(invalid_option_value(hexcode));
#else
        throw std::invalid_argument(hexcode);
#endif
    }

    return input;
}

std::ostream& operator<<(std::ostream& output, hash256 const& argument) {
    output << encode_hash(argument.value_);
    return output;
}

} // namespace kth::infrastructure::config
