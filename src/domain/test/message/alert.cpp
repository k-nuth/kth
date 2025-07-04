// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: alert tests

TEST_CASE("alert  constructor 1  always invalid", "[alert]") {
    message::alert instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("alert  constructor 2  always  equals params", "[alert]") {
    data_chunk const payload = to_chunk(base16_literal("0123456789abcdef"));
    data_chunk const signature = to_chunk(base16_literal("fedcba9876543210"));

    message::alert instance(payload, signature);

    REQUIRE(instance.is_valid());
    REQUIRE(payload == instance.payload());
    REQUIRE(signature == instance.signature());
}

TEST_CASE("alert  constructor 3  always  equals params", "[alert]") {
    auto const payload = to_chunk(base16_literal("0123456789abcdef"));
    auto const signature = to_chunk(base16_literal("fedcba9876543210"));
    auto dup_payload = payload;
    auto dup_signature = signature;

    message::alert instance(std::move(dup_payload), std::move(dup_signature));

    REQUIRE(instance.is_valid());
    REQUIRE(payload == instance.payload());
    REQUIRE(signature == instance.signature());
}

TEST_CASE("alert  constructor 4  always  equals params", "[alert]") {
    data_chunk const payload = to_chunk(base16_literal("0123456789abcdef"));
    data_chunk const signature = to_chunk(base16_literal("fedcba9876543210"));

    message::alert value(payload, signature);
    message::alert instance(value);

    REQUIRE(instance.is_valid());
    REQUIRE(value == instance);
    REQUIRE(payload == instance.payload());
    REQUIRE(signature == instance.signature());
}

TEST_CASE("alert  constructor 5  always  equals params", "[alert]") {
    data_chunk const payload = to_chunk(base16_literal("0123456789abcdef"));
    data_chunk const signature = to_chunk(base16_literal("fedcba9876543210"));

    message::alert value(payload, signature);
    message::alert instance(std::move(value));

    REQUIRE(instance.is_valid());
    REQUIRE(payload == instance.payload());
    REQUIRE(signature == instance.signature());
}

TEST_CASE("alert from data insufficient bytes  failure", "[alert]") {
    data_chunk const raw{0xab, 0x11};
    message::alert instance;

    byte_reader reader(raw);
    auto result = message::alert::from_data(reader, message::version::level::minimum);
    REQUIRE( ! result);
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("alert from data wiki sample  success", "[alert]") {
    data_chunk const raw_payload{
        0x01, 0x00, 0x00, 0x00, 0x37, 0x66, 0x40, 0x4f, 0x00,
        0x00, 0x00, 0x00, 0xb3, 0x05, 0x43, 0x4f, 0x00, 0x00, 0x00,
        0x00, 0xf2, 0x03, 0x00, 0x00, 0xf1, 0x03, 0x00, 0x00, 0x00,
        0x10, 0x27, 0x00, 0x00, 0x48, 0xee, 0x00, 0x00, 0x00, 0x64,
        0x00, 0x00, 0x00, 0x00, 0x46, 0x53, 0x65, 0x65, 0x20, 0x62,
        0x69, 0x74, 0x63, 0x6f, 0x69, 0x6e, 0x2e, 0x6f, 0x72, 0x67,
        0x2f, 0x66, 0x65, 0x62, 0x32, 0x30, 0x20, 0x69, 0x66, 0x20,
        0x79, 0x6f, 0x75, 0x20, 0x68, 0x61, 0x76, 0x65, 0x20, 0x74,
        0x72, 0x6f, 0x75, 0x62, 0x6c, 0x65, 0x20, 0x63, 0x6f, 0x6e,
        0x6e, 0x65, 0x63, 0x74, 0x69, 0x6e, 0x67, 0x20, 0x61, 0x66,
        0x74, 0x65, 0x72, 0x20, 0x32, 0x30, 0x20, 0x46, 0x65, 0x62,
        0x72, 0x75, 0x61, 0x72, 0x79, 0x00};

    data_chunk const raw_signature{
        0x30, 0x45, 0x02, 0x21, 0x00, 0x83, 0x89, 0xdf, 0x45, 0xf0,
        0x70, 0x3f, 0x39, 0xec, 0x8c, 0x1c, 0xc4, 0x2c, 0x13, 0x81,
        0x0f, 0xfc, 0xae, 0x14, 0x99, 0x5b, 0xb6, 0x48, 0x34, 0x02,
        0x19, 0xe3, 0x53, 0xb6, 0x3b, 0x53, 0xeb, 0x02, 0x20, 0x09,
        0xec, 0x65, 0xe1, 0xc1, 0xaa, 0xee, 0xc1, 0xfd, 0x33, 0x4c,
        0x6b, 0x68, 0x4b, 0xde, 0x2b, 0x3f, 0x57, 0x30, 0x60, 0xd5,
        0xb7, 0x0c, 0x3a, 0x46, 0x72, 0x33, 0x26, 0xe4, 0xe8, 0xa4,
        0xf1};

    data_chunk const raw{
        0x73, 0x01, 0x00, 0x00, 0x00, 0x37, 0x66, 0x40, 0x4f, 0x00,
        0x00, 0x00, 0x00, 0xb3, 0x05, 0x43, 0x4f, 0x00, 0x00, 0x00,
        0x00, 0xf2, 0x03, 0x00, 0x00, 0xf1, 0x03, 0x00, 0x00, 0x00,
        0x10, 0x27, 0x00, 0x00, 0x48, 0xee, 0x00, 0x00, 0x00, 0x64,
        0x00, 0x00, 0x00, 0x00, 0x46, 0x53, 0x65, 0x65, 0x20, 0x62,
        0x69, 0x74, 0x63, 0x6f, 0x69, 0x6e, 0x2e, 0x6f, 0x72, 0x67,
        0x2f, 0x66, 0x65, 0x62, 0x32, 0x30, 0x20, 0x69, 0x66, 0x20,
        0x79, 0x6f, 0x75, 0x20, 0x68, 0x61, 0x76, 0x65, 0x20, 0x74,
        0x72, 0x6f, 0x75, 0x62, 0x6c, 0x65, 0x20, 0x63, 0x6f, 0x6e,
        0x6e, 0x65, 0x63, 0x74, 0x69, 0x6e, 0x67, 0x20, 0x61, 0x66,
        0x74, 0x65, 0x72, 0x20, 0x32, 0x30, 0x20, 0x46, 0x65, 0x62,
        0x72, 0x75, 0x61, 0x72, 0x79, 0x00, 0x47, 0x30, 0x45, 0x02,
        0x21, 0x00, 0x83, 0x89, 0xdf, 0x45, 0xf0, 0x70, 0x3f, 0x39,
        0xec, 0x8c, 0x1c, 0xc4, 0x2c, 0x13, 0x81, 0x0f, 0xfc, 0xae,
        0x14, 0x99, 0x5b, 0xb6, 0x48, 0x34, 0x02, 0x19, 0xe3, 0x53,
        0xb6, 0x3b, 0x53, 0xeb, 0x02, 0x20, 0x09, 0xec, 0x65, 0xe1,
        0xc1, 0xaa, 0xee, 0xc1, 0xfd, 0x33, 0x4c, 0x6b, 0x68, 0x4b,
        0xde, 0x2b, 0x3f, 0x57, 0x30, 0x60, 0xd5, 0xb7, 0x0c, 0x3a,
        0x46, 0x72, 0x33, 0x26, 0xe4, 0xe8, 0xa4, 0xf1};

    message::alert const expected {raw_payload, raw_signature};
    byte_reader reader(raw);
    auto const result_exp = message::alert::from_data(reader, message::version::level::minimum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(result.is_valid());
    REQUIRE(raw.size() == result.serialized_size(message::version::level::minimum));
    REQUIRE(result == expected);

    auto const data = expected.to_data(message::version::level::minimum);

    REQUIRE(raw.size() == data.size());
    REQUIRE(data.size() == expected.serialized_size(message::version::level::minimum));
}

TEST_CASE("alert from data roundtrip  success", "[alert]") {
    const message::alert expected{
        {0x07, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07},
        {0x04, 0xff, 0xab, 0xcd, 0xee}};

    auto const data = expected.to_data(message::version::level::minimum);
    byte_reader reader(data);
    auto const result_exp = message::alert::from_data(reader, message::version::level::minimum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(message::version::level::minimum));
    REQUIRE(expected.serialized_size(message::version::level::minimum) == result.serialized_size(message::version::level::minimum));
}



TEST_CASE("alert  payload accessor 1  always  returns initialized", "[alert]") {
    data_chunk const payload = to_chunk(base16_literal("0123456789abcdef"));
    data_chunk const signature = to_chunk(base16_literal("fedcba9876543210"));

    message::alert instance(payload, signature);
    REQUIRE(payload == instance.payload());
}

TEST_CASE("alert  payload accessor 2  always  returns initialized", "[alert]") {
    data_chunk const payload = to_chunk(base16_literal("0123456789abcdef"));
    data_chunk const signature = to_chunk(base16_literal("fedcba9876543210"));

    const message::alert instance(payload, signature);
    REQUIRE(payload == instance.payload());
}

TEST_CASE("alert  payload setter 1  roundtrip  success", "[alert]") {
    auto const value = to_chunk(base16_literal("aabbccddeeff"));
    message::alert instance;
    REQUIRE(instance.payload() != value);
    instance.set_payload(value);
    REQUIRE(value == instance.payload());
}

TEST_CASE("alert  payload setter 2  roundtrip  success", "[alert]") {
    auto const value = to_chunk(base16_literal("aabbccddeeff"));
    auto dup_value = value;
    message::alert instance;
    REQUIRE(instance.payload() != value);
    instance.set_payload(std::move(dup_value));
    REQUIRE(value == instance.payload());
}

TEST_CASE("alert  signature accessor 1  always  returns initialized", "[alert]") {
    data_chunk const payload = to_chunk(base16_literal("0123456789abcdef"));
    data_chunk const signature = to_chunk(base16_literal("fedcba9876543210"));

    message::alert instance(payload, signature);
    REQUIRE(signature == instance.signature());
}

TEST_CASE("alert  signature accessor 2  always  returns initialized", "[alert]") {
    data_chunk const payload = to_chunk(base16_literal("0123456789abcdef"));
    data_chunk const signature = to_chunk(base16_literal("fedcba9876543210"));

    const message::alert instance(payload, signature);
    REQUIRE(signature == instance.signature());
}

TEST_CASE("alert  signature setter 1  roundtrip  success", "[alert]") {
    auto const value = to_chunk(base16_literal("aabbccddeeff"));
    message::alert instance;
    REQUIRE(instance.signature() != value);
    instance.set_signature(value);
    REQUIRE(value == instance.signature());
}

TEST_CASE("alert  signature setter 2  roundtrip  success", "[alert]") {
    auto const value = to_chunk(base16_literal("aabbccddeeff"));
    auto dup_value = value;
    message::alert instance;
    REQUIRE(instance.signature() != value);
    instance.set_signature(std::move(dup_value));
    REQUIRE(value == instance.signature());
}

TEST_CASE("alert  operator assign equals  always  matches equivalent", "[alert]") {
    data_chunk const payload = to_chunk(base16_literal("0123456789abcdef"));
    data_chunk const signature = to_chunk(base16_literal("fedcba9876543210"));

    message::alert value(payload, signature);

    REQUIRE(value.is_valid());

    message::alert instance;
    REQUIRE( ! instance.is_valid());

    instance = std::move(value);
    REQUIRE(instance.is_valid());
    REQUIRE(payload == instance.payload());
    REQUIRE(signature == instance.signature());
}

TEST_CASE("alert  operator boolean equals  duplicates  returns true", "[alert]") {
    const message::alert expected(
        to_chunk(base16_literal("0123456789abcdef")),
        to_chunk(base16_literal("fedcba9876543210")));

    message::alert instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("alert  operator boolean equals  differs  returns false", "[alert]") {
    const message::alert expected(
        to_chunk(base16_literal("0123456789abcdef")),
        to_chunk(base16_literal("fedcba9876543210")));

    message::alert instance;
    REQUIRE(instance != expected);
}

TEST_CASE("alert  operator boolean not equals  duplicates  returns false", "[alert]") {
    const message::alert expected(
        to_chunk(base16_literal("0123456789abcdef")),
        to_chunk(base16_literal("fedcba9876543210")));

    message::alert instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("alert  operator boolean not equals  differs  returns true", "[alert]") {
    const message::alert expected(
        to_chunk(base16_literal("0123456789abcdef")),
        to_chunk(base16_literal("fedcba9876543210")));

    message::alert instance;
    REQUIRE(instance != expected);
}

// End Test Suite
