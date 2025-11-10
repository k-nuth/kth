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

base2::base2(std::string_view binary) {
    std::stringstream(std::string(binary)) >> *this;
}

base2::base2(binary const& value)
    : value_(value)
{}

size_t base2::size() const {
    return value_.size();
}

base2::operator binary const&() const {
    return value_;
}

std::istream& operator>>(std::istream& input, base2& argument) {
    std::string binary;
    input >> binary;

    if ( ! binary::is_base2(binary)) {
#if ! defined(__EMSCRIPTEN__)
        using namespace boost::program_options;
        BOOST_THROW_EXCEPTION(invalid_option_value(binary));
#else
        throw std::invalid_argument(binary);
#endif
    }

    std::stringstream(binary) >> argument.value_;

    return input;
}

std::ostream& operator<<(std::ostream& output, const base2& argument) {
    output << argument.value_;
    return output;
}

} // namespace kth::infrastructure::config
