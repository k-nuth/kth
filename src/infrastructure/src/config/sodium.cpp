// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/sodium.hpp>

#include <iostream>
#include <sstream>
#include <string>

#if ! defined(__EMSCRIPTEN__)
#include <boost/program_options.hpp>
#endif

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/formats/base_85.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth::infrastructure::config {

sodium::sodium()
    : value_(null_hash)
{}

sodium::sodium(std::string_view base85) {
    std::stringstream(std::string(base85)) >> *this;
}

sodium::sodium(hash_digest const& value)
    : value_(value)
{}

sodium::sodium(sodium const& x)
    : sodium(x.value_)
{}

sodium::operator hash_digest const&() const {
    return value_;
}

sodium::operator data_slice() const {
    return value_;
}

sodium::operator bool const() const {
    return value_ != null_hash;
}

std::string sodium::to_string() const {
    std::stringstream value;
    value << *this;
    return value.str();
}

std::istream& operator>>(std::istream& input, sodium& argument) {
    std::string base85;
    input >> base85;

    data_chunk out_value;
    if ( ! decode_base85(out_value, base85) || out_value.size() != hash_size) {
#if ! defined(__EMSCRIPTEN__)
        using namespace boost::program_options;
        BOOST_THROW_EXCEPTION(invalid_option_value(base85));
#else
        throw std::invalid_argument(base85);
#endif
    }

    std::copy_n(out_value.begin(), hash_size, argument.value_.begin());
    return input;
}

std::ostream& operator<<(std::ostream& output, const sodium& argument) {
    std::string decoded;

    // Z85 requires four byte alignment (hash_digest is 32).
    /* bool */ encode_base85(decoded, argument.value_);

    output << decoded;
    return output;
}

} // namespace kth::infrastructure::config
