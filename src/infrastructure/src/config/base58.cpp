// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/base58.hpp>

#include <iostream>
#include <sstream>
#include <string>

#if ! defined(__EMSCRIPTEN__)
#include <boost/program_options.hpp>
#endif

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/formats/base_58.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::infrastructure::config {

// base58::base58()
// {
// }

base58::base58(std::string_view base58) {
    std::stringstream(std::string(base58)) >> *this;
}

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

std::istream& operator>>(std::istream& input, base58& argument) {
    std::string base58;
    input >> base58;

    if ( ! decode_base58(argument.value_, base58)) {
#if ! defined(__EMSCRIPTEN__)
        using namespace boost::program_options;
        BOOST_THROW_EXCEPTION(invalid_option_value(base58));
#else
        throw std::invalid_argument(base58);
#endif
    }

    return input;
}

std::ostream& operator<<(std::ostream& output, const base58& argument) {
    output << encode_base58(argument.value_);
    return output;
}

} // namespace kth::infrastructure::config
