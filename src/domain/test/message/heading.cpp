// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string>

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::message;

// Start Test Suite: heading tests

TEST_CASE("heading  constructor 1  always  initialized invalid", "[heading]") {
    heading instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("heading  constructor 2  always  equals params", "[heading]") {
    uint32_t magic = 123u;
    std::string const command = "foo";
    uint32_t payload_size = 3454u;
    uint32_t checksum = 35746u;
    heading instance(magic, command, payload_size, checksum);
    REQUIRE(instance.is_valid());
    REQUIRE(magic == instance.magic());
    REQUIRE(command == instance.command());
    REQUIRE(payload_size == instance.payload_size());
    REQUIRE(checksum == instance.checksum());
}

TEST_CASE("heading  constructor 3  always  equals params", "[heading]") {
    uint32_t magic = 123u;
    std::string const command = "foo";
    uint32_t payload_size = 3454u;
    uint32_t checksum = 35746u;
    heading instance(magic, "foo", payload_size, checksum);
    REQUIRE(instance.is_valid());
    REQUIRE(magic == instance.magic());
    REQUIRE(command == instance.command());
    REQUIRE(payload_size == instance.payload_size());
    REQUIRE(checksum == instance.checksum());
}

TEST_CASE("heading  constructor 4  always  equals params", "[heading]") {
    heading expected(453u, "bar", 436u, 5743u);
    heading instance(expected);
    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
}

TEST_CASE("heading  constructor 5  always  equals params", "[heading]") {
    uint32_t magic = 123u;
    std::string const command = "foo";
    uint32_t payload_size = 3454u;
    uint32_t checksum = 35746u;
    heading value(magic, command, payload_size, checksum);
    heading instance(std::move(value));
    REQUIRE(instance.is_valid());
    REQUIRE(magic == instance.magic());
    REQUIRE(command == instance.command());
    REQUIRE(payload_size == instance.payload_size());
    REQUIRE(checksum == instance.checksum());
}

TEST_CASE("heading  to data  checksum variations  success", "[heading]") {
    heading instance{
        32414u,
        "foo",
        56731u,
        0u};

    auto const zero_checksum = instance.to_data();
    REQUIRE(zero_checksum.size() == heading::satoshi_fixed_size());

    instance.set_checksum(123u);
    auto const nonzero_checksum = instance.to_data();
    REQUIRE(nonzero_checksum.size() == heading::satoshi_fixed_size());
}

TEST_CASE("heading from data insufficient bytes  failure", "[heading]") {
    static data_chunk const raw{
        0xab, 0xcd};

    heading instance;
    byte_reader reader(raw);
    auto result = heading::from_data(reader, true);
    REQUIRE( ! result);
}

TEST_CASE("heading from data valid input  success", "[heading]") {
    static const heading expected{
        32414u,
        "foo",
        56731u,
        0u};

    auto const data = expected.to_data();
    byte_reader reader(data);
    auto const result_exp = heading::from_data(reader, true);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);
    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
    REQUIRE(data.size() == heading::satoshi_fixed_size());
}



TEST_CASE("heading  magic accessor  always  returns initialized value", "[heading]") {
    uint32_t expected = 3574u;
    message::heading instance(expected, "baz", 4356u, 7923u);
    REQUIRE(expected == instance.magic());
}

TEST_CASE("heading  magic setter  roundtrip  success", "[heading]") {
    uint32_t const expected = 3574u;
    message::heading instance;
    REQUIRE(0 == instance.magic());
    instance.set_magic(expected);
    REQUIRE(expected == instance.magic());
}

TEST_CASE("heading  command accessor 1  always  returns initialized value", "[heading]") {
    std::string const expected = "asdge";
    message::heading instance(545u, expected, 4356u, 7923u);
    REQUIRE(expected == instance.command());
}

TEST_CASE("heading  command accessor 2  always  returns initialized value", "[heading]") {
    std::string const expected = "asdge";
    const message::heading instance(545u, expected, 4356u, 7923u);
    REQUIRE(expected == instance.command());
}

TEST_CASE("heading  command setter 1  roundtrip  success", "[heading]") {
    std::string const expected = "gdasd";
    message::heading instance;
    REQUIRE(expected != instance.command());
    instance.set_command(expected);
    REQUIRE(expected == instance.command());
}

TEST_CASE("heading  command setter 2  roundtrip  success", "[heading]") {
    std::string expected = "eytyry";
    message::heading instance;
    REQUIRE(expected != instance.command());
    instance.set_command("eytyry");
    REQUIRE(expected == instance.command());
}

TEST_CASE("heading  payload size accessor  always  returns initialized value", "[heading]") {
    uint32_t const expected = 4356u;
    message::heading instance(3574u, "baz", expected, 7923u);
    REQUIRE(expected == instance.payload_size());
}

TEST_CASE("heading  payload size setter  roundtrip  success", "[heading]") {
    uint32_t const expected = 3574u;
    message::heading instance;
    REQUIRE(0 == instance.payload_size());
    instance.set_payload_size(expected);
    REQUIRE(expected == instance.payload_size());
}

