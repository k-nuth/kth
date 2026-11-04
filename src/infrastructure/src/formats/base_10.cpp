// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/formats/base_10.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <system_error>

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
    uint64_t number = 0;
    if ( ! value.empty()) {
        auto const [end, ec] = std::from_chars(value.data(), value.data() + value.size(), number);
        if (ec != std::errc{} || end != value.data() + value.size()) {
            return false;
        }
    }

    // Round and return:
    if (round && number == max_uint64) {
        return false;
    }

    out = number + static_cast<unsigned long>(round);
    return true;
}

std::string encode_base10(uint64_t amount, uint8_t decimal_places) {
    // uint64_t has at most digits10 + 1 decimal digits; to_chars cannot exhaust
    // this buffer and does not allocate or consult the locale.
    std::array<char, std::numeric_limits<uint64_t>::digits10 + 1> digits;
    auto const end = std::to_chars(digits.data(), digits.data() + digits.size(), amount).ptr;
    auto const size = size_t(end - digits.data());
    if (decimal_places == 0) {
        return {digits.data(), size};
    }

    auto const whole = size > decimal_places ? size - decimal_places : 0;
    auto fraction_end = size;
    while (fraction_end > whole && digits[fraction_end - 1] == '0') {
        --fraction_end;
    }
    auto const padding = decimal_places > size ? decimal_places - size : 0;
    auto const has_fraction = fraction_end > whole;
    std::string out;
    out.reserve(std::max(size_t(1), whole) +
        (has_fraction ? 1 + padding + fraction_end - whole : 0));
    if (whole != 0) {
        out.append(digits.data(), whole);
    } else {
        out.push_back('0');
    }
    if (has_fraction) {
        out.push_back('.');
        out.append(padding, '0');
        out.append(digits.data() + whole, fraction_end - whole);
    }
    return out;
}

bool btc_to_satoshi(uint64_t& satoshi, std::string const& btc) {
    return decode_base10(satoshi, btc, btc_decimal_places);
}

std::string satoshi_to_btc(uint64_t satoshi) {
    return encode_base10(satoshi, btc_decimal_places);
}

} // namespace kth
