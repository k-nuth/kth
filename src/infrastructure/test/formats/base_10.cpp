// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/infrastructure/formats/base_10.hpp>
#include <limits>
#include <string>

using namespace kth;

TEST_CASE("decimal formatting preserves exact units", "[infrastructure][base10]") {
    CHECK(encode_base10(0, btc_decimal_places) == "0");
    CHECK(encode_base10(1, btc_decimal_places) == "0.00000001");
    CHECK(encode_base10(10, btc_decimal_places) == "0.0000001");
    CHECK(encode_base10(100000000, btc_decimal_places) == "1");
    CHECK(encode_base10(150000000, btc_decimal_places) == "1.5");
    CHECK(encode_base10(5000000219, btc_decimal_places) == "50.00000219");
    CHECK(encode_base10(2100000000000000, btc_decimal_places) == "21000000");
    CHECK(encode_base10(std::numeric_limits<uint64_t>::max(), btc_decimal_places) == "184467440737.09551615");
    CHECK(encode_base10(std::numeric_limits<uint64_t>::max(), 0) == "18446744073709551615");
    CHECK(encode_base10(1000, 0) == "1000");
    CHECK(encode_base10(101, 2) == "1.01");
    CHECK(encode_base10(1, 255) == "0." + std::string(254, '0') + "1");
    CHECK(satoshi_to_btc(150000000) == "1.5");
}

TEST_CASE("decimal parsing preserves precision and rounding", "[infrastructure][base10]") {
    uint64_t out = 42;
    REQUIRE(decode_base10(out, "", 0)); CHECK(out == 0);
    REQUIRE(decode_base10(out, ".", btc_decimal_places)); CHECK(out == 0);
    REQUIRE(decode_base10(out, ".5", btc_decimal_places)); CHECK(out == 50000000);
    REQUIRE(decode_base10(out, "0001.23000000", btc_decimal_places)); CHECK(out == 123000000);
    REQUIRE(decode_base10(out, "18446744073709551615", 0)); CHECK(out == std::numeric_limits<uint64_t>::max());
    REQUIRE(decode_base10(out, "184467440737.09551615", btc_decimal_places)); CHECK(out == std::numeric_limits<uint64_t>::max());
    REQUIRE(decode_base10(out, "0.000000001", btc_decimal_places, false)); CHECK(out == 1);
    REQUIRE(decode_base10(out, "0.999999999", btc_decimal_places, false)); CHECK(out == 100000000);
    REQUIRE(decode_base10(out, "18446744073709551615.0", 0)); CHECK(out == std::numeric_limits<uint64_t>::max());
    REQUIRE(btc_to_satoshi(out, "1.5")); CHECK(out == 150000000);
}

TEST_CASE("decimal parsing rejects invalid and overflowing amounts without changing output", "[infrastructure][base10]") {
    for (auto const* input : {"-1", "+1", " 1", "1 ", "1.2.3", "1e2", "0x10", "18446744073709551616"}) {
        CAPTURE(input); uint64_t out = 42;
        CHECK_FALSE(decode_base10(out, input, 0)); CHECK(out == 42);
    }
    for (auto const* input : {"0.000000001", "184467440737.09551616"}) {
        CAPTURE(input); uint64_t out = 42;
        CHECK_FALSE(decode_base10(out, input, btc_decimal_places)); CHECK(out == 42);
    }
    uint64_t out = 42;
    CHECK_FALSE(decode_base10(out, "18446744073709551615.1", 0, false)); CHECK(out == 42);
}