TEST_CASE("heading  checksum accessor  always  returns initialized value", "[heading]") {
    uint32_t expected = 7923u;
    message::heading instance(3574u, "baz", 4356u, expected);
    REQUIRE(expected == instance.checksum());
}

TEST_CASE("heading  checksum setter  roundtrip  success", "[heading]") {
    uint32_t const expected = 3574u;
    message::heading instance;
    REQUIRE(0 == instance.checksum());
    instance.set_checksum(expected);
    REQUIRE(expected == instance.checksum());
}

TEST_CASE("heading  operator assign equals  always  matches equivalent", "[heading]") {
    message::heading value(1u, "foobar", 2u, 3u);
    REQUIRE(value.is_valid());
    message::heading instance;
    REQUIRE( ! instance.is_valid());
    instance = std::move(value);
    REQUIRE(instance.is_valid());
}

TEST_CASE("heading  operator boolean equals  duplicates  returns true", "[heading]") {
    const message::heading expected(1u, "foobar", 2u, 3u);
    message::heading instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("heading  operator boolean equals  differs  returns false", "[heading]") {
    const message::heading expected(1u, "foobar", 2u, 3u);
    message::heading instance;
    REQUIRE(instance != expected);
}

TEST_CASE("heading  operator boolean not equals  duplicates  returns false", "[heading]") {
    const message::heading expected(1u, "foobar", 2u, 3u);
    message::heading instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("heading  operator boolean not equals  differs  returns true", "[heading]") {
    const message::heading expected(1u, "foobar", 2u, 3u);
    message::heading instance;
    REQUIRE(instance != expected);
}

TEST_CASE("heading type all cases match expected", "[heading]") {
    message::heading instance;
    REQUIRE(message::message_type::unknown == instance.type());
    instance.set_command(message::address::command);
    REQUIRE(message::message_type::address == instance.type());
    instance.set_command(message::alert::command);
    REQUIRE(message::message_type::alert == instance.type());
    instance.set_command(message::block::command);
    REQUIRE(message::message_type::block == instance.type());
    instance.set_command(message::block_transactions::command);
    REQUIRE(message::message_type::block_transactions == instance.type());
    instance.set_command(message::compact_block::command);
    REQUIRE(message::message_type::compact_block == instance.type());
    instance.set_command(message::double_spend_proof::command);
    REQUIRE(message::message_type::double_spend_proof == instance.type());
    instance.set_command(message::filter_add::command);
    REQUIRE(message::message_type::filter_add == instance.type());
    instance.set_command(message::filter_clear::command);
    REQUIRE(message::message_type::filter_clear == instance.type());
    instance.set_command(message::filter_load::command);
    REQUIRE(message::message_type::filter_load == instance.type());
    instance.set_command(message::get_address::command);
    REQUIRE(message::message_type::get_address == instance.type());
    instance.set_command(message::get_block_transactions::command);
    REQUIRE(message::message_type::get_block_transactions == instance.type());
    instance.set_command(message::get_blocks::command);
    REQUIRE(message::message_type::get_blocks == instance.type());
    instance.set_command(message::get_data::command);
    REQUIRE(message::message_type::get_data == instance.type());
    instance.set_command(message::get_headers::command);
    REQUIRE(message::message_type::get_headers == instance.type());
    instance.set_command(message::headers::command);
    REQUIRE(message::message_type::headers == instance.type());
    instance.set_command(message::inventory::command);
    REQUIRE(message::message_type::inventory == instance.type());
    instance.set_command(message::memory_pool::command);
    REQUIRE(message::message_type::memory_pool == instance.type());
    instance.set_command(message::merkle_block::command);
    REQUIRE(message::message_type::merkle_block == instance.type());
    instance.set_command(message::not_found::command);
    REQUIRE(message::message_type::not_found == instance.type());
    instance.set_command(message::ping::command);
    REQUIRE(message::message_type::ping == instance.type());
    instance.set_command(message::pong::command);
    REQUIRE(message::message_type::pong == instance.type());
    instance.set_command(message::reject::command);
    REQUIRE(message::message_type::reject == instance.type());
    instance.set_command(message::send_compact::command);
    REQUIRE(message::message_type::send_compact == instance.type());
    instance.set_command(message::send_headers::command);
    REQUIRE(message::message_type::send_headers == instance.type());
    instance.set_command(message::transaction::command);
    REQUIRE(message::message_type::transaction == instance.type());
    instance.set_command(message::verack::command);
    REQUIRE(message::message_type::verack == instance.type());
    instance.set_command(message::version::command);
    REQUIRE(message::message_type::version == instance.type());
    instance.set_command(message::xverack::command);
    REQUIRE(message::message_type::xverack == instance.type());
    instance.set_command(message::xversion::command);
    REQUIRE(message::message_type::xversion == instance.type());
}

TEST_CASE("heading  maximum size  always  matches satoshi fixed size", "[heading]") {
    REQUIRE(heading::satoshi_fixed_size() == heading::maximum_size());
}

// End Test Suite
