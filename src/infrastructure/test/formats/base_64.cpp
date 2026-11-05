// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/infrastructure.hpp>

using namespace kth;

// Start Test Suite: base 64 tests

#define BASE64_MURRAY "TXVycmF5IFJvdGhiYXJk"
#define BASE64_DATA_MURRAY {0x4d, 0x75, 0x72, 0x72, 0x61, 0x79, 0x20, 0x52, 0x6f, 0x74, 0x68, 0x62, 0x61, 0x72, 0x64}

#define BASE64_BOOK "TWFuLCBFY29ub215IGFuZCBTdGF0ZQ=="
#define BASE64_DATA_BOOK                                                                                               \
    {0x4d, 0x61, 0x6e, 0x2c, 0x20, 0x45, 0x63, 0x6f, 0x6e, 0x6f, 0x6d, 0x79, 0x20, 0x61, 0x6e, 0x64, 0x20, 0x53, 0x74, \
        0x61, 0x74, 0x65}

TEST_CASE("infrastructure base64 encode empty data", "[infrastructure][base64]") {
    data_chunk decoded;
    REQUIRE(encode_base64(decoded) == "");
}

TEST_CASE("infrastructure base64 decode empty string", "[infrastructure][base64]") {
    auto const result = decode_base64("");
    REQUIRE(result);
    REQUIRE(*result == data_chunk());
}

TEST_CASE("infrastructure base64 encode unpadded data", "[infrastructure][base64]") {
    data_chunk decoded(BASE64_DATA_MURRAY);
    REQUIRE(encode_base64(decoded) == BASE64_MURRAY);
}

TEST_CASE("infrastructure base64 decode unpadded data", "[infrastructure][base64]") {
    auto const result = decode_base64(BASE64_MURRAY);
    REQUIRE(result);
    REQUIRE(*result == data_chunk(BASE64_DATA_MURRAY));
}

TEST_CASE("infrastructure base64 encode padded data", "[infrastructure][base64]") {
    data_chunk decoded(BASE64_DATA_BOOK);
    REQUIRE(encode_base64(decoded) == BASE64_BOOK);
}

TEST_CASE("infrastructure base64 decode padded data", "[infrastructure][base64]") {
    auto const result = decode_base64(BASE64_BOOK);
    REQUIRE(result);
    REQUIRE(*result == data_chunk(BASE64_DATA_BOOK));
}

TEST_CASE("infrastructure base64 decode invalid characters", "[infrastructure][base64]") {
    REQUIRE(!decode_base64("!@#$%^&*()"));
}

// End Test Suite

constexpr auto base64_example = "Zm9v"_base64;
static_assert(base64_example == std::array<uint8_t, 3>{'f', 'o', 'o'});
static_assert(encode_base64(base64_example) == "Zm9v");
static_assert(decode_base64(" Zg== ")->front() == 'f');
static_assert(decode_base64("Z").error() == base64_errc::invalid_length);

TEST_CASE("base64 runtime and constexpr scalar agree on tails and padding", "[infrastructure][base64]") {
    for (auto const *input : {"", "Zg", "Zg==", "Zh==", "Zm8", "Zm8=", " Zm9v\n", "Zg=", "Z", "====", "AA=A", "AA==A",
             "Zm9v=", "Zg==\v", "Zg==\f"}) {
        CAPTURE(input);
        auto const scalar = detail::decode_base64_scalar(input);
        auto const native = decode_base64(input);
        CHECK(bool(native) == bool(scalar));
        if (native && scalar)
            CHECK(*native == *scalar);
    }
}
TEST_CASE("base64 decoding honors exact output spans", "[infrastructure][base64]") {
    std::array<uint8_t, 3> bytes{};
    CHECK(decode_base64("Zm9v", bytes) == 3);
    CHECK(bytes == base64_example);
    CHECK(decode_base64("Zm9v", std::span(bytes).first(2)).error() == base64_errc::output_too_small);
    CHECK(decode_base64("", std::span(bytes).first(0)) == 0);
    for (size_t size = 0; size < 512; ++size) {
        data_chunk value(size);
        for (size_t i = 0; i < size; ++i)
            value[i] = uint8_t(i * 37);
        auto const encoded = encode_base64(value);
        CHECK(encoded == detail::encode_base64_scalar(value));
        auto const decoded = decode_base64(encoded);
        REQUIRE(decoded);
        CHECK(*decoded == value);
        auto const scalar = detail::decode_base64_scalar(encoded);
        REQUIRE(scalar);
        CHECK(*scalar == value);
    }
}

namespace {

constexpr bool base64_fixed_size_control() {
    if (decode_base64<3>("Zm9v") != byte_array<3>{102, 111, 111})
        return false;
    if (decode_base64<0>("") != byte_array<0>{})
        return false;
    if (decode_base64<1>("Zg==") != byte_array<1>{102})
        return false;
    if (decode_base64<2>("Zg==").error() != base64_errc::size_mismatch)
        return false;
    if (decode_base64<0>("Zg==").error() != base64_errc::size_mismatch)
        return false;
    return decode_base64<1>("!").error() == base64_errc::invalid_character;
}

static_assert(base64_fixed_size_control());
static_assert(noexcept("Zg=="_base64));
static_assert(noexcept(decode_base64<1>(std::string_view{})));
static_assert(noexcept(decode_base64(std::string_view{}, std::span<uint8_t>{})));
static_assert(!noexcept(encode_base64(byte_span{})));
static_assert(!noexcept(decode_base64(std::string_view{})));

} // namespace

TEST_CASE("base64 fixed size decode agrees in compile time and runtime", "[base64]") {
    CHECK(base64_fixed_size_control());
    CHECK(decode_base64<3>(" Zm9v\n") == "Zm9v"_base64);
    CHECK(decode_base64<1>("Zg") == "Zg=="_base64);
    CHECK(decode_base64<1>("Zg=").error() == base64_errc::invalid_padding);
}
