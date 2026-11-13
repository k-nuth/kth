// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

data_chunk valid_raw_output = to_chunk("20300500000000001976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

// Start Test Suite: output tests

TEST_CASE("output constructor 1 always returns default initialized", "[output]") {
    chain::output instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("output constructor 2 valid input returns input initialized", "[output]") {
    uint64_t value = 643u;
    auto const data = to_chunk("ece424a6bb6ddf4db592c0faed60685047a361b1"_base16);
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

TEST_CASE("output constructor 3 valid input returns input initialized", "[output]") {
    uint64_t value = 643u;
    auto const data = to_chunk("ece424a6bb6ddf4db592c0faed60685047a361b1"_base16);
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

TEST_CASE("output constructor 4 valid input returns input initialized", "[output]") {
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

TEST_CASE("output constructor 5 valid input returns input initialized", "[output]") {
    // This must be non-const.
    byte_reader reader(valid_raw_output);
    auto expected_result = chain::output::from_data(reader);
    REQUIRE(expected_result);
    auto expected = std::move(*expected_result);

    chain::output instance(std::move(expected));
    REQUIRE(instance.is_valid());
    REQUIRE( ! instance.token_data().has_value());
}

TEST_CASE("output from data insufficient bytes failure", "[output]") {
    data_chunk data(2);
    byte_reader reader(data);
    auto const result = chain::output::from_data(reader);
    REQUIRE( ! result);
}

TEST_CASE("output from data valid input success", "[output]") {
    byte_reader reader(valid_raw_output);
    auto const result = chain::output::from_data(reader);
    REQUIRE(result);
    auto const& instance = *result;
    REQUIRE(instance.is_valid());
    REQUIRE(instance.serialized_size() == valid_raw_output.size());
    REQUIRE( ! instance.token_data().has_value());

    // Re-save and compare against original.
    data_chunk resave = kth::to_data_chunk(instance, true);
    REQUIRE(resave.size() == valid_raw_output.size());
    REQUIRE(resave == valid_raw_output);
}

TEST_CASE("output factory from data 2 valid input success", "[output]") {
    byte_reader reader(valid_raw_output);
    auto const result = chain::output::from_data(reader);
    REQUIRE(result);
    auto const& instance = *result;
    REQUIRE(instance.is_valid());
    REQUIRE(instance.serialized_size() == valid_raw_output.size());
    REQUIRE( ! instance.token_data().has_value());

    // Re-save and compare against original.
    data_chunk resave = kth::to_data_chunk(instance, true);
    REQUIRE(resave.size() == valid_raw_output.size());
    REQUIRE(resave == valid_raw_output);
}

TEST_CASE("output factory from data 3 valid input success", "[output]") {
    byte_reader reader(valid_raw_output);
    auto const result = chain::output::from_data(reader);
    REQUIRE(result);
    auto const& instance = *result;
    REQUIRE(instance.is_valid());
    REQUIRE(instance.serialized_size() == valid_raw_output.size());
    REQUIRE( ! instance.token_data().has_value());

    // Re-save and compare against original.
    data_chunk resave = kth::to_data_chunk(instance, true);
    REQUIRE(resave.size() == valid_raw_output.size());
    REQUIRE(resave == valid_raw_output);
}

TEST_CASE("output signature operations always returns script sigops false", "[output]") {
    chain::output instance;
    REQUIRE(instance.script().sigops(false) == instance.signature_operations(false));
    REQUIRE( ! instance.token_data().has_value());
}

TEST_CASE("output value roundtrip success", "[output]") {
    uint64_t expected = 523542u;
    chain::output instance;
    REQUIRE(expected != instance.value());
    REQUIRE( ! instance.token_data().has_value());
    instance.set_value(expected);
    REQUIRE(expected == instance.value());
    REQUIRE( ! instance.token_data().has_value());
}

TEST_CASE("output script setter 1 roundtrip success", "[output]") {
    auto const data = to_chunk("ece424a6bb6ddf4db592c0faed60685047a361b1"_base16);
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

TEST_CASE("output script setter 2 roundtrip success", "[output]") {
    chain::script value;
    auto const data = to_chunk("ece424a6bb6ddf4db592c0faed60685047a361b1"_base16);
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

TEST_CASE("output operator assign equals 1 always matches equivalent", "[output]") {
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

TEST_CASE("output operator assign equals 2 always matches equivalent", "[output]") {
    byte_reader reader(valid_raw_output);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    auto const expected = std::move(*result);
    chain::output instance;
    instance = expected;
    REQUIRE(instance == expected);
}

TEST_CASE("output operator boolean equals duplicates returns true", "[output]") {
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

TEST_CASE("output operator boolean equals differs returns false", "[output]") {
    chain::output alpha;
    chain::output beta;
    byte_reader reader(valid_raw_output);
    auto result = chain::output::from_data(reader);
    REQUIRE(result);
    alpha = std::move(*result);
    REQUIRE(alpha != beta);
}

TEST_CASE("output operator boolean not equals duplicates returns false", "[output]") {
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

TEST_CASE("output operator boolean not equals differs returns true", "[output]") {
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
    auto const data = to_chunk("20300500000000003cefbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb100176a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with just FT amount 252", "[output]") {
    auto const data = to_chunk("20300500000000003cefbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb10fc76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with just FT amount 253", "[output]") {
    auto const data = to_chunk("20300500000000003eefbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb10fdfd0076a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with just FT amount 9223372036854775807", "[output]") {
    auto const data = to_chunk("203005000000000044efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb10ffffffffffffffff7f76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with just immutable NFT 0-byte commitment", "[output]") {
    auto const data = to_chunk("20300500000000003befbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb2076a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - immutable NFT 0-byte commitment - FT amount 1", "[output]") {
    auto const data = to_chunk("20300500000000003cefbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb300176a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).fungible_part;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).non_fungible_part;
    REQUIRE(ft.amount == chain::amount_t{1});
    REQUIRE(nft.commitment.size() == 0);
    REQUIRE(nft.capability == chain::capability_t::none); // immutable

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - immutable NFT 0-byte commitment - FT amount 253", "[output]") {
    auto const data = to_chunk("20300500000000003eefbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb30fdfd0076a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).fungible_part;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).non_fungible_part;
    REQUIRE(ft.amount == chain::amount_t{253});
    REQUIRE(nft.commitment.size() == 0);
    REQUIRE(nft.capability == chain::capability_t::none); // immutable

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - immutable NFT 0-byte commitment - FT amount 9223372036854775807", "[output]") {
    auto const data = to_chunk("203005000000000044efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb30ffffffffffffffff7f76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).fungible_part;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).non_fungible_part;
    REQUIRE(ft.amount == chain::amount_t{9223372036854775807});
    REQUIRE(nft.commitment.size() == 0);
    REQUIRE(nft.capability == chain::capability_t::none); // immutable

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with just immutable NFT 1-byte commitment", "[output]") {
    auto const data = to_chunk("20300500000000003defbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb6001cc76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - immutable NFT 1-byte commitment - FT amount 252", "[output]") {
    auto const data = to_chunk("20300500000000003eefbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb7001ccfc76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).fungible_part;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).non_fungible_part;
    REQUIRE(ft.amount == chain::amount_t{252});
    REQUIRE(nft.commitment.size() == 1);
    REQUIRE(encode_base16(nft.commitment) == "cc");
    REQUIRE(nft.capability == chain::capability_t::none); // immutable

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - immutable NFT 2-byte commitment - FT amount 253", "[output]") {
    auto const data = to_chunk("203005000000000041efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb7002ccccfdfd0076a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).fungible_part;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).non_fungible_part;
    REQUIRE(ft.amount == chain::amount_t{253});
    REQUIRE(nft.commitment.size() == 2);
    REQUIRE(encode_base16(nft.commitment) == "cccc");
    REQUIRE(nft.capability == chain::capability_t::none); // immutable

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - immutable NFT 10-byte commitment - FT amount 65535", "[output]") {
    auto const data = to_chunk("203005000000000049efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb700accccccccccccccccccccfdffff76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).fungible_part;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).non_fungible_part;
    REQUIRE(ft.amount == chain::amount_t{65535});
    REQUIRE(nft.commitment.size() == 10);
    REQUIRE(encode_base16(nft.commitment) == "cccccccccccccccccccc");
    REQUIRE(nft.capability == chain::capability_t::none); // immutable

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - immutable NFT 40-byte commitment - FT amount 65536", "[output]") {
    auto const data = to_chunk("203005000000000069efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb7028ccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccfe0000010076a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).fungible_part;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).non_fungible_part;
    REQUIRE(ft.amount == chain::amount_t{65536});
    REQUIRE(nft.commitment.size() == 40);
    REQUIRE(encode_base16(nft.commitment) == "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    REQUIRE(nft.capability == chain::capability_t::none); // immutable

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with just mutable NFT 0-byte commitment", "[output]") {
    auto const data = to_chunk("20300500000000003befbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb2176a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - mutable NFT 0-byte commitment - FT amount 4294967295", "[output]") {
    auto const data = to_chunk("203005000000000040efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb31feffffffff76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).fungible_part;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).non_fungible_part;
    REQUIRE(ft.amount == chain::amount_t{4294967295});
    REQUIRE(nft.commitment.size() == 0);
    REQUIRE(nft.capability == chain::capability_t::mut); // mutable

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with just mutable NFT 1-byte commitment", "[output]") {
    auto const data = to_chunk("20300500000000003defbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb6101cc76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - mutable NFT 1-byte commitment - FT amount 4294967296", "[output]") {
    auto const data = to_chunk("203005000000000046efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb7101ccff000000000100000076a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).fungible_part;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).non_fungible_part;
    REQUIRE(ft.amount == chain::amount_t{4294967296});
    REQUIRE(nft.commitment.size() == 1);
    REQUIRE(encode_base16(nft.commitment) == "cc");
    REQUIRE(nft.capability == chain::capability_t::mut); // mutable

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - mutable NFT 2-byte commitment - FT amount 9223372036854775807", "[output]") {
    auto const data = to_chunk("203005000000000047efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb7102ccccffffffffffffffff7f76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).fungible_part;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).non_fungible_part;
    REQUIRE(ft.amount == chain::amount_t{9223372036854775807});
    REQUIRE(nft.commitment.size() == 2);
    REQUIRE(encode_base16(nft.commitment) == "cccc");
    REQUIRE(nft.capability == chain::capability_t::mut); // mutable

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - mutable NFT 10-byte commitment - FT amount 1", "[output]") {
    auto const data = to_chunk("203005000000000047efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb710acccccccccccccccccccc0176a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).fungible_part;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).non_fungible_part;
    REQUIRE(ft.amount == chain::amount_t{1});
    REQUIRE(nft.commitment.size() == 10);
    REQUIRE(encode_base16(nft.commitment) == "cccccccccccccccccccc");
    REQUIRE(nft.capability == chain::capability_t::mut); // mutable

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - mutable NFT 40-byte commitment - FT amount 252", "[output]") {
    auto const data = to_chunk("203005000000000065efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb7128ccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccfc76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).fungible_part;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).non_fungible_part;
    REQUIRE(ft.amount == chain::amount_t{252});
    REQUIRE(nft.commitment.size() == 40);
    REQUIRE(encode_base16(nft.commitment) == "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    REQUIRE(nft.capability == chain::capability_t::mut); // mutable

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with just minting NFT 0-byte commitment", "[output]") {
    auto const data = to_chunk("20300500000000003befbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb2276a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - minting NFT 0-byte commitment - FT amount 253", "[output]") {
    auto const data = to_chunk("20300500000000003eefbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb32fdfd0076a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).fungible_part;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).non_fungible_part;
    REQUIRE(ft.amount == chain::amount_t{253});
    REQUIRE(nft.commitment.size() == 0);
    REQUIRE(nft.capability == chain::capability_t::minting); // minting

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with just minting NFT 1-byte commitment", "[output]") {
    auto const data = to_chunk("20300500000000003defbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb6201cc76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - minting NFT 1-byte commitment - FT amount 65535", "[output]") {
    auto const data = to_chunk("203005000000000040efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb7201ccfdffff76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).fungible_part;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).non_fungible_part;
    REQUIRE(ft.amount == chain::amount_t{65535});
    REQUIRE(nft.commitment.size() == 1);
    REQUIRE(encode_base16(nft.commitment) == "cc");
    REQUIRE(nft.capability == chain::capability_t::minting); // minting

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - minting NFT 2-byte commitment - FT amount 65536", "[output]") {
    auto const data = to_chunk("203005000000000043efbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb7202ccccfe0000010076a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).fungible_part;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).non_fungible_part;
    REQUIRE(ft.amount == chain::amount_t{65536});
    REQUIRE(nft.commitment.size() == 2);
    REQUIRE(encode_base16(nft.commitment) == "cccc");
    REQUIRE(nft.capability == chain::capability_t::minting); // minting

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - minting NFT 10-byte commitment - FT amount 4294967297", "[output]") {
    auto const data = to_chunk("20300500000000004fefbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb720accccccccccccccccccccff010000000100000076a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).fungible_part;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).non_fungible_part;
    REQUIRE(ft.amount == chain::amount_t{4294967297});
    REQUIRE(nft.commitment.size() == 10);
    REQUIRE(encode_base16(nft.commitment) == "cccccccccccccccccccc");
    REQUIRE(nft.capability == chain::capability_t::minting); // minting

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization with both - minting NFT 40-byte commitment - FT amount 9223372036854775807", "[output]") {
    auto const data = to_chunk("20300500000000006defbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb7228ccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccffffffffffffffff7f76a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac"_base16);

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
    auto const& ft = std::get<chain::both_kinds>(instance.token_data().value().data).fungible_part;
    auto const& nft = std::get<chain::both_kinds>(instance.token_data().value().data).non_fungible_part;
    REQUIRE(ft.amount == chain::amount_t{9223372036854775807});
    REQUIRE(nft.commitment.size() == 40);
    REQUIRE(encode_base16(nft.commitment) == "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    REQUIRE(nft.capability == chain::capability_t::minting); // minting

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a914905f933de850988603aafeeb2fd7fce61e66fe5d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

TEST_CASE("output deserialization ...", "[output]") {
    auto const data = to_chunk("e8030000000000003cefb43378d02ca3a5ef93f150d44b3be4f098f103e4336062ee2142d03ddd9ac629100a76a91448a5e322b29f3db7297f4dc744e30bca63a0179d88ac"_base16);

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

    REQUIRE(encode_base16(kth::to_data_chunk(instance.script(), true)) == "1976a91448a5e322b29f3db7297f4dc744e30bca63a0179d88ac");
    REQUIRE(encode_base16(kth::to_data_chunk(instance, true)) == encode_base16(data));
}

// End Test Suite

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("outputs preserve malformed token prefixes for contextual validation", "[output][token_prefix]") {
    for (size_t size : {size_t{1}, size_t{33}, size_t{34}, size_t{35}, size_t{80}}) {
        for (bool wire : {false, true}) {
            DYNAMIC_SECTION("bytes=" << size << ", wire=" << wire) {
                data_chunk bytes(size, 0);
                bytes[0] = 0xef;
                if (size >= 34) bytes[33] = 0x80; // reserved token bit
                if (size == 35) {
                    bytes[33] = 0x10; // amount present
                    bytes[34] = 0xfd; // amount needs bytes outside this output
                }
                chain::output original(1000, chain::script(bytes, false), chain::token_data_opt{});
                auto raw = to_data_chunk(original, wire);
                auto const serialized = raw;
                raw.insert(raw.end(), {0x01, 0x00, 0x51}); // following data is not token data
                byte_reader reader(raw);
                auto parsed = chain::output::from_data(reader, wire);
                REQUIRE(parsed);
                CHECK_FALSE(parsed->token_data());
                CHECK(parsed->script().bytes() == bytes);
                CHECK(to_data_chunk(*parsed, wire) == serialized);
                CHECK(reader.remaining_size() == 3);
            }
        }
    }
}
#endif

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("parsed token prefixes remain distinct from malformed raw prefixes", "[output][token_prefix]") {
    hash_digest category{};
    category[0] = 0x42;
    chain::output token_output(1000, chain::script(data_chunk{0xef}, false),
        chain::token_data_opt{chain::make_fungible(category, 1)});
    auto const raw = to_data_chunk(token_output, true);
    byte_reader reader(raw);
    auto parsed = chain::output::from_data(reader, true);
    REQUIRE(parsed);
    REQUIRE(parsed->token_data());
    CHECK(*parsed == token_output);
    chain::input::list inputs;
    inputs.emplace_back(chain::output_point{category, 0}, chain::script{}, 0xffffffff);
    chain::transaction tx(2, 0, std::move(inputs), chain::output::list{*parsed});
    tx.inputs()[0].previous_output().validation.cache =
        chain::output(2000, chain::script(data_chunk{0x51}, false), chain::token_data_opt{});
    CHECK(tx.validate_tokens(to_flags(machine::upgrade::bch_leibniz)) == error::success);
}

TEST_CASE("malformed token prevouts are rejected before and after Descartes", "[output][token_prefix]") {
    hash_digest h{};
    h[0] = 0x42;
    chain::input::list inputs;
    inputs.emplace_back(chain::output_point{h, 0}, chain::script{}, 0xffffffff);
    chain::transaction tx(2, 0, std::move(inputs),
        chain::output::list{chain::output(1000, chain::script(data_chunk{0x51}, false), chain::token_data_opt{})});
    tx.inputs()[0].previous_output().validation.cache =
        chain::output(2000, chain::script(data_chunk{0xef}, false), chain::token_data_opt{});
    CHECK(tx.validate_tokens(to_flags(machine::upgrade::bch_gauss)) == error::token_pre_activation_input);
    CHECK(tx.validate_tokens(to_flags(machine::upgrade::bch_leibniz)) == error::token_unparseable_input);
}
#endif

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("token decoding rejects noncanonical amount and commitment presence", "[output][token_prefix][token_canonical]") {
    std::vector<data_chunk> const invalid{
        {0x30, 0x00}, // NFT plus an explicitly encoded zero FT amount
        {0x60, 0x00}, // commitment present, but empty
        {0x10, 0xff, 0, 0, 0, 0, 0, 0, 0, 0x80} // 2^63
    };
    for (auto const& suffix : invalid) {
        DYNAMIC_SECTION("suffix=" << encode_base16(suffix)) {
            data_chunk payload(32, 0x42);
            payload.insert(payload.end(), suffix.begin(), suffix.end());
            byte_reader token_reader(payload);
            CHECK_FALSE(chain::token::encoding::from_data(token_reader));
            data_chunk raw_script{0xef};
            raw_script.insert(raw_script.end(), payload.begin(), payload.end());
            raw_script.push_back(0x51);
            chain::output original(1000, chain::script(raw_script, false), chain::token_data_opt{});
            for (bool wire : {false, true}) {
                auto const raw = to_data_chunk(original, wire);
                byte_reader reader(raw);
                auto parsed = chain::output::from_data(reader, wire);
                REQUIRE(parsed);
                CHECK_FALSE(parsed->token_data());
                CHECK(parsed->script().bytes() == raw_script);
                CHECK(to_data_chunk(*parsed, wire) == raw);
            }
        }
    }
}

TEST_CASE("token decoding preserves implicit zero NFT amount and legal encoded boundaries", "[output][token_prefix][token_canonical]") {
    for (data_chunk suffix : {data_chunk{0x20}, data_chunk{0x60, 1, 0x42}, data_chunk{0x10, 1},
            data_chunk{0x10, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f}}) {
        DYNAMIC_SECTION("suffix=" << encode_base16(suffix)) {
            data_chunk payload(32, 0x42);
            payload.insert(payload.end(), suffix.begin(), suffix.end());
            byte_reader reader(payload);
            auto parsed = chain::token::encoding::from_data(reader);
            REQUIRE(parsed);
            CHECK(reader.is_exhausted());
            CHECK(chain::token::encoding::to_data(*parsed) == payload);
        }
    }
}
#endif
