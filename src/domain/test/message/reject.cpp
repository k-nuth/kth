// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string>

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// /Satoshi:0.12.1/
// Invalid reject payload from [46.101.110.115:8333] bad data stream
// This contradicts docs in that it is tx with readable text vs. hash.
// tx : nonstandard : too-long-mempool-chain : <empty>
#define MALFORMED_REJECT "0274784016746f6f2d6c6f6e672d6d656d706f6f6c2d636861696e"

static std::string const reason_text = "My Reason...";
static auto const version_maximum = message::version::level::maximum;

static hash_digest const data{
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
     0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
     0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
     0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F}};

// Start Test Suite: reject tests

TEST_CASE("reject  factory from data  tx nonstandard empty data valid", "[reject]") {
    data_chunk payload;
    REQUIRE(decode_base16(payload, MALFORMED_REJECT));
    byte_reader reader(payload);
    auto const result_exp = message::reject::from_data(reader, version_maximum);
    REQUIRE(result_exp);
    auto const reject = std::move(*result_exp);
    REQUIRE(reject.is_valid());
}

TEST_CASE("reject  constructor 1  always invalid", "[reject]") {
    message::reject instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("reject  constructor 2  always  equals params", "[reject]") {
    auto code = message::reject::reason_code::nonstandard;
    std::string message = "Alpha Beta";
    std::string reason = "Gamma Delta";
    hash_digest data = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    message::reject instance(code, message, reason, data);
    REQUIRE(instance.is_valid());
    REQUIRE(code == instance.code());
    REQUIRE(message == instance.message());
    REQUIRE(reason == instance.reason());
    REQUIRE(data == instance.data());
}

TEST_CASE("reject  constructor 3  always  equals params", "[reject]") {
    auto code = message::reject::reason_code::nonstandard;
    std::string message = "sadfasdgd";
    std::string reason = "jgfghkggfsr";
    hash_digest data = hash_literal("ce8f4b713ffdd2658900845251890f30371856be201cd1f5b3d970f793634333");
    message::reject instance(code, std::move(message), std::move(reason), std::move(data));
    REQUIRE(instance.is_valid());
}

TEST_CASE("reject  constructor 4  always  equals params", "[reject]") {
    auto code = message::reject::reason_code::nonstandard;
    std::string message = "Alpha Beta";
    std::string reason = "Gamma Delta";
    hash_digest data = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    message::reject expected(code, message, reason, data);
    message::reject instance(expected);
    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
    REQUIRE(code == instance.code());
    REQUIRE(message == instance.message());
    REQUIRE(reason == instance.reason());
    REQUIRE(data == instance.data());
}

TEST_CASE("reject  constructor 5  always  equals params", "[reject]") {
    auto code = message::reject::reason_code::nonstandard;
    std::string message = "Alpha Beta";
    std::string reason = "Gamma Delta";
    hash_digest data = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    message::reject expected(code, message, reason, data);
    message::reject instance(std::move(expected));
    REQUIRE(instance.is_valid());
    REQUIRE(code == instance.code());
    REQUIRE(message == instance.message());
    REQUIRE(reason == instance.reason());
    REQUIRE(data == instance.data());
}

TEST_CASE("reject from data insufficient bytes  failure", "[reject]") {
    static data_chunk const raw{0xab};
    message::reject instance{};
    byte_reader reader(raw);
    auto result = message::reject::from_data(reader, version_maximum);
    REQUIRE( ! result);
}

TEST_CASE("reject from data insufficient version  failure", "[reject]") {
    const message::reject expected(
        message::reject::reason_code::dust,
        message::block::command,
        reason_text,
        data);

    data_chunk const raw = expected.to_data(version_maximum);
    message::reject instance{};
    byte_reader reader(raw);
    auto result = message::reject::from_data(reader, message::reject::version_minimum - 1);
    REQUIRE( ! result);
}

TEST_CASE("reject from data code malformed  success", "[reject]") {
    const message::reject expected(
        message::reject::reason_code::malformed,
        message::block::command,
        reason_text,
        data);

    data_chunk const raw = expected.to_data(version_maximum);
    message::reject instance{};
    byte_reader reader(raw);
    auto result = message::reject::from_data(reader, message::reject::version_minimum);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(expected == instance);
}

TEST_CASE("reject from data code invalid  success", "[reject]") {
    const message::reject expected(
        message::reject::reason_code::invalid,
        message::block::command,
        reason_text,
        data);

    data_chunk const raw = expected.to_data(version_maximum);
    message::reject instance{};

    byte_reader reader(raw);
    auto result = message::reject::from_data(reader, message::reject::version_minimum);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(expected == instance);
}

TEST_CASE("reject from data code obsolete  success", "[reject]") {
    const message::reject expected(
        message::reject::reason_code::obsolete,
        message::block::command,
        reason_text,
        data);

    data_chunk const raw = expected.to_data(version_maximum);
    message::reject instance{};
    byte_reader reader(raw);
    auto result = message::reject::from_data(reader, message::reject::version_minimum);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(expected == instance);
}

TEST_CASE("reject from data code duplicate  success", "[reject]") {
    const message::reject expected(
        message::reject::reason_code::duplicate,
        message::block::command,
        reason_text,
        data);

    data_chunk const raw = expected.to_data(version_maximum);
    message::reject instance{};
    byte_reader reader(raw);
    auto result = message::reject::from_data(reader, message::reject::version_minimum);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(expected == instance);
}

TEST_CASE("reject from data code nonstandard  success", "[reject]") {
    const message::reject expected(
        message::reject::reason_code::nonstandard,
        message::block::command,
        reason_text,
        data);

    data_chunk const raw = expected.to_data(version_maximum);
    message::reject instance{};
    byte_reader reader(raw);
    auto result = message::reject::from_data(reader, message::reject::version_minimum);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(expected == instance);
}

TEST_CASE("reject from data code dust  success", "[reject]") {
    const message::reject expected(
        message::reject::reason_code::dust,
        message::block::command,
        reason_text,
        data);

    data_chunk const raw = expected.to_data(version_maximum);
    message::reject instance{};
    byte_reader reader(raw);
    auto result = message::reject::from_data(reader, message::reject::version_minimum);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(expected == instance);
}

TEST_CASE("reject from data code insufficient fee  success", "[reject]") {
    const message::reject expected(
        message::reject::reason_code::insufficient_fee,
        message::block::command,
        reason_text,
        data);

    data_chunk const raw = expected.to_data(version_maximum);
    message::reject instance{};
    byte_reader reader(raw);
    auto result = message::reject::from_data(reader, message::reject::version_minimum);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(expected == instance);
}

TEST_CASE("reject from data code checkpoint  success", "[reject]") {
    const message::reject expected(
        message::reject::reason_code::checkpoint,
        message::block::command,
        reason_text,
        data);

    data_chunk const raw = expected.to_data(version_maximum);
    message::reject instance{};

    byte_reader reader(raw);
    auto result = message::reject::from_data(reader, message::reject::version_minimum);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(expected == instance);
}

TEST_CASE("reject from data code undefined  success", "[reject]") {
    const message::reject expected(
        message::reject::reason_code::undefined,
        message::block::command,
        reason_text,
        data);

    data_chunk const raw = expected.to_data(version_maximum);
    message::reject instance{};

    byte_reader reader(raw);
    auto result = message::reject::from_data(reader, message::reject::version_minimum);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(expected == instance);
}

TEST_CASE("reject from data valid input  success", "[reject]") {
    const message::reject expected(
        message::reject::reason_code::dust,
        message::block::command,
        reason_text,
        data);

    auto const data = expected.to_data(version_maximum);
    byte_reader reader(data);
    auto const result_exp = message::reject::from_data(reader, version_maximum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);
    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(version_maximum));
    REQUIRE(expected.serialized_size(version_maximum) == result.serialized_size(version_maximum));
}



