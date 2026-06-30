// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/config/input.hpp>

#include <iostream>
#include <sstream>
#include <string>

#include <boost/program_options.hpp>

#include <kth/domain/chain/input.hpp>
#include <kth/domain/chain/input_point.hpp>
#include <kth/domain/config/point.hpp>
#include <kth/infrastructure/utility/string.hpp>

namespace kth::domain::config {

using namespace boost::program_options;

// input is currently a private encoding in bx.
static
chain::input_opt decode_input(std::string const& tuple) {
    auto const tokens = split(tuple, point::delimeter);
    if (tokens.size() != 2 && tokens.size() != 3) {
        return std::nullopt;
    }

    auto const sequence = (tokens.size() == 3)
        ? deserialize<uint32_t>(tokens[2], true)
        : max_input_sequence;

    return chain::input{
        chain::output_point{point(tokens[0] + ":" + tokens[1])},
        chain::script{},
        sequence
    };
}

// input is currently a private encoding in bx.
static
std::string encode_input(chain::input const& input) {
    std::stringstream result;
    result << point(input.previous_output()) << point::delimeter
           << input.sequence();

    return result.str();
}

input::input(std::string const& tuple) {
    std::stringstream(tuple) >> *this;
}

input::input(chain::input const& value)
    : value_(value) {}

input::input(input const& x)
    : value_(x.value_) {}

input::input(chain::input_point const& value)
    : value_(chain::input{value, {}, max_input_sequence}) {}

input::operator chain::input const&() const {
    KTH_ASSERT(value_.has_value());
    return *value_;
}

std::istream& operator>>(std::istream& stream, input& argument) {
    std::string tuple;
    stream >> tuple;

    auto decoded = decode_input(tuple);
    if ( ! decoded) {
        BOOST_THROW_EXCEPTION(invalid_option_value(tuple));
    }
    argument.value_ = std::move(*decoded);

    return stream;
}

std::ostream& operator<<(std::ostream& output, input const& argument) {
    KTH_ASSERT(argument.value_.has_value());
    output << encode_input(*argument.value_);
    return output;
}

} // namespace kth::domain::config
