// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string>
#include <sstream>
#include <test_helpers.hpp>
#include <kth/infrastructure.hpp>

using namespace kth;

// Start Test Suite: unicode ostream tests

// Use of L is not recommended as it will only work for ascii.
TEST_CASE("unicode ostream conditional test", "[unicode ostream tests]") {
    std::wstringstream wide_stream;
    std::stringstream narrow_stream;

    unicode_ostream output(narrow_stream, wide_stream, 256);
    output << "ascii";
    output.flush();

#ifdef _MSC_VER
    REQUIRE(narrow_stream.str().empty());
    REQUIRE(wide_stream.str().c_str() == L"ascii");
#else
    REQUIRE(wide_stream.str().empty());
    REQUIRE(narrow_stream.str() == "ascii");
#endif
}

TEST_CASE("unicode ostream non ascii test", "[unicode ostream tests]") {
    auto const utf8 = "テスト";
    auto const utf16 = to_utf16(utf8);

    std::wstringstream wide_stream;
    std::stringstream narrow_stream;

    unicode_ostream output(narrow_stream, wide_stream, 256);
    output << utf8;
    output.flush();

#ifdef _MSC_VER
    REQUIRE(narrow_stream.str().empty());
    REQUIRE(wide_stream.str().c_str() == utf16.c_str());
#else
    REQUIRE(wide_stream.str().empty());
    REQUIRE(narrow_stream.str() == utf8);
#endif
}

TEST_CASE("unicode ostream overflow test", "[unicode ostream tests]") {
    // This is a 20x10 matrix of 3 bytes per character triples (1800 bytes).
    // The buffer is 256 (wide) and 1024 (narrow), resulting in a potential
    // character split because 256 is not a multiple of 3. The overflow
    // method of the output stream buffer must compensate for character
    // splitting as the utf8 stream is not character-oriented, otherwise
    // this will fail.
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

    std::wstringstream wide_stream;
    std::stringstream narrow_stream;

    unicode_ostream output(narrow_stream, wide_stream, 256);
    output << utf8_1800_bytes;
    output.flush();

#ifdef _MSC_VER
    REQUIRE(narrow_stream.str().empty());
    REQUIRE(wide_stream.str().c_str() == utf16_600_chars.c_str());
#else
    REQUIRE(wide_stream.str().empty());
    REQUIRE(narrow_stream.str() == utf8_1800_bytes);
#endif
}

// End Test Suite
