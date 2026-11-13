// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_STRING_HPP
#define KTH_INFRASTRUCTURE_STRING_HPP

#include <charconv>
#include <concepts>
#include <string>
#include <string_view>
#include <vector>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/expect.hpp>

namespace kth {

using string_list = std::vector<std::string>;

/// Parse a complete decimal integer. Optional trimming accepts ASCII whitespace.
/// Leading '+' is accepted; negative values are rejected for unsigned types.
template <std::integral Value> requires (!std::same_as<Value, bool>)
expect<Value> parse_integer(std::string_view text, bool trim = true) {
    if (trim) {
        constexpr std::string_view whitespace = " \t\n\r\f\v";
        auto const first = text.find_first_not_of(whitespace);
        if (first == std::string_view::npos) {
            return std::unexpected(error::illegal_value);
        }
        text = text.substr(first, text.find_last_not_of(whitespace) - first + 1);
    }
    if (text.empty()) {
        return std::unexpected(error::illegal_value);
    }
    if (text.front() == '+') {
        text.remove_prefix(1);
        if (text.empty() || text.front() < '0' || text.front() > '9') {
            return std::unexpected(error::illegal_value);
        }
    }
    Value value{};
    auto const result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return std::unexpected(error::illegal_value);
    }
    return value;
}

/**
 * Join a list of strings into a single string, in order.
 * @param[in]  words      The list of strings to join.
 * @param[in]  delimiter  The delimiter, defaults to " ".
 * @return                The resulting string.
 */
KI_API std::string join(string_list const& words,
    std::string const& delimiter=" ");

/**
 * Split a list of strings into a string vector string, in order, white space
 * delimited.
 * @param[in]  sentence   The string to split.
 * @param[in]  delimiter  The delimeter, defaults to " ".
 * @param[in]  trim       Trim the sentence for whitespace, defaults to true.
 * @return                The list of resulting strings.
 */
KI_API string_list split(std::string_view sentence,
    std::string_view delimiter=" ", bool trim=true);

} // namespace kth

#endif
