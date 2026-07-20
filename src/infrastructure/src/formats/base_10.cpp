// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/formats/base_10.hpp>

#include <iomanip>
#include <sstream>

#include <boost/algorithm/string.hpp>

#include <kth/infrastructure/constants.hpp>

namespace kth {

/**
 * Returns true if a character is one of `[0-9]`.
 *
 * The C standard library function `isdigit` depends on the current locale,
 * and doesn't necessarily correspond to the valid base10 encoding digits.
 * The Microsoft `isdigit` includes superscript characters like '²'
 * in some locales, for example.
 */
static bool is_digit(char c) {
    return '0' <= c && c <= '9';
}

template <char C>
bool char_is(char c) {
    return c == C;
}

bool decode_base10(uint64_t& out, std::string const& amount, uint8_t decimal_places, bool strict) {
    std::string value(amount);

    // Get rid of the decimal point:
    auto point = std::find(value.begin(), value.end(), '.');
    if (point != value.end()) {
        point = value.erase(point);
    }

    // Only digits should remain:
    if ( ! std::all_of(value.begin(), value.end(), is_digit)) {
        return false;
    }

    // Add digits to the end if there are too few:
    auto actual_places = value.end() - point;
    if (actual_places < decimal_places) {
        value.append(decimal_places - actual_places, '0');
    }

    // Remove digits from the end if there are too many:
    bool round = false;
    if (actual_places > decimal_places) {
        auto end = point + decimal_places;
        round = !std::all_of(end, value.end(), char_is<'0'>);
        value.erase(end, value.end());
    }

    if (strict && round) {
        return false;
    }

    // Convert to an integer:
    std::istringstream stream(value);
    uint64_t number = 0;
    if ( ! value.empty() && ! (stream >> number)) {
        return false;
    }

    // Round and return:
    if (round && number == max_uint64) {
        return false;
    }

    out = number + static_cast<unsigned long>(round);
    return true;
}

std::string encode_base10(uint64_t amount, uint8_t decimal_places) {
    std::ostringstream stream;
    stream << std::setfill('0') << std::setw(1 + decimal_places) << amount;

    auto string = stream.str();
    string.insert(string.size() - decimal_places, 1, '.');
    boost::algorithm::trim_right_if(string, char_is<'0'>);
    boost::algorithm::trim_right_if(string, char_is<'.'>);
    return string;
}

bool btc_to_satoshi(uint64_t& satoshi, std::string const& btc) {
    return decode_base10(satoshi, btc, btc_decimal_places);
}

std::string satoshi_to_btc(uint64_t satoshi) {
    return encode_base10(satoshi, btc_decimal_places);
}

} // namespace kth
