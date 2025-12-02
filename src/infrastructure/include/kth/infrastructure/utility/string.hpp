// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_STRING_HPP
#define KTH_INFRASTRUCTURE_STRING_HPP

#include <string>
#include <vector>

#include <kth/infrastructure/define.hpp>
// #include <kth/infrastructure/define.hpp>
// #include <kth/infrastructure/utility/data.hpp>
// #include <boost/algorithm/string/trim.hpp>
// #include <boost/lexical_cast.hpp>

namespace kth {

using string_list = std::vector<std::string>;

/**
 * Convert a text string to the specified type.
 * @param      <Value>  The converted type.
 * @param[in]  text     The text to convert.
 * @param[in]  trim     True if value should be trimmed before conversion.
 * return               The parsed value.
 */
template <typename Value>
Value deserialize(std::string const& text, bool trim);

/**
 * Convert a text string to the specified type.
 * @param      <Value>  The converted type.
 * @param[out] value    The parsed value.
 * @param[in]  text     The text to convert.
 * @param[in]  trim     True if value should be trimmed before conversion.
 */
template <typename Value>
void deserialize(Value& value, std::string const& text, bool trim);

/**
 * Deserialize the tokens of a text string to a vector of the inner type.
 * @param      <Value>     The inner type.
 * @param[out] collection  The parsed vector value.
 * @param[in]  text        The text to convert.
 * @param[in]  trim        True if value should be trimmed before conversion.
 */
template <typename Value>
void deserialize(std::vector<Value>& collection, std::string const& text,
    bool trim);

/**
 * Conveniently convert an instance of the specified type to string.
 * @param      <Value>   The type to serialize.
 * @param[in]  value     The instance to convert.
 * @param[in]  fallback  The text to populate if value is empty.
 * @return               The serialized value.
 */
template <typename Value>
std::string serialize(const Value& value, std::string const& fallback="");

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
KI_API string_list split(std::string const& sentence,
    std::string const& delimiter=" ", bool trim=true);

} // namespace kth

#include <kth/infrastructure/impl/utility/string.ipp>

#endif
