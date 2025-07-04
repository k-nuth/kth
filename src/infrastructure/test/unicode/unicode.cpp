// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstring>
#include <stdexcept>
#include <vector>
#include <test_helpers.hpp>
#include <kth/infrastructure.hpp>

using namespace kth;

// Start Test Suite: unicode tests

#ifdef WITH_ICU

// github.com/bitcoin/bips/blob/master/bip-0038.mediawiki
TEST_CASE("infrastructure unicode to normal nfc form validation", "[infrastructure][unicode]") {
    data_chunk original;
    REQUIRE(decode_base16(original, "cf92cc8100f0909080f09f92a9"));
    std::string original_string(original.begin(), original.end());

    data_chunk normal;
    REQUIRE(decode_base16(normal, "cf9300f0909080f09f92a9"));
    std::string expected_normal_string(normal.begin(), normal.end());

    auto const derived_normal_string = kth::to_normal_nfc_form(original_string);
    REQUIRE(expected_normal_string == derived_normal_string);
}

TEST_CASE("infrastructure unicode to normal nfkd form validation", "[infrastructure][unicode]") {
    auto const ascii_space_sandwich = "space-> <-space";
    auto const ideographic_space_sandwich = "space->　<-space";
    auto const normalized = to_normal_nfkd_form(ideographic_space_sandwich);
    REQUIRE(normalized.c_str() == ascii_space_sandwich);
}

#endif

// Use of L is not recommended as it will only work for ascii.
TEST_CASE("infrastructure unicode to utf8 string ascii conversion", "[infrastructure][unicode]") {
    auto const utf8_ascii = "ascii";
    auto const utf16_ascii = L"ascii";
    auto const converted = to_utf8(utf16_ascii);
    REQUIRE(converted == utf8_ascii);
}

//TODO(fernando): fix this test
// // Use of L is not recommended as it will only work for ascii.
// TEST_CASE("unicode to utf16 string ascii test", "[unicode tests]") {
//     auto const utf8_ascii = "ascii";
//     auto const utf16_ascii = L"ascii";
//     auto const converted = to_utf16(utf8_ascii);
//     REQUIRE(converted.c_str() == utf16_ascii);
// }

TEST_CASE("unicode string round trip ascii test", "[unicode tests]") {
    auto const utf8_ascii = "ascii";
    auto const narrowed = to_utf8(to_utf16(utf8_ascii));
    REQUIRE(narrowed == utf8_ascii);
}

TEST_CASE("unicode string round trip utf8 test", "[unicode tests]") {
    auto const utf8 = "テスト";
    auto const narrowed = to_utf8(to_utf16(utf8));
    REQUIRE(narrowed == utf8);
}

//TODO(fernando): fix this test
// TEST_CASE("unicode string round trip wide literal test", "[unicode tests]") {
//     auto const utf8 = "テスト";
//     auto const utf16 = L"テスト";

//     auto const widened = to_utf16(utf8);
//     auto const narrowed = to_utf8(utf16);

// #ifdef _MSC_VER
//     // This confirms that the L prefix does not work with non-ascii text when
//     // the source file does not have a BOM (which we avoid for other reasons).
//     BOOST_REQUIRE_NE(widened.c_str(), utf16);
//     BOOST_REQUIRE_NE(narrowed, utf8);
// #else
//     REQUIRE(widened.c_str() == utf16);
//     REQUIRE(narrowed == utf8);
// #endif
// }

TEST_CASE("unicode  to utf8 array  ascii  test", "[unicode tests]") {
    char utf8[20];

    // Text buffer provides null termination for test comparison.
    memset(utf8, 0, sizeof(utf8) / sizeof(char));

    // Use of L is not recommended as it will only work for ascii.
    const std::wstring utf16(L"ascii");
    std::string const expected_utf8("ascii");

    auto const size = to_utf8(utf8, sizeof(utf8), utf16.c_str(), (int)utf16.size());
    REQUIRE(utf8 == expected_utf8);
    REQUIRE(size == expected_utf8.size());
}

