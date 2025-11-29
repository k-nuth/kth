// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/config/endorsement.hpp>

#include <array>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>

#include <boost/program_options.hpp>

#include <kth/domain.hpp>
#include <kth/domain/define.hpp>

namespace kth::domain::config {

// endorsement format is currently private to bx.
static
bool decode_endorsement(kth::endorsement& endorsement,
                               std::string const& encoded) {
    auto decoded = decode_base16(encoded);
    if ( ! decoded || decoded->size() > max_endorsement_size) {
        return false;
    }

    endorsement = std::move(*decoded);
    return true;
}

static
std::string encode_endorsement(data_slice signature) {
    return encode_base16(signature);
}

endorsement::endorsement(std::string const& hexcode) {
    std::stringstream(hexcode) >> *this;
}

endorsement::endorsement(data_chunk const& value)
    : value_(value) {
}

endorsement::endorsement(const endorsement& x)
    : endorsement(x.value_) {
}

endorsement::operator data_chunk const&() const {
    return value_;
}

endorsement::operator data_slice() const {
    return value_;
}

std::istream& operator>>(std::istream& input, endorsement& argument) {
    std::string hexcode;
    input >> hexcode;

    if ( ! decode_endorsement(argument.value_, hexcode)) {
        BOOST_THROW_EXCEPTION(boost::program_options::invalid_option_value(hexcode));
    }

    return input;
}

std::ostream& operator<<(std::ostream& output, const endorsement& argument) {
    output << encode_endorsement(argument.value_);
    return output;
}

} // namespace kth::domain::config