TEST_CASE("reject  code accessor  always  returns initialized value", "[reject]") {
    auto code = message::reject::reason_code::nonstandard;
    std::string message = "Alpha Beta";
    std::string reason = "Gamma Delta";
    hash_digest data = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    message::reject instance(code, message, reason, data);
    REQUIRE(code == instance.code());
}

TEST_CASE("reject  code setter  roundtrip  success", "[reject]") {
    auto code = message::reject::reason_code::nonstandard;
    std::string message = "Alpha Beta";
    std::string reason = "Gamma Delta";
    hash_digest data = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    message::reject instance;
    REQUIRE(code != instance.code());
    instance.set_code(code);
    REQUIRE(code == instance.code());
}

TEST_CASE("reject  message accessor 1  always  returns initialized value", "[reject]") {
    auto code = message::reject::reason_code::nonstandard;
    std::string message = "Alpha Beta";
    std::string reason = "Gamma Delta";
    hash_digest data = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    message::reject instance(code, message, reason, data);
    REQUIRE(message == instance.message());
}

TEST_CASE("reject  message accessor 2  always  returns initialized value", "[reject]") {
    auto code = message::reject::reason_code::nonstandard;
    std::string message = "Alpha Beta";
    std::string reason = "Gamma Delta";
    hash_digest data = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    const message::reject instance(code, message, reason, data);
    REQUIRE(message == instance.message());
}

