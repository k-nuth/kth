// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/infrastructure/formats/bitcoin.hpp>
#include <limits>
#include <string>

using namespace kth;

TEST_CASE("decimal formatting preserves exact units", "[infrastructure][base10]") {
    CHECK(encode_base10(0, bitcoin_decimal_places) == "0");
    CHECK(encode_base10(1, bitcoin_decimal_places) == "0.00000001");
    CHECK(encode_base10(10, bitcoin_decimal_places) == "0.0000001");
    CHECK(encode_base10(100000000, bitcoin_decimal_places) == "1");
    CHECK(encode_base10(150000000, bitcoin_decimal_places) == "1.5");
    CHECK(encode_base10(5000000219, bitcoin_decimal_places) == "50.00000219");
    CHECK(encode_base10(2100000000000000, bitcoin_decimal_places) == "21000000");
    CHECK(encode_base10(std::numeric_limits<uint64_t>::max(), bitcoin_decimal_places) == "184467440737.09551615");
    CHECK(encode_base10(std::numeric_limits<uint64_t>::max(), 0) == "18446744073709551615");
    CHECK(encode_base10(1000, 0) == "1000");
    CHECK(encode_base10(101, 2) == "1.01");
    CHECK(encode_base10(1, 255) == "0." + std::string(254, '0') + "1");
    CHECK(satoshi_to_bitcoin(150000000) == "1.5");
}

static_assert(decode_base10("0001.23000000", 8).value() == 123000000);
static_assert(decode_base10("184467440737.09551615", 8).value() == std::numeric_limits<uint64_t>::max());
static_assert(decode_base10("0.000000001", 8).error() == base10_errc::excess_precision);
static_assert(decode_base10("18446744073709551616").error() == base10_errc::overflow);
static_assert(decode_base10("12x").error() == base10_errc::invalid_format);
static_assert(encode_base10(150000000, 8) == "1.5");
static_assert(satoshi_to_bitcoin(1) == "0.00000001");
static_assert("1.5"_bitcoin == 150000000);
static_assert(1.5_bitcoin == 150000000);
static_assert(1'000_bitcoin == 100000000000);
static_assert("0.00000001"_bitcoin == 1_satoshi);
static_assert(1_millibitcoin == 100000);
static_assert("1"_microbitcoin == 100);
static_assert("18446744073709551615"_satoshi == std::numeric_limits<uint64_t>::max());

TEST_CASE("decimal parsing preserves precision and rounding", "[infrastructure][base10]") {
    CHECK(decode_base10("", 0) == 0);
    CHECK(decode_base10(".", bitcoin_decimal_places) == 0);
    CHECK(decode_base10(".5", bitcoin_decimal_places) == 50000000);
    CHECK(decode_base10("0001.23000000", bitcoin_decimal_places) == 123000000);
    CHECK(decode_base10("18446744073709551615", 0) == std::numeric_limits<uint64_t>::max());
    CHECK(decode_base10("184467440737.09551615", bitcoin_decimal_places) == std::numeric_limits<uint64_t>::max());
    CHECK(decode_base10("0.000000001", bitcoin_decimal_places, false) == 1);
    CHECK(decode_base10("0.999999999", bitcoin_decimal_places, false) == 100000000);
    CHECK(decode_base10("18446744073709551615.0", 0) == std::numeric_limits<uint64_t>::max());
    CHECK(bitcoin_to_satoshi("1.5") == 150000000);
    CHECK(decode_base10(std::string_view("x1.25y").substr(1, 4), 2) == 125);
    CHECK(decode_base10("0", 255) == 0);
    CHECK(decode_base10("0." + std::string(254, '0') + "1", 255) == 1);
    CHECK(decode_base10("1", 255).error() == base10_errc::overflow);
}

TEST_CASE("decimal parsing discriminates invalid precision and overflow", "[infrastructure][base10]") {
    for (auto const *input : {"-1", "+1", " 1", "1 ", "1.2.3", "1e2", "0x10"}) {
        CAPTURE(input);
        auto const result = decode_base10(input);
        REQUIRE_FALSE(result);
        CHECK(result.error() == base10_errc::invalid_format);
    }
    for (auto const *input : {"18446744073709551616", "18446744073709551615.1"}) {
        CAPTURE(input);
        auto const result = decode_base10(input, 0, false);
        REQUIRE_FALSE(result);
        CHECK(result.error() == base10_errc::overflow);
    }
    CHECK(decode_base10("0.000000001", 8).error() == base10_errc::excess_precision);
    CHECK(decode_base10("184467440737.09551616", 8).error() == base10_errc::overflow);
    CHECK(decode_base10(std::string_view("1\0", 2)).error() == base10_errc::invalid_format);
}

static_assert("0"_base10 == 0);
static_assert("00042"_base10 == 42);
static_assert("18446744073709551615"_base10 == UINT64_MAX);
static_assert(noexcept("42"_base10));
static_assert(noexcept(decode_base10(std::string_view{})));
static_assert(!noexcept(encode_base10(0)));

TEST_CASE("base10 checked literal agrees with runtime decoding", "[base10]") {
    CHECK(decode_base10("00042") == "00042"_base10);
    CHECK(decode_base10("18446744073709551615") == "18446744073709551615"_base10);
}
