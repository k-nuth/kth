// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <test_helpers.hpp>
#include <kth/infrastructure/unicode/unicode.hpp>
#include "../../src/unicode/utf.hpp"
#include <kth/infrastructure/unicode/unicode_streambuf.hpp>
#include <sstream>

using namespace kth;

TEST_CASE("strict UTF conversions preserve scalars and embedded nulls", "[utf]") {
    std::string const text{"A\0á日本😀", 14};
    auto const wide = to_utf16(text);
    REQUIRE(wide);
    REQUIRE(to_utf8(*wide) == text);
    auto const u16 = detail::utf_from_utf8<char16_t>(text);
    auto const u32 = detail::utf_from_utf8<char32_t>(text);
    REQUIRE(u16);
    REQUIRE(u32);
    REQUIRE(detail::utf_to_utf8(*u16) == text);
    REQUIRE(detail::utf_to_utf8(*u32) == text);
    REQUIRE(to_utf8(std::wstring{}) == "");
    REQUIRE(to_utf16("") == L"");
    REQUIRE(detail::utf_from_utf8<char16_t>("😀") == u"😀");
    REQUIRE(detail::utf_from_utf8<char32_t>("😀") == U"😀");
}

TEST_CASE("strict UTF conversions return public errors for malformed sequences", "[utf]") {
    for (auto const text : {std::string{"\x80"}, std::string{"\xc0\xaf"}, std::string{"\xe2\x82"},
                           std::string{"\xed\xa0\x80"}, std::string{"\xf4\x90\x80\x80"}}) {
        REQUIRE(to_utf16(text) == std::unexpected(unicode_error::invalid_encoding));
        REQUIRE(detail::utf_from_utf8<char16_t>(text) == std::unexpected(unicode_error::invalid_encoding));
        REQUIRE(detail::utf_from_utf8<char32_t>(text) == std::unexpected(unicode_error::invalid_encoding));
    }
    for (auto const& text : {std::u16string{0xd800}, std::u16string{0xdc00}, std::u16string{0xd800, 'A'}})
        REQUIRE(detail::utf_to_utf8(text) == std::unexpected(unicode_error::invalid_encoding));
    for (auto const& text : {std::u32string{0xd800}, std::u32string{0x110000}})
        REQUIRE(detail::utf_to_utf8(text) == std::unexpected(unicode_error::invalid_encoding));
}

TEST_CASE("UTF buffer conversions report invalid input and insufficient space", "[utf]") {
    char out[8]{};
    wchar_t wide[8]{};
    uint8_t truncated = 0;
    REQUIRE(to_utf8(out, 1, L"日本", 2) == std::unexpected(unicode_error::buffer_too_small));
    REQUIRE(to_utf16(wide, 0, "a", 1, truncated) == std::unexpected(unicode_error::buffer_too_small));
    REQUIRE(to_utf16(wide, 8, "\xc0\xaf", 2, truncated) == std::unexpected(unicode_error::invalid_encoding));
    REQUIRE(to_utf8(out, 8, nullptr, 1) == std::unexpected(unicode_error::invalid_argument));
    REQUIRE(to_utf8(-1, nullptr) == std::unexpected(unicode_error::invalid_argument));
    REQUIRE(to_utf8(nullptr, 0, nullptr, 0) == size_t{0});
    REQUIRE(to_utf16(nullptr, 0, nullptr, 0, truncated) == size_t{0});
}

#ifdef WITH_ICU
TEST_CASE("direct ICU normalization preserves canonical wallet forms", "[utf]") {
    REQUIRE(to_normal_nfc_form("e\xcc\x81") == "é");
    REQUIRE(to_normal_nfkd_form("é") == "e\xcc\x81");
    REQUIRE(to_normal_nfkd_form("　") == " ");
    REQUIRE(to_normal_nfc_form("") == "");
    REQUIRE(to_normal_nfkd_form("") == "");
    std::string const nul{"e\xcc\x81\0x", 5};
    REQUIRE(to_normal_nfc_form(nul) == std::string{"é\0x", 4});
    REQUIRE(to_normal_nfc_form("\xc0\xaf") == std::unexpected(unicode_error::invalid_encoding));
}
#endif

TEST_CASE("Unicode stream flushing reports conversion errors", "[utf]") {
    std::wstringbuf target;
    unicode_streambuf buffer(&target, 8);
    REQUIRE(buffer.sputn("a", 1) == 1);
    REQUIRE(buffer.pubsync() == 0);
    REQUIRE(target.str() == L"a");
    REQUIRE(buffer.sputn("\xc0\xaf", 2) == 2);
    REQUIRE(buffer.pubsync() == -1);
    REQUIRE(target.str() == L"a");
}
