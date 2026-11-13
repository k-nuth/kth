// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/string.hpp>

#include <locale>

namespace kth {

std::string_view trim_view(std::string_view text) {
    std::locale const locale;
    while (!text.empty() && std::isspace(text.front(), locale)) text.remove_prefix(1);
    while (!text.empty() && std::isspace(text.back(), locale)) text.remove_suffix(1);
    return text;
}

std::string join(string_list const& words, std::string const& delimiter) {
    std::string result;
    bool first = true;
    for (auto const& word : words) {
        if (!first) result.append(delimiter);
        first = false;
        result.append(word);
    }
    return result;
}

// Delimiters are individual characters. Compress delimiter runs while preserving
// empty edge tokens, including one empty token for an empty input.
string_list split(std::string_view sentence, std::string_view delimiter, bool trim) {
    if (trim) sentence = trim_view(sentence);
    string_list words;
    size_t start = 0;
    while (true) {
        auto const end = sentence.find_first_of(delimiter, start);
        words.emplace_back(sentence.substr(start, end == std::string_view::npos ? end : end - start));
        if (end == std::string_view::npos) break;
        start = sentence.find_first_not_of(delimiter, end);
        if (start == std::string_view::npos) {
            words.emplace_back();
            break;
        }
    }
    return words;
}

} // namespace kth