TEST_CASE("unicode  to utf8 array  non ascii  test", "[unicode tests]") {
    char utf8[36];

    // Text buffer provides null termination for test comparison.
    memset(utf8, 0, sizeof(utf8) / sizeof(char));

    std::string const expected_utf8("テスト");
    auto const utf16 = to_utf16(expected_utf8);

    auto const size = to_utf8(utf8, sizeof(utf8), utf16.c_str(), (int)utf16.size());

    REQUIRE(utf8 == expected_utf8);
    REQUIRE(size == expected_utf8.size());
}

//TODO(fernando): fix this test
// TEST_CASE("unicode to utf16 array ascii test", "[unicode tests]") {
//     wchar_t utf16[20];

//     // Text buffer provides null termination for test comparison.
//     wmemset(utf16, 0, sizeof(utf16) / sizeof(wchar_t));

//     // Use of L is not recommended as it will only work for ascii.
//     const std::wstring expected_utf16(L"ascii");
//     std::string const utf8("ascii");

//     uint8_t truncated;
//     auto const size = to_utf16(utf16, sizeof(utf16), utf8.c_str(), (int)utf8.size(), truncated);

//     REQUIRE(utf16 == expected_utf16.c_str());
//     REQUIRE(size == expected_utf16.size());
//     REQUIRE(truncated == 0);
// }

// TEST_CASE("unicode  to utf16 array  non ascii  test", "[unicode tests]") {
//     wchar_t utf16[36];

//     // Text buffer provides null termination for test comparison.
//     wmemset(utf16, 0, sizeof(utf16) / sizeof(wchar_t));

//     std::string const utf8("テスト");
//     auto const expected_utf16 = to_utf16(utf8);

//     uint8_t truncated;
//     auto const size = to_utf16(utf16, sizeof(utf16), utf8.c_str(), (int)utf8.size(), truncated);

//     REQUIRE(utf16 == expected_utf16.c_str());
//     REQUIRE(size == expected_utf16.size());
//     REQUIRE(truncated == 0);
// }

// TEST_CASE("unicode  to utf16 array  non ascii truncation1  test", "[unicode tests]") {
//     wchar_t utf16[36];

//     // Text buffer provides null termination for test comparison.
//     wmemset(utf16, 0, sizeof(utf16) / sizeof(wchar_t));

//     std::string utf8("テスト");
//     auto expected_utf16 = to_utf16(utf8);

//     // Lop off last byte, which will split the last character.
//     auto const drop_bytes = 1;
//     utf8.resize(utf8.size() - drop_bytes);

//     // Expect the loss of the last wide character.
//     expected_utf16.resize(expected_utf16.size() - 1);

//     // Expect the truncation of the remaining bytes of the last character.
//     auto const expected_truncated = strlen("ト") - 1;

//     uint8_t truncated;
//     auto const size = to_utf16(utf16, sizeof(utf16), utf8.c_str(), (int)utf8.size(), truncated);

//     REQUIRE(truncated == expected_truncated);
//     REQUIRE(utf16 == expected_utf16.c_str());
//     REQUIRE(size == expected_utf16.size());
// }

// TEST_CASE("unicode  to utf16 array  non ascii truncation2  test", "[unicode tests]") {
//     wchar_t utf16[36];

//     // Text buffer provides null termination for test comparison.
//     wmemset(utf16, 0, sizeof(utf16) / sizeof(wchar_t));

//     std::string utf8("テスト");
//     auto expected_utf16 = to_utf16(utf8);

//     // Lop off last two bytes, which will split the last character.
//     auto const drop_bytes = 2;
//     utf8.resize(utf8.size() - drop_bytes);

//     // Expect the loss of the last wide character.
//     expected_utf16.resize(expected_utf16.size() - 1);

//     // Expect the truncation of the remaining bytes of the last character.
//     auto const expected_truncated = strlen("ト") - drop_bytes;

//     uint8_t truncated;
//     auto const size = to_utf16(utf16, sizeof(utf16), utf8.c_str(), (int)utf8.size(), truncated);

//     REQUIRE(truncated == expected_truncated);
//     REQUIRE(utf16 == expected_utf16.c_str());
//     REQUIRE(size == expected_utf16.size());
// }

