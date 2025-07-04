// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/config/script.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>

#include <kth/domain/chain/script.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/string.hpp>
#include <kth/infrastructure/config/base16.hpp>


namespace kth::domain::config {

using namespace boost;
using namespace boost::program_options;

script::script(std::string const& mnemonic) {
    std::stringstream(mnemonic) >> *this;
}

script::script(chain::script const& value)
    : value_(value) {
}

script::script(data_chunk const& value) {
    byte_reader reader(value);
    auto script_exp = chain::script::from_data(reader, false);
    if ( ! script_exp) {
        BOOST_THROW_EXCEPTION(invalid_option_value(encode_base16(value)));
    }
    value_ = std::move(*script_exp);
}

script::script(const std::vector<std::string>& tokens) {
    auto const mnemonic = join(tokens);
    std::stringstream(mnemonic) >> *this;
}

script::script(script const& x)
    : script(x.value_) {
}

data_chunk script::to_data() const {
    return value_.to_data(false);
}

std::string script::to_string() const {
    static constexpr auto flags = machine::rule_fork::all_rules;
    return value_.to_string(flags);
}

script::operator chain::script const&() const {
    return value_;
}

std::istream& operator>>(std::istream& input, script& argument) {
    std::istreambuf_iterator<char> end;
    std::string mnemonic(std::istreambuf_iterator<char>(input), end);
    boost::trim(mnemonic);

    // Test for invalid result sentinel.
    if ( ! argument.value_.from_string(mnemonic) && mnemonic.length() > 0) {
        BOOST_THROW_EXCEPTION(invalid_option_value(mnemonic));
    }

    return input;
}

std::ostream& operator<<(std::ostream& output, script const& argument) {
    static constexpr auto flags = machine::rule_fork::all_rules;
    output << argument.value_.to_string(flags);
    return output;
}

} // namespace kth::domain::config
