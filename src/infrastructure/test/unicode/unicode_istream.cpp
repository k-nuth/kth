// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string>
#include <sstream>
#include <test_helpers.hpp>
#include <kth/infrastructure.hpp>

using namespace kth;

// Start Test Suite: unicode istream tests

// Use of L is not recommended as it will only work for ascii.
TEST_CASE("unicode istream conditional test", "[unicode istream tests]") {
    std::wstringstream wide_stream(L"windows ...");
    std::stringstream narrow_stream("linux ...");

    unicode_istream input(narrow_stream, wide_stream, 256);
    std::string result;
    input >> result;

#ifdef _MSC_VER
    REQUIRE(result == "windows");
#else
    REQUIRE(result == "linux");
#endif
}

TEST_CASE("unicode istream non ascii test", "[unicode istream tests]") {
    auto const utf8 = "テスト";
    auto const utf16 = to_utf16(utf8);

    std::wstringstream wide_stream(utf16);
    std::stringstream narrow_stream(utf8);

    unicode_istream input(narrow_stream, wide_stream, 256);
    std::string result;
    input >> result;

    REQUIRE(result == utf8);
}

TEST_CASE("unicode istream tokenization test", "[unicode istream tests]") {
    auto const utf8 = "テスト\rス\nト\tテス スト";
    auto const utf16 = to_utf16(utf8);

    std::wstringstream wide_stream(utf16);
    std::stringstream narrow_stream(utf8);

    unicode_istream input(narrow_stream, wide_stream, 256);
    std::string result;

    input >> result;
    REQUIRE(result == "テスト");
    input >> result;
    REQUIRE(result == "ス");
    input >> result;
    REQUIRE(result == "ト");
    input >> result;
    REQUIRE(result == "テス");
    input >> result;
    REQUIRE(result == "スト");
}

TEST_CASE("unicode istream overflow test", "[unicode istream tests]") {
    // This is a 20x10 matrix of 3 bytes per character triples (1800 bytes).
    // The buffer is 256 (wide) and 1024 (narrow), resulting in a potential
    // character split because 256 is not a multiple of 3. However sgetn()
    // doesn't split on non-character boundaries as it reads, so this works.
    auto const utf8_1800_bytes =
        "テストテストテストテストテストテストテストテストテストテスト"
        "テストテストテストテストテストテストテストテストテストテスト"
        "テストテストテストテストテストテストテストテストテストテスト"
        "テストテストテストテストテストテストテストテストテストテスト"
        "テストテストテストテストテストテストテストテストテストテスト"
        "テストテストテストテストテストテストテストテストテストテスト"
        "テストテストテストテストテストテストテストテストテストテスト"
        "テストテストテストテストテストテストテストテストテストテスト"
        "テストテストテストテストテストテストテストテストテストテスト"
        "テストテストテストテストテストテストテストテストテストテスト"
        "テストテストテストテストテストテストテストテストテストテスト"
        "テストテストテストテストテストテストテストテストテストテスト"
        "テストテストテストテストテストテストテストテストテストテスト"
        "テストテストテストテストテストテストテストテストテストテスト"
        "テストテストテストテストテストテストテストテストテストテスト"
        "テストテストテストテストテストテストテストテストテストテスト"
        "テストテストテストテストテストテストテストテストテストテスト"
        "テストテストテストテストテストテストテストテストテストテスト"
        "テストテストテストテストテストテストテストテストテストテスト"
        "テストテストテストテストテストテストテストテストテストテスト";

    auto const utf16_600_chars = to_utf16(utf8_1800_bytes);

    std::wstringstream wide_stream(utf16_600_chars);
    std::stringstream narrow_stream(utf8_1800_bytes);

    unicode_istream input(narrow_stream, wide_stream, 256);
    std::string result;
    input >> result;

    REQUIRE(result == utf8_1800_bytes);
}

// End Test Suite
