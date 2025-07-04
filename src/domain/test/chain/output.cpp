// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

data_chunk valid_raw_output = to_chunk(base16_literal("20300500000000001976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

// Start Test Suite: output tests

TEST_CASE("output constructor 1 always returns default initialized", "[output]") {
    chain::output instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("output constructor 2  valid input  returns input initialized", "[output]") {
    uint64_t value = 643u;
    auto const data = to_chunk(base16_literal("ece424a6bb6ddf4db592c0faed60685047a361b1"));
    byte_reader reader(data);
    auto const script_result = chain::script::from_data(reader, false);
    REQUIRE(script_result);
    auto const& script = *script_result;

    chain::output instance(value, script, chain::token_data_opt{});
    REQUIRE(instance.is_valid());
    REQUIRE(script == instance.script());
    REQUIRE(value == instance.value());
    REQUIRE( ! instance.token_data().has_value());
}

TEST_CASE("output constructor 3  valid input  returns input initialized", "[output]") {
    uint64_t value = 643u;
    auto const data = to_chunk(base16_literal("ece424a6bb6ddf4db592c0faed60685047a361b1"));
    byte_reader reader(data);
    auto const script_result = chain::script::from_data(reader, false);
    REQUIRE(script_result);
    auto const& script = *script_result;

    // This must be non-const.
    auto dup_script = script;

    chain::output instance(value, std::move(dup_script), chain::token_data_opt{});

    REQUIRE(instance.is_valid());
    REQUIRE(script == instance.script());
    REQUIRE(value == instance.value());
    REQUIRE( ! instance.token_data().has_value());
}

TEST_CASE("output constructor 4  valid input  returns input initialized", "[output]") {
    byte_reader reader(valid_raw_output);
    auto const expected_result = chain::output::from_data(reader);
    REQUIRE(expected_result);
    auto const& expected = *expected_result;

    chain::output instance(expected);
    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
    REQUIRE(instance.value() == 0x53020);
    REQUIRE( ! instance.token_data().has_value());
}

TEST_CASE("output constructor 5  valid input  returns input initialized", "[output]") {
    // This must be non-const.
    byte_reader reader(valid_raw_output);
    auto expected_result = chain::output::from_data(reader);
    REQUIRE(expected_result);
    auto expected = std::move(*expected_result);

    chain::output instance(std::move(expected));
    REQUIRE(instance.is_valid());
    REQUIRE( ! instance.token_data().has_value());
}

TEST_CASE("output from data  insufficient bytes  failure", "[output]") {
    data_chunk data(2);
    byte_reader reader(data);
    auto const result = chain::output::from_data(reader);
    REQUIRE( ! result);
}

TEST_CASE("output from data  valid input success", "[output]") {
    byte_reader reader(valid_raw_output);
    auto const result = chain::output::from_data(reader);
    REQUIRE(result);
    auto const& instance = *result;
    REQUIRE(instance.is_valid());
    REQUIRE(instance.serialized_size() == valid_raw_output.size());
    REQUIRE( ! instance.token_data().has_value());

    // Re-save and compare against original.
    data_chunk resave = instance.to_data();
    REQUIRE(resave.size() == valid_raw_output.size());
    REQUIRE(resave == valid_raw_output);
}

TEST_CASE("output factory from data 2  valid input success", "[output]") {
    byte_reader reader(valid_raw_output);
    auto const result = chain::output::from_data(reader);
    REQUIRE(result);
    auto const& instance = *result;
    REQUIRE(instance.is_valid());
    REQUIRE(instance.serialized_size() == valid_raw_output.size());
    REQUIRE( ! instance.token_data().has_value());

    // Re-save and compare against original.
    data_chunk resave = instance.to_data();
    REQUIRE(resave.size() == valid_raw_output.size());
    REQUIRE(resave == valid_raw_output);
}

TEST_CASE("output factory from data 3  valid input success", "[output]") {
    byte_reader reader(valid_raw_output);
    auto const result = chain::output::from_data(reader);
    REQUIRE(result);
    auto const& instance = *result;
    REQUIRE(instance.is_valid());
    REQUIRE(instance.serialized_size() == valid_raw_output.size());
    REQUIRE( ! instance.token_data().has_value());

    // Re-save and compare against original.
    data_chunk resave = instance.to_data();
    REQUIRE(resave.size() == valid_raw_output.size());
    REQUIRE(resave == valid_raw_output);
}

TEST_CASE("output signature operations  always  returns script sigops false", "[output]") {
    chain::output instance;
    REQUIRE(instance.script().sigops(false) == instance.signature_operations(false));
    REQUIRE( ! instance.token_data().has_value());
}

TEST_CASE("output value  roundtrip  success", "[output]") {
    uint64_t expected = 523542u;
    chain::output instance;
    REQUIRE(expected != instance.value());
    REQUIRE( ! instance.token_data().has_value());
    instance.set_value(expected);
    REQUIRE(expected == instance.value());
    REQUIRE( ! instance.token_data().has_value());
}

TEST_CASE("output script setter 1  roundtrip  success", "[output]") {
    auto const data = to_chunk(base16_literal("ece424a6bb6ddf4db592c0faed60685047a361b1"));
    byte_reader reader(data);
    auto const value_result = chain::script::from_data(reader, false);
    REQUIRE(value_result);
    auto const& value = *value_result;

    chain::output instance;
    REQUIRE(value != instance.script());
    REQUIRE( ! instance.token_data().has_value());
    instance.set_script(value);
    REQUIRE(value == instance.script());
    REQUIRE( ! instance.token_data().has_value());
    auto const& restricted = instance;
    REQUIRE(value == instance.script());
}

TEST_CASE("output script setter 2  roundtrip  success", "[output]") {
    chain::script value;
    auto const data = to_chunk(base16_literal("ece424a6bb6ddf4db592c0faed60685047a361b1"));
    byte_reader reader(data);
    auto result = chain::script::from_data(reader, false);
    REQUIRE(result);
    value = std::move(*result);

    // This must be non-const.
    auto dup_value = value;

    chain::output instance;
    REQUIRE(value != instance.script());
    REQUIRE( ! instance.token_data().has_value());
    instance.set_script(std::move(dup_value));
    REQUIRE(value == instance.script());
    REQUIRE( ! instance.token_data().has_value());
    auto const& restricted = instance;
    REQUIRE(value == instance.script());
}

TEST_CASE("output operator assign equals 1  always  matches equivalent", "[output]") {
    byte_reader reader(valid_raw_output);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    auto const expected = std::move(*result);
    reader.reset();
    auto result_exp = chain::output::from_data(reader);
    REQUIRE(result_exp);
    auto const instance = std::move(*result_exp);
    REQUIRE(instance == expected);
}

TEST_CASE("output operator assign equals 2  always  matches equivalent", "[output]") {
    byte_reader reader(valid_raw_output);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    auto const expected = std::move(*result);
    chain::output instance;
    instance = expected;
    REQUIRE(instance == expected);
}

TEST_CASE("output operator boolean equals  duplicates  returns true", "[output]") {
    chain::output alpha;
    chain::output beta;
    byte_reader reader(valid_raw_output);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    alpha = std::move(*result);
    reader.reset();
    result = chain::output::from_data(reader);
    REQUIRE(result);
    beta = std::move(*result);
    REQUIRE(alpha == beta);
}

TEST_CASE("output operator boolean equals  differs  returns false", "[output]") {
    chain::output alpha;
    chain::output beta;
    byte_reader reader(valid_raw_output);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    alpha = std::move(*result);
    REQUIRE(alpha != beta);
}

TEST_CASE("output operator boolean not equals  duplicates  returns false", "[output]") {
    chain::output alpha;
    chain::output beta;
    byte_reader reader(valid_raw_output);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    alpha = std::move(*result);
    reader.reset();
    result = chain::output::from_data(reader);
    REQUIRE(result);
    beta = std::move(*result);
    REQUIRE(alpha == beta);
}

TEST_CASE("output operator boolean not equals  differs  returns true", "[output]") {
    chain::output alpha;
    chain::output beta;
    byte_reader reader(valid_raw_output);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    alpha = std::move(*result);
    REQUIRE(alpha != beta);
}

// Cash Tokens ------------------------------------------------------------------------------

TEST_CASE("output deserialization with just FT amount 1", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "3c" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb1001" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::fungible>(instance.token_data().value().data));
    auto const& ft = std::get<chain::fungible>(instance.token_data().value().data);
    REQUIRE(ft.amount == chain::amount_t{0x01});

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with just FT amount 252", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "3c" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb10fc" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::fungible>(instance.token_data().value().data));
    auto const& ft = std::get<chain::fungible>(instance.token_data().value().data);
    REQUIRE(ft.amount == chain::amount_t{252});

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with just FT amount 253", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "3e" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb10fdfd00" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::fungible>(instance.token_data().value().data));
    auto const& ft = std::get<chain::fungible>(instance.token_data().value().data);
    REQUIRE(ft.amount == chain::amount_t{253});

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with just FT amount 9223372036854775807", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "44" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb10ffffffffffffffff7f" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::fungible>(instance.token_data().value().data));
    auto const& ft = std::get<chain::fungible>(instance.token_data().value().data);
    REQUIRE(ft.amount == chain::amount_t{9223372036854775807});

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with just immutable NFT 0-byte commitment", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "3b" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb20" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::non_fungible>(instance.token_data().value().data));
    auto const& nft = std::get<chain::non_fungible>(instance.token_data().value().data);
    REQUIRE(nft.commitment.size() == 0);
    REQUIRE(nft.capability == chain::capability_t::none); // immutable

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - immutable NFT 0-byte commitment - FT amount 1", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "3c" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb3001" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::both_kinds>(instance.token_data().value().data));
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).first;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).second;
    REQUIRE(ft.amount == chain::amount_t{1});
    REQUIRE(nft.commitment.size() == 0);
    REQUIRE(nft.capability == chain::capability_t::none); // immutable

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - immutable NFT 0-byte commitment - FT amount 253", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "3e" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb30fdfd00" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::both_kinds>(instance.token_data().value().data));
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).first;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).second;
    REQUIRE(ft.amount == chain::amount_t{253});
    REQUIRE(nft.commitment.size() == 0);
    REQUIRE(nft.capability == chain::capability_t::none); // immutable

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - immutable NFT 0-byte commitment - FT amount 9223372036854775807", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "44" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb30ffffffffffffffff7f" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::both_kinds>(instance.token_data().value().data));
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).first;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).second;
    REQUIRE(ft.amount == chain::amount_t{9223372036854775807});
    REQUIRE(nft.commitment.size() == 0);
    REQUIRE(nft.capability == chain::capability_t::none); // immutable

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with just immutable NFT 1-byte commitment", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "3d" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb6001cc" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::non_fungible>(instance.token_data().value().data));
    auto const& nft = std::get<chain::non_fungible>(instance.token_data().value().data);
    REQUIRE(nft.commitment.size() == 1);
    REQUIRE(encode_base16(nft.commitment) == "cc");
    REQUIRE(nft.capability == chain::capability_t::none); // immutable

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - immutable NFT 1-byte commitment - FT amount 252", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "3e" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb7001ccfc" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::both_kinds>(instance.token_data().value().data));
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).first;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).second;
    REQUIRE(ft.amount == chain::amount_t{252});
    REQUIRE(nft.commitment.size() == 1);
    REQUIRE(encode_base16(nft.commitment) == "cc");
    REQUIRE(nft.capability == chain::capability_t::none); // immutable

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - immutable NFT 2-byte commitment - FT amount 253", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "41" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb7002ccccfdfd00" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::both_kinds>(instance.token_data().value().data));
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).first;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).second;
    REQUIRE(ft.amount == chain::amount_t{253});
    REQUIRE(nft.commitment.size() == 2);
    REQUIRE(encode_base16(nft.commitment) == "cccc");
    REQUIRE(nft.capability == chain::capability_t::none); // immutable

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - immutable NFT 10-byte commitment - FT amount 65535", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "49" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb700accccccccccccccccccccfdffff" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::both_kinds>(instance.token_data().value().data));
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).first;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).second;
    REQUIRE(ft.amount == chain::amount_t{65535});
    REQUIRE(nft.commitment.size() == 10);
    REQUIRE(encode_base16(nft.commitment) == "cccccccccccccccccccc");
    REQUIRE(nft.capability == chain::capability_t::none); // immutable

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - immutable NFT 40-byte commitment - FT amount 65536", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "69" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb7028ccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccfe00000100" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::both_kinds>(instance.token_data().value().data));
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).first;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).second;
    REQUIRE(ft.amount == chain::amount_t{65536});
    REQUIRE(nft.commitment.size() == 40);
    REQUIRE(encode_base16(nft.commitment) == "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    REQUIRE(nft.capability == chain::capability_t::none); // immutable

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with just mutable NFT 0-byte commitment", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "3b" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb21" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::non_fungible>(instance.token_data().value().data));
    auto const& nft = std::get<chain::non_fungible>(instance.token_data().value().data);
    REQUIRE(nft.commitment.size() == 0);
    REQUIRE(nft.capability == chain::capability_t::mut);

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - mutable NFT 0-byte commitment - FT amount 4294967295", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "40" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb31feffffffff" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::both_kinds>(instance.token_data().value().data));
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).first;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).second;
    REQUIRE(ft.amount == chain::amount_t{4294967295});
    REQUIRE(nft.commitment.size() == 0);
    REQUIRE(nft.capability == chain::capability_t::mut); // mutable

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with just mutable NFT 1-byte commitment", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "3d" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb6101cc" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::non_fungible>(instance.token_data().value().data));
    auto const& nft = std::get<chain::non_fungible>(instance.token_data().value().data);
    REQUIRE(nft.commitment.size() == 1);
    REQUIRE(encode_base16(nft.commitment) == "cc");
    REQUIRE(nft.capability == chain::capability_t::mut); // mutable

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - mutable NFT 1-byte commitment - FT amount 4294967296", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "46" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb7101ccff0000000001000000" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::both_kinds>(instance.token_data().value().data));
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).first;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).second;
    REQUIRE(ft.amount == chain::amount_t{4294967296});
    REQUIRE(nft.commitment.size() == 1);
    REQUIRE(encode_base16(nft.commitment) == "cc");
    REQUIRE(nft.capability == chain::capability_t::mut); // mutable

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - mutable NFT 2-byte commitment - FT amount 9223372036854775807", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "47" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb7102ccccffffffffffffffff7f" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::both_kinds>(instance.token_data().value().data));
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).first;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).second;
    REQUIRE(ft.amount == chain::amount_t{9223372036854775807});
    REQUIRE(nft.commitment.size() == 2);
    REQUIRE(encode_base16(nft.commitment) == "cccc");
    REQUIRE(nft.capability == chain::capability_t::mut); // mutable

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - mutable NFT 10-byte commitment - FT amount 1", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "47" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb710acccccccccccccccccccc01" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::both_kinds>(instance.token_data().value().data));
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).first;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).second;
    REQUIRE(ft.amount == chain::amount_t{1});
    REQUIRE(nft.commitment.size() == 10);
    REQUIRE(encode_base16(nft.commitment) == "cccccccccccccccccccc");
    REQUIRE(nft.capability == chain::capability_t::mut); // mutable

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - mutable NFT 40-byte commitment - FT amount 252", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "65" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb7128ccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccfc" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::both_kinds>(instance.token_data().value().data));
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).first;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).second;
    REQUIRE(ft.amount == chain::amount_t{252});
    REQUIRE(nft.commitment.size() == 40);
    REQUIRE(encode_base16(nft.commitment) == "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    REQUIRE(nft.capability == chain::capability_t::mut); // mutable

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with just minting NFT 0-byte commitment", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "3b" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb22" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::non_fungible>(instance.token_data().value().data));
    auto const& nft = std::get<chain::non_fungible>(instance.token_data().value().data);
    REQUIRE(nft.commitment.size() == 0);
    REQUIRE(nft.capability == chain::capability_t::minting);

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - minting NFT 0-byte commitment - FT amount 253", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "3e" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb32fdfd00" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::both_kinds>(instance.token_data().value().data));
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).first;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).second;
    REQUIRE(ft.amount == chain::amount_t{253});
    REQUIRE(nft.commitment.size() == 0);
    REQUIRE(nft.capability == chain::capability_t::minting); // minting

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with just minting NFT 1-byte commitment", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "3d" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb6201cc" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::non_fungible>(instance.token_data().value().data));
    auto const& nft = std::get<chain::non_fungible>(instance.token_data().value().data);
    REQUIRE(nft.commitment.size() == 1);
    REQUIRE(encode_base16(nft.commitment) == "cc");
    REQUIRE(nft.capability == chain::capability_t::minting); // minting

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - minting NFT 1-byte commitment - FT amount 65535", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "40" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb7201ccfdffff" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::both_kinds>(instance.token_data().value().data));
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).first;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).second;
    REQUIRE(ft.amount == chain::amount_t{65535});
    REQUIRE(nft.commitment.size() == 1);
    REQUIRE(encode_base16(nft.commitment) == "cc");
    REQUIRE(nft.capability == chain::capability_t::minting); // minting

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - minting NFT 2-byte commitment - FT amount 65536", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "43" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb7202ccccfe00000100" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::both_kinds>(instance.token_data().value().data));
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).first;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).second;
    REQUIRE(ft.amount == chain::amount_t{65536});
    REQUIRE(nft.commitment.size() == 2);
    REQUIRE(encode_base16(nft.commitment) == "cccc");
    REQUIRE(nft.capability == chain::capability_t::minting); // minting

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - minting NFT 10-byte commitment - FT amount 4294967297", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "4f" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb720accccccccccccccccccccff0100000001000000" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::both_kinds>(instance.token_data().value().data));
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).first;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).second;
    REQUIRE(ft.amount == chain::amount_t{4294967297});
    REQUIRE(nft.commitment.size() == 10);
    REQUIRE(encode_base16(nft.commitment) == "cccccccccccccccccccc");
    REQUIRE(nft.capability == chain::capability_t::minting); // minting

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - minting NFT 40-byte commitment - FT amount 9223372036854775807", "[output]") {
    auto const data = to_chunk(base16_literal("2030050000000000" "6d" "efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb7228ccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccffffffffffffffff7f" "76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 0x53020);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    REQUIRE(std::holds_alternative<chain::both_kinds>(instance.token_data().value().data));
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).first;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).second;
    REQUIRE(ft.amount == chain::amount_t{9223372036854775807});
    REQUIRE(nft.commitment.size() == 40);
    REQUIRE(encode_base16(nft.commitment) == "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    REQUIRE(nft.capability == chain::capability_t::minting); // minting

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

TEST_CASE("output deserialization ...", "[output]") {
    auto const data = to_chunk(base16_literal("e803000000000000" "3c" "efb43378d02ca3a5ef93f150d44b3be4f098f103e4336062ee2142d03ddd9ac629100a" "76a91448a5e322b29f3db7297f4dc744e30bca63a0179d88ac"));

    chain::output instance;
    byte_reader reader(data);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);

    REQUIRE(instance.is_valid());

    REQUIRE(instance.value() == 1000);

    REQUIRE(instance.token_data().has_value());
    REQUIRE(encode_base16(instance.token_data().value().id) == "b43378d02ca3a5ef93f150d44b3be4f098f103e4336062ee2142d03ddd9ac629");
    REQUIRE(std::holds_alternative<chain::fungible>(instance.token_data().value().data));
    auto const& ft = std::get<chain::fungible>(instance.token_data().value().data);
    REQUIRE(ft.amount == chain::amount_t{0x0a});

    REQUIRE(encode_base16(instance.script().to_data(true)) == "1976a91448a5e322b29f3db7297f4dc744e30bca63a0179d88ac");
    REQUIRE(encode_base16(instance.to_data(true)) == encode_base16(data));
}

// End Test Suite
