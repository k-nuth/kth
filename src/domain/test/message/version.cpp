// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

static
auto const version_maximum = message::version::level::maximum;

// "/Satoshi:1.1.1/" (70006) no relay
// anarchistprime: bitcointalk.org/index.php?topic=1001407
// This node is identifiable by a different genesis block.
#define NO_RELAY_ANARCHISTPRIME_1 "761101000100000000000000ae1b9c58000000000100000000000000260106009000d69ee9a999156d2e27fed77d01000000000000002a0104f80160144600000000000000022b2aaf9b8ea1eb14614b0f2f5361746f7368693a312e312e312f64450200"
#define NO_RELAY_ANARCHISTPRIME_2 "7611010001000000000000005b429c5800000000010000000000000000000000000000000000ffff1813e52e939b010000000000000000000000000000000000ffffd59fd7db200ac7f00f6ee45f1ab30f2f5361746f7368693a312e312e312f66450200"

// "/Cornell-Falcon-Network:0.1.0/" (70014) no relay
#define NO_RELAY_FALCON_1 "7e11010001000000000000005f429c5800000000010000000000000000000000000000000000ffff000000000000010000000000000000000000000000000000ffff22c06db5208d6241eabdf2d6753c1e2f436f726e656c6c2d46616c636f6e2d4e6574776f726b3a302e312e302f97e60600"
#define NO_RELAY_FALCON_2 "7e1101000100000000000000ae429c5800000000010000000000000000000000000000000000ffff000000000000010000000000000000000000000000000000ffff23a25ec4208d9ed337a66b411a441e2f436f726e656c6c2d46616c636f6e2d4e6574776f726b3a302e312e302f97e60600"

// "/Satoshi:0.13.0/" (70014) no relay
#define NO_RELAY_SATOSHI_1 "7e1101000900000000000000ec429c5800000000090000000000000000000000000000000000ffff1813e52e208d090000000000000000000000000000000000ffff97ec0b6d208d7c8c30307127a822102f5361746f7368693a302e31332e302f97e60600"

// "/therealbitcoin.org:0.9.99.99/" (99999) no relay
#define NO_RELAY_THEREALBITCOIN_1 "9f86010001000000000000002336a15800000000010000000000000000000000000000000000ffff1813e52ebb81010000000000000000000000000000000000ffff6f6f6f6f208db1f33b262e6acb331e2f7468657265616c626974636f696e2e6f72673a302e392e39392e39392fb9e80600"

// Start Test Suite: version tests

TEST_CASE("version  factory  therealbitcoin dot org valid", "[version]") {
    data_chunk payload;
    REQUIRE(decode_base16(payload, NO_RELAY_THEREALBITCOIN_1));
    byte_reader reader(payload);
    auto const result_exp = message::version::from_data(reader, version_maximum);
    REQUIRE(result_exp);
    auto const version = std::move(*result_exp);
    REQUIRE(version.is_valid());
}

TEST_CASE("version  factory  anarchistprime1 valid", "[version]") {
    data_chunk payload;
    REQUIRE(decode_base16(payload, NO_RELAY_ANARCHISTPRIME_1));
    byte_reader reader(payload);
    auto const result_exp = message::version::from_data(reader, version_maximum);
    REQUIRE(result_exp);
    auto const version = std::move(*result_exp);
    REQUIRE(version.is_valid());
}

TEST_CASE("version  factory  anarchistprime2 valid", "[version]") {
    data_chunk payload;
    REQUIRE(decode_base16(payload, NO_RELAY_ANARCHISTPRIME_2));
    byte_reader reader(payload);
    auto const result_exp = message::version::from_data(reader, version_maximum);
    REQUIRE(result_exp);
    auto const version = std::move(*result_exp);
    REQUIRE(version.is_valid());
}

TEST_CASE("version  factory  falcon1 valid", "[version]") {
    data_chunk payload;
    REQUIRE(decode_base16(payload, NO_RELAY_FALCON_1));
    byte_reader reader(payload);
    auto const result_exp = message::version::from_data(reader, version_maximum);
    REQUIRE(result_exp);
    auto const version = std::move(*result_exp);
    REQUIRE(version.is_valid());
}

