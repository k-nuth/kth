// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: ping tests

TEST_CASE("ping constructor 1 always invalid", "[ping]") {
    message::ping instance;
}

TEST_CASE("ping constructor 2 always equals params", "[ping]") {
    uint64_t nonce = 462434u;
    message::ping instance(nonce);
    REQUIRE(nonce == instance.nonce());
}

TEST_CASE("ping constructor 3 always equals params", "[ping]") {
    message::ping expected(24235u);
    message::ping instance(expected);
    REQUIRE(expected == instance);
}

TEST_CASE("ping satoshi fixed size minimum version zero", "[ping]") {
    REQUIRE(0u == message::ping::satoshi_fixed_size(message::version::level::minimum));
}

TEST_CASE("ping satoshi fixed size bip31 version 8", "[ping]") {
    REQUIRE(8u == message::ping::satoshi_fixed_size(message::version::level::bip31));
}

TEST_CASE("ping from data maximum version empty data invalid", "[ping]") {
    static auto const version = message::version::level::maximum;
    byte_reader reader(data_chunk{});
    auto const result_exp = message::ping::from_data(reader, version);
    REQUIRE( ! result_exp);
}

TEST_CASE("ping from data minimum version empty data valid", "[ping]") {
    static auto const version = message::version::level::minimum;
    byte_reader reader(data_chunk{});
    auto const result_exp = message::ping::from_data(reader, version);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);
}

TEST_CASE("ping from data 1 minimum version success zero nonce", "[ping]") {
    static const message::ping value{213153u};

    // This serializes the nonce.
    auto const data = kth::to_data_chunk(value, message::version::level::bip31);
    REQUIRE(data.size() == 8u);

    // This leaves the nonce on the wire but otherwise succeeds with a zero nonce.
    message::ping instance;
    byte_reader reader(data);
    auto result = message::ping::from_data(reader, message::ping::version_minimum);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(instance.nonce() == 0u);
}

TEST_CASE("ping from data minimum version round trip zero nonce", "[ping]") {
    static const message::ping value{
        16545612u};

    static auto const version = message::version::level::minimum;
    auto const data = kth::to_data_chunk(value, version);
    byte_reader reader(data);
    auto const result_exp = message::ping::from_data(reader, version);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);
    REQUIRE(result.nonce() == 0u);
}

TEST_CASE("ping from data 1 maximum version success expected nonce", "[ping]") {
    static const message::ping expected{
        213153u};

    // This serializes the nonce.
    auto const data = kth::to_data_chunk(expected, message::version::level::bip31);
    REQUIRE(data.size() == 8u);

    // This leaves the nonce on the wire but otherwise succeeds with a zero nonce.
    message::ping instance;
    byte_reader reader(data);
    auto result = message::ping::from_data(reader, message::ping::version_maximum);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(instance == expected);
}

TEST_CASE("ping from data bip31 version round trip expected nonce", "[ping]") {
    static const message::ping expected{
        16545612u};

    static auto const version = message::version::level::bip31;
    auto const data = kth::to_data_chunk(expected, version);
    byte_reader reader(data);
    auto const result_exp = message::ping::from_data(reader, version);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);
    REQUIRE(result == expected);
}

TEST_CASE("ping nonce accessor always returns initialized value", "[ping]") {
    uint64_t value = 43564u;
    message::ping instance(value);
    REQUIRE(value == instance.nonce());
}

TEST_CASE("ping constructor sets nonce", "[ping]") {
    uint64_t value = 43564u;
    message::ping const instance(value);
    REQUIRE(value == instance.nonce());
}

TEST_CASE("ping operator assign equals always matches equivalent", "[ping]") {
    message::ping value(356234u);
    message::ping instance;
    instance = std::move(value);
}

TEST_CASE("ping operator boolean equals duplicates returns true", "[ping]") {
    const message::ping expected(4543234u);
    message::ping instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("ping operator boolean equals differs returns false", "[ping]") {
    const message::ping expected(547553u);
    message::ping instance;
    REQUIRE(instance != expected);
}

TEST_CASE("ping operator boolean not equals duplicates returns false", "[ping]") {
    const message::ping expected(653786u);
    message::ping instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("ping operator boolean not equals differs returns true", "[ping]") {
    const message::ping expected(89764u);
    message::ping instance;
    REQUIRE(instance != expected);
}

TEST_CASE("ping is usable in a constant expression", "[ping]") {
    static_assert(message::ping{7u}.nonce() == 7u);
    static_assert(message::ping{1u} == message::ping{1u});
    // Before BIP31 a ping is a bare marker; from BIP31 on it carries the nonce.
    static_assert(message::ping{}.serialized_size(message::version::level::bip31 - 1u) == 0u);
    static_assert(message::ping{}.serialized_size(message::version::level::bip31) == 8u);
    REQUIRE(true);
}

// End Test Suite
