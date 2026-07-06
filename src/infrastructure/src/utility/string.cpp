// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/string.hpp>

#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>

namespace kth {

std::string join(string_list const& words, std::string const& delimiter) {
    return boost::join(words, delimiter);
}

// Note that use of token_compress_on may cause unexpected results when
// working with CSV-style lists that accept empty elements.
string_list split(std::string_view sentence, std::string_view delimiter, bool trim) {
    string_list words;
    auto const compress = boost::token_compress_on;
    auto const delimit = boost::is_any_of(delimiter);

    // boost::algorithm operates on ranges; a `string_view` is a valid
    // range so we can hand it straight to `boost::split`. The trim
    // path needs an owning string because `boost::trim_copy` returns
    // one — but that allocation lived here before the switch to
    // `string_view` too. Callers no longer pay for a `std::string`
    // materialisation at the call site.
    if (trim) {
        auto const trimmed = boost::trim_copy(std::string{sentence});
        boost::split(words, trimmed, delimit, compress);
    } else {
        boost::split(words, sentence, delimit, compress);
    }

    return words;
}

} // namespace kth