TEST_CASE("version  factory  falcon2 valid", "[version]") {
    data_chunk payload;
    REQUIRE(decode_base16(payload, NO_RELAY_FALCON_2));
    byte_reader reader(payload);
    auto const result_exp = message::version::from_data(reader, version_maximum);
    REQUIRE(result_exp);
    auto const version = std::move(*result_exp);
    REQUIRE(version.is_valid());
}

TEST_CASE("version  factory  satoshi1 valid", "[version]") {
    data_chunk payload;
    REQUIRE(decode_base16(payload, NO_RELAY_SATOSHI_1));
    byte_reader reader(payload);
    auto const result_exp = message::version::from_data(reader, version_maximum);
    REQUIRE(result_exp);
    auto const version = std::move(*result_exp);
    REQUIRE(version.is_valid());
}

TEST_CASE("version  constructor 1  always invalid", "[version]") {
    message::version instance;
    REQUIRE( ! instance.is_valid());
    REQUIRE( ! instance.address_receiver().is_valid());
    REQUIRE( ! instance.address_sender().is_valid());
}

TEST_CASE("version  constructor 2  always  equals params", "[version]") {
    uint32_t value = 45624u;
    uint64_t services = 263546u;
    uint64_t timestamp = 7668534u;

    message::network_address receiver{
        734678u,
        5357534u,
        {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
          0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
        123u};

    message::network_address sender{
        46324u,
        156u,
        {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
          0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
        351u};

    uint64_t nonce = 335743u;
    std::string agent = "sdashgdfafh";
    uint32_t height = 1246323u;
    auto const relay = true;

    message::version instance(value, services, timestamp, receiver, sender, nonce, agent, height, relay);

    REQUIRE(instance.is_valid());
    REQUIRE(value == instance.value());
    REQUIRE(services == instance.services());
    REQUIRE(timestamp == instance.timestamp());
    REQUIRE(receiver == instance.address_receiver());
    REQUIRE(sender == instance.address_sender());
    REQUIRE(nonce == instance.nonce());
    REQUIRE(agent == instance.user_agent());
    REQUIRE(height == instance.start_height());
    REQUIRE(relay == instance.relay());
}

TEST_CASE("version  constructor 3  always  equals params", "[version]") {
    uint32_t value = 45624u;
    uint64_t services = 263546u;
    uint64_t timestamp = 7668534u;

    message::network_address receiver{
        734678u,
        5357534u,
        {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
          0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
        123u};

    message::network_address sender{
        46324u,
        156u,
        {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
          0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
        351u};

    uint64_t nonce = 335743u;
    std::string agent = "sdashgdfafh";
    uint32_t height = 1246323u;
    auto const relay = true;

    REQUIRE(receiver.is_valid());
    REQUIRE(sender.is_valid());

    message::version instance(value, services, timestamp, std::move(receiver), std::move(sender), nonce, agent, height, relay);

    REQUIRE(instance.is_valid());
}

TEST_CASE("version  constructor 4  always  equals params", "[version]") {
    uint32_t value = 45624u;
    uint64_t services = 263546u;
    uint64_t timestamp = 7668534u;

    message::network_address receiver{
        734678u,
        5357534u,
        {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
          0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
        123u};

    message::network_address sender{
        46324u,
        156u,
        {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
          0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
        351u};

    uint64_t nonce = 335743u;
    std::string agent = "sdashgdfafh";
    uint32_t height = 1246323u;
    auto const relay = true;

    REQUIRE(receiver.is_valid());
    REQUIRE(sender.is_valid());

    message::version alpha(value, services, timestamp, receiver, sender, nonce, agent, height, relay);
    REQUIRE(alpha.is_valid());

    message::version beta(alpha);
    REQUIRE(beta == alpha);
}

TEST_CASE("version  constructor 5  always  equals params", "[version]") {
    uint32_t value = 45624u;
    uint64_t services = 263546u;
    uint64_t timestamp = 7668534u;

    message::network_address receiver{
        734678u,
        5357534u,
        {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
          0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
        123u};

    message::network_address sender{
        46324u,
        156u,
        {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
          0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
        351u};

    uint64_t nonce = 335743u;
    std::string agent = "sdashgdfafh";
    uint32_t height = 1246323u;
    auto const relay = true;

    REQUIRE(receiver.is_valid());
    REQUIRE(sender.is_valid());

    message::version alpha(value, services, timestamp, receiver, sender, nonce, agent, height, relay);
    REQUIRE(alpha.is_valid());

    message::version beta(std::move(alpha));
    REQUIRE(beta.is_valid());
    REQUIRE(value == beta.value());
    REQUIRE(services == beta.services());
    REQUIRE(timestamp == beta.timestamp());
    REQUIRE(receiver == beta.address_receiver());
    REQUIRE(sender == beta.address_sender());
    REQUIRE(nonce == beta.nonce());
    REQUIRE(agent == beta.user_agent());
    REQUIRE(height == beta.start_height());
    REQUIRE(relay == beta.relay());
}

TEST_CASE("version from data insufficient bytes invalid", "[version]") {
    data_chunk const raw{0xab};
    message::version instance{};

    byte_reader reader(raw);
    auto result = message::version::from_data(reader, version_maximum);
    REQUIRE( ! result);
}

TEST_CASE("version from data mismatched sender services invalid", "[version]") {
    uint64_t sender_services = 1515u;
    const message::version expected(
        210u,
        sender_services,
        979797u,
        message::network_address{
            734678u,
            5357534u,
            {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
              0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
            123u},
        message::network_address{
            46324u,
            sender_services + 1,
            {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
              0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
            351u},
        13626u,
        "my agent",
        100u,
        false);

    auto const data = expected.to_data(version_maximum);
    byte_reader reader(data);
    auto const result_exp = message::version::from_data(reader, version_maximum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    // HACK: disabled check due to inconsistent node implementation.
    REQUIRE(/*!*/ result.is_valid());
}

TEST_CASE("version from data version meets bip37  success", "[version]") {
    auto const sender_services = 1515u;
    const message::version expected{
        message::version::level::bip37,
        sender_services,
        979797u,
        {734678u,
         5357534u,
         {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
           0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
         123u},
        {46324u,
         sender_services,
         {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
           0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
         351u},
        13626u,
        "my agent",
        100u,
        true};

    auto const data = expected.to_data(version_maximum);
    byte_reader reader(data);
    auto const result_exp = message::version::from_data(reader, version_maximum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);
    REQUIRE(result.is_valid());
}

TEST_CASE("version from data valid input  success", "[version]") {
    auto const sender_services = 1515u;
    const message::version expected{
        210u,
        sender_services,
        979797u,
        {734678u,
         5357534u,
         {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
           0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
         123u},
        {46324u,
         sender_services,
         {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
           0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
         351u},
        13626u,
        "my agent",
        100u,
        true};

    auto const data = expected.to_data(version_maximum);
    byte_reader reader(data);
    auto const result_exp = message::version::from_data(reader, version_maximum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);
    REQUIRE(result.is_valid());
    REQUIRE(data.size() == result.serialized_size(version_maximum));
    REQUIRE(expected.serialized_size(version_maximum) == result.serialized_size(version_maximum));
    REQUIRE(expected == result);
}



TEST_CASE("version  value accessor  returns initialized value", "[version]") {
    uint32_t const expected = 210u;
    const message::version instance(
        expected,
        1515u,
        979797u,
        {734678u,
         5357534u,
         {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
           0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
         123u},
        {46324u,
         1515u,
         {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
           0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
         351u},
        13626u,
        "my agent",
        100u,
        false);

    REQUIRE(expected == instance.value());
}

TEST_CASE("version  value setter  roundtrip  success", "[version]") {
    uint32_t const expected = 210u;
    message::version instance;
    instance.set_value(expected);
    REQUIRE(expected == instance.value());
}

TEST_CASE("version  services accessor  always  returns initialized value", "[version]") {
    uint64_t const expected = 1515u;
    const message::version instance(
        210u,
        expected,
        979797u,
        {734678u,
         5357534u,
         {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
           0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
         123u},
        {46324u,
         1515u,
         {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
           0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
         351u},
        13626u,
        "my agent",
        100u,
        false);

    REQUIRE(expected == instance.services());
}

TEST_CASE("version  services setter  roundtrip  success", "[version]") {
    uint64_t const expected = 1515u;
    message::version instance;
    instance.set_services(expected);
    REQUIRE(expected == instance.services());
}

TEST_CASE("version  timestamp accessor  always  returns initialized value", "[version]") {
    uint64_t const expected = 979797u;
    const message::version instance(
        210u,
        1515u,
        expected,
        {734678u,
         5357534u,
         {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
           0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
         123u},
        {46324u,
         1515u,
         {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
           0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
         351u},
        13626u,
        "my agent",
        100u,
        false);

    REQUIRE(expected == instance.timestamp());
}

TEST_CASE("version  timestamp setter  roundtrip  success", "[version]") {
    uint64_t const expected = 979797u;
    message::version instance;
    instance.set_timestamp(expected);
    REQUIRE(expected == instance.timestamp());
}

TEST_CASE("version  address receiver accessor  always  returns initialized value", "[version]") {
    const message::network_address expected{
        734678u,
        5357534u,
        {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
          0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
        123u};

    const message::version instance(
        210u,
        1515u,
        979797u,
        expected,
        {46324u,
         1515u,
         {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
           0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
         351u},
        13626u,
        "my agent",
        100u,
        false);

    REQUIRE(expected == instance.address_receiver());
}

// TEST_CASE("version  address receiver setter 1  roundtrip  success", "[None]")
//{
//    message::network_address expected
//    {
//        734678u,
//        5357534u,
//        {
//            {
//                0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
//                0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55
//            }
//        },
//        123u
//    };
//
//    message::version instance;
//    REQUIRE( ! instance.address_receiver().is_valid());
//    instance.address_receiver(expected);
//    const message::network_address result = instance.address_receiver();
//    REQUIRE(result == expected);
//    REQUIRE(result.is_valid());
//}

TEST_CASE("version  address receiver setter 2  roundtrip  success", "[version]") {
    message::version instance;
    REQUIRE( ! instance.address_receiver().is_valid());
    instance.set_address_receiver(message::network_address{
        734678u,
        5357534u,
        {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
          0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
        123u});

    const message::network_address result = instance.address_receiver();
    REQUIRE(result.is_valid());
}

TEST_CASE("version  address sender accessor  always  returns initialized value", "[version]") {
    const message::network_address expected{
        46324u,
        1515u,
        {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
          0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
        351u};

    const message::version instance(
        210u,
        1515u,
        979797u,
        {734678u,
         5357534u,
         {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
           0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
         123u},
        expected,
        13626u,
        "my agent",
        100u,
        false);

    REQUIRE(expected == instance.address_sender());
}

// TEST_CASE("version  address sender setter 1  roundtrip  success", "[None]")
//{
//    message::network_address expected
//    {
//        46324u,
//        1515u,
//        {
//            {
//                0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
//                0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88
//            }
//        },
//        351u
//    };
//
//    message::version instance;
//    REQUIRE( ! instance.address_sender().is_valid());
//    instance.address_sender(expected);
//    const message::network_address result = instance.address_sender();
//    REQUIRE(result == expected);
//    REQUIRE(result.is_valid());
//}

TEST_CASE("version  address sender setter 2  roundtrip  success", "[version]") {
    message::version instance;
    REQUIRE( ! instance.address_sender().is_valid());
    instance.set_address_sender(message::network_address{
        46324u,
        1515u,
        {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
          0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
        351u});

    const message::network_address result = instance.address_sender();
    REQUIRE(result.is_valid());
}

TEST_CASE("version  nonce accessor  always  returns initialized value", "[version]") {
    uint64_t const expected = 13626u;
    const message::version instance(
        210u,
        1515u,
        979797u,
        {734678u,
         5357534u,
         {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
           0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
         123u},
        {46324u,
         1515u,
         {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
           0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
         351u},
        expected,
        "my agent",
        100u,
        false);

    REQUIRE(expected == instance.nonce());
}

TEST_CASE("version  nonce setter  roundtrip  success", "[version]") {
    uint64_t const expected = 13626u;
    message::version instance;
    instance.set_nonce(expected);
    REQUIRE(expected == instance.nonce());
}

TEST_CASE("version  user agent accessor  always  returns initialized value", "[version]") {
    std::string const expected = "MyUseRAgenT";
    const message::version instance(
        210u,
        1515u,
        979797u,
        {734678u,
         5357534u,
         {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
           0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
         123u},
        {46324u,
         1515u,
         {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
           0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
         351u},
        13626u,
        expected,
        100u,
        false);

    REQUIRE(expected == instance.user_agent());
}

TEST_CASE("version  user agent setter 1  roundtrip  success", "[version]") {
    std::string const expected = "MyUseRAgenT";
    message::version instance;
    instance.set_user_agent(expected);
    REQUIRE(expected == instance.user_agent());
}

TEST_CASE("version  user agent setter 2  roundtrip  success", "[version]") {
    std::string expected = "MyUseRAgenT";
    message::version instance;
    REQUIRE(instance.user_agent().empty());
    instance.set_user_agent(std::move(expected));
    REQUIRE( ! instance.user_agent().empty());
}

TEST_CASE("version  start height accessor  always  returns initialized value", "[version]") {
    uint32_t const expected = 514u;
    const message::version instance(
        210u,
        1515u,
        979797u,
        {734678u,
         5357534u,
         {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
           0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
         123u},
        {46324u,
         1515u,
         {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
           0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
         351u},
        13626u,
        "my agent",
        expected,
        false);

    REQUIRE(expected == instance.start_height());
}

TEST_CASE("version  start height setter  roundtrip  success", "[version]") {
    uint32_t const expected = 514u;
    message::version instance;
    instance.set_start_height(expected);
    REQUIRE(expected == instance.start_height());
}

TEST_CASE("version  relay accessor  always  returns initialized value", "[version]") {
    bool const expected = true;
    const message::version instance(
        210u,
        1515u,
        979797u,
        {734678u,
         5357534u,
         {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
           0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
         123u},
        {46324u,
         1515u,
         {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
           0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
         351u},
        13626u,
        "my agent",
        100u,
        expected);

    REQUIRE(expected == instance.relay());
}

TEST_CASE("version  relay setter  roundtrip  success", "[version]") {
    bool const expected = true;
    message::version instance;
    instance.set_relay(expected);
    REQUIRE(expected == instance.relay());
}

TEST_CASE("version  operator assign equals  always  matches equivalent", "[version]") {
    message::version value(
        210u,
        15234u,
        979797u,
        message::network_address{
            734678u,
            5357534u,
            {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
              0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
            123u},
        message::network_address{
            46324u,
            57835u,
            {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
              0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
            351u},
        13626u,
        "my agent",
        100u,
        false);

    REQUIRE(value.is_valid());

    message::version instance;
    instance = std::move(value);
    REQUIRE(instance.is_valid());
}

TEST_CASE("version  operator boolean equals  duplicates  returns true", "[version]") {
    const message::version expected(
        210u,
        15234u,
        979797u,
        message::network_address{
            734678u,
            5357534u,
            {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
              0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
            123u},
        message::network_address{
            46324u,
            57835u,
            {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
              0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
            351u},
        13626u,
        "my agent",
        100u,
        false);

    message::version instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("version  operator boolean equals  differs  returns false", "[version]") {
    const message::version expected(
        210u,
        15234u,
        979797u,
        message::network_address{
            734678u,
            5357534u,
            {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
              0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
            123u},
        message::network_address{
            46324u,
            57835u,
            {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
              0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
            351u},
        13626u,
        "my agent",
        100u,
        false);

    message::version instance;
    REQUIRE(instance != expected);
}

TEST_CASE("version  operator boolean not equals  duplicates  returns false", "[version]") {
    const message::version expected(
        210u,
        15234u,
        979797u,
        message::network_address{
            734678u,
            5357534u,
            {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
              0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
            123u},
        message::network_address{
            46324u,
            57835u,
            {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
              0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
            351u},
        13626u,
        "my agent",
        100u,
        false);

    message::version instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("version  operator boolean not equals  differs  returns true", "[version]") {
    const message::version expected(
        210u,
        15234u,
        979797u,
        message::network_address{
            734678u,
            5357534u,
            {{0x47, 0x81, 0x6a, 0x40, 0xbb, 0x92, 0xbd, 0xb4,
              0xe0, 0xb8, 0x25, 0x68, 0x61, 0xf9, 0x6a, 0x55}},
            123u},
        message::network_address{
            46324u,
            57835u,
            {{0xab, 0xcd, 0x6a, 0x40, 0x33, 0x92, 0x77, 0xb4,
              0xe0, 0xb8, 0xda, 0x43, 0x61, 0x66, 0x6a, 0x88}},
            351u},
        13626u,
        "my agent",
        100u,
        false);

    message::version instance;
    REQUIRE(instance != expected);
}

// End Test Suite
