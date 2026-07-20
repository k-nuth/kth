// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_STRING_IPP
#define KTH_STRING_IPP

#include <boost/algorithm/string/trim.hpp>
#include <boost/lexical_cast.hpp>

namespace kth {

template <typename Value>
Value deserialize(std::string const& text, bool trim) {
    if (trim) {
        return boost::lexical_cast<Value>(boost::trim_copy(text));
    }
    return boost::lexical_cast<Value>(text);
}

template <typename Value>
void deserialize(Value& value, std::string const& text, bool trim) {
    if (trim) {
        value = boost::lexical_cast<Value>(boost::trim_copy(text));
    } else {
        value = boost::lexical_cast<Value>(text);
    }
}

template <typename Value>
void deserialize(std::vector<Value>& collection, std::string const& text, bool trim) {
    // This had problems with the inclusion of the ideographic (CJK) space
    // (0xe3,0x80, 0x80). Need to infuse the local in kth::split().
    auto const tokens = split(text, " \n\r\t");
    for (auto const& token: tokens) {
        Value value;
        deserialize(value, token, true);
        collection.push_back(value);
    }
}

template <typename Value>
std::string serialize(const Value& value, std::string const& fallback) {
    std::stringstream stream;
    stream << value;
    auto const& text = stream.str();
    return text.empty() ? fallback : text;
}

} // namespace kth

#endif