TEST_CASE("reject  message setter 1  roundtrip  success", "[reject]") {
    std::string message = "Alpha Beta";
    message::reject instance;
    REQUIRE(message != instance.message());
    instance.set_message(message);
    REQUIRE(message == instance.message());
}

TEST_CASE("reject  message setter 2  roundtrip  success", "[reject]") {
    std::string duplicate = "Gamma";
    std::string message = "Gamma";
    message::reject instance;
    REQUIRE(duplicate != instance.message());
    instance.set_message(std::move(message));
    REQUIRE(duplicate == instance.message());
}

TEST_CASE("reject  reason accessor 1  always  returns initialized value", "[reject]") {
    auto code = message::reject::reason_code::nonstandard;
    std::string message = "Alpha Beta";
    std::string reason = "Gamma Delta";
    hash_digest data = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    message::reject instance(code, message, reason, data);
    REQUIRE(reason == instance.reason());
}

TEST_CASE("reject  reason accessor 2  always  returns initialized value", "[reject]") {
    auto code = message::reject::reason_code::nonstandard;
    std::string message = "Alpha Beta";
    std::string reason = "Gamma Delta";
    hash_digest data = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    const message::reject instance(code, message, reason, data);
    REQUIRE(reason == instance.reason());
}

TEST_CASE("reject  reason setter 1  roundtrip  success", "[reject]") {
    std::string reason = "Alpha Beta";
    message::reject instance;
    REQUIRE(reason != instance.reason());
    instance.set_reason(reason);
    REQUIRE(reason == instance.reason());
}

TEST_CASE("reject  reason setter 2  roundtrip  success", "[reject]") {
    std::string duplicate = "Gamma";
    std::string reason = "Gamma";
    message::reject instance;
    REQUIRE(duplicate != instance.reason());
    instance.set_reason(std::move(reason));
    REQUIRE(duplicate == instance.reason());
}

TEST_CASE("reject  data accessor 1  always  returns initialized value", "[reject]") {
    auto code = message::reject::reason_code::nonstandard;
    std::string message = "Alpha Beta";
    std::string reason = "Gamma Delta";
    hash_digest data = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    message::reject instance(code, message, reason, data);
    REQUIRE(data == instance.data());
}

TEST_CASE("reject  data accessor 2  always  returns initialized value", "[reject]") {
    auto code = message::reject::reason_code::nonstandard;
    std::string message = "Alpha Beta";
    std::string reason = "Gamma Delta";
    hash_digest data = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    const message::reject instance(code, message, reason, data);
    REQUIRE(data == instance.data());
}

TEST_CASE("reject  data setter 1  roundtrip  success", "[reject]") {
    hash_digest data = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    message::reject instance;
    REQUIRE(data != instance.data());
    instance.set_data(data);
    REQUIRE(data == instance.data());
}

TEST_CASE("reject  data setter 2  roundtrip  success", "[reject]") {
    hash_digest duplicate = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    hash_digest data = hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
    message::reject instance;
    REQUIRE(duplicate != instance.data());
    instance.set_data(std::move(data));
    REQUIRE(duplicate == instance.data());
}

TEST_CASE("reject  operator assign equals  always  matches equivalent", "[reject]") {
    message::reject value(
        message::reject::reason_code::dust,
        "My Message",
        "My Reason",
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

    REQUIRE(value.is_valid());

    message::reject instance;
    REQUIRE( ! instance.is_valid());

    instance = std::move(value);
    REQUIRE(instance.is_valid());
}

TEST_CASE("reject  operator boolean equals  duplicates  returns true", "[reject]") {
    const message::reject expected(
        message::reject::reason_code::dust,
        "My Message",
        "My Reason",
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

    message::reject instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("reject  operator boolean equals  differs  returns false", "[reject]") {
    const message::reject expected(
        message::reject::reason_code::dust,
        "My Message",
        "My Reason",
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

    message::reject instance;
    REQUIRE(instance != expected);
}

TEST_CASE("reject - reject  operator boolean not equals  duplicates  returns false", "[reject]") {
    const message::reject expected(
        message::reject::reason_code::dust,
        "My Message",
        "My Reason",
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

    message::reject instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("reject - reject  operator boolean not equals  differs  returns true", "[reject]") {
    const message::reject expected(
        message::reject::reason_code::dust,
        "My Message",
        "My Reason",
        hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));

    message::reject instance;
    REQUIRE(instance != expected);
}

// End Test Suite