// TEST_CASE("unicode  to utf8 environment  ascii  test", "[unicode tests]") {
//     std::vector<const wchar_t*> wide_environment = { L"ascii", nullptr };

//     auto variables = const_cast<wchar_t**>(&wide_environment[0]);
//     auto buffer = to_utf8(variables);
//     auto narrow_environment = reinterpret_cast<char**>(&buffer[0]);

//     REQUIRE(narrow_environment[0] == "ascii");
// }

// TEST_CASE("unicode  to utf8 environment  utf16  test", "[unicode tests]") {
//     // We cannot use L for literal encoding of non-ascii text on Windows.
//     auto utf16 = to_utf16("テスト");
//     auto non_literal_utf16 = utf16.c_str();
//     std::vector<const wchar_t*> wide_environment = { L"ascii", non_literal_utf16, nullptr };

//     auto variables = const_cast<wchar_t**>(&wide_environment[0]);
//     auto buffer = to_utf8(variables);
//     auto narrow_environment = reinterpret_cast<char**>(&buffer[0]);

//     REQUIRE(narrow_environment[0] == "ascii");
//     REQUIRE(narrow_environment[1] == "テスト");
// }

TEST_CASE("unicode  to utf8 environment  null termination  test", "[unicode tests]") {
    std::vector<const wchar_t*> wide_environment = { L"ascii", nullptr };

    auto variables = const_cast<wchar_t**>(&wide_environment[0]);
    auto expected_count = static_cast<int>(wide_environment.size()) - 1;

    auto environment = to_utf8(variables);
    auto narrow_environment = reinterpret_cast<char**>(&environment[0]);

    // Each argument is a null terminated string.
    auto const length = strlen(narrow_environment[0]);
    auto variable_terminator = narrow_environment[0][length];
    REQUIRE(variable_terminator == '\0');

    // The argument vector is a null terminated array.
    auto environment_terminator = narrow_environment[expected_count];
    REQUIRE(environment_terminator == static_cast<char*>(nullptr));
}

// TEST_CASE("unicode  to utf8 main  ascii  test", "[unicode tests]") {
//     std::vector<const wchar_t*> wide_args = { L"ascii", nullptr };

//     auto argv = const_cast<wchar_t**>(&wide_args[0]);
//     auto argc = static_cast<int>(wide_args.size()) - 1;

//     auto buffer = to_utf8(argc, argv);
//     auto narrow_args = reinterpret_cast<char**>(&buffer[0]);

//     REQUIRE(narrow_args[0] == "ascii");
// }

// TEST_CASE("unicode  to utf8 main  utf16  test", "[unicode tests]") {
//     // We cannot use L for literal encoding of non-ascii text on Windows.
//     auto utf16 = to_utf16("テスト");
//     auto non_literal_utf16 = utf16.c_str();
//     std::vector<const wchar_t*> wide_args = { L"ascii", non_literal_utf16, nullptr };

//     auto argv = const_cast<wchar_t**>(&wide_args[0]);
//     auto argc = static_cast<int>(wide_args.size()) - 1;

//     auto buffer = to_utf8(argc, argv);
//     auto narrow_args = reinterpret_cast<char**>(&buffer[0]);

//     REQUIRE(narrow_args[0] == "ascii");
//     REQUIRE(narrow_args[1] == "テスト");
// }

TEST_CASE("unicode  to utf8 main  null termination  test", "[unicode tests]") {
    std::vector<const wchar_t*> wide_args = { L"ascii", nullptr };

    auto argv = const_cast<wchar_t**>(&wide_args[0]);
    auto argc = static_cast<int>(wide_args.size()) - 1;

    auto buffer = to_utf8(argc, argv);
    auto narrow_args = reinterpret_cast<char**>(&buffer[0]);

    // Each argument is a null terminated string.
    auto const length = strlen(narrow_args[0]);
    auto arg_terminator = narrow_args[0][length];
    REQUIRE(arg_terminator == '\0');

    // The argument vector is a null terminated array.
    auto argv_terminator = narrow_args[argc];
    REQUIRE(argv_terminator == static_cast<char*>(nullptr));
}

// End Test Suite
