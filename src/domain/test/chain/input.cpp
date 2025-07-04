// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::chain;

data_chunk valid_raw_input = to_chunk(base16_literal(
    "54b755c39207d443fd96a8d12c94446a1c6f66e39c95e894c23418d7501f681b01000"
    "0006b48304502203267910f55f2297360198fff57a3631be850965344370f732950b4"
    "7795737875022100f7da90b82d24e6e957264b17d3e5042bab8946ee5fc676d15d915"
    "da450151d36012103893d5a06201d5cf61400e96fa4a7514fc12ab45166ace618d68b"
    "8066c9c585f9ffffffff"));

// Start Test Suite: input tests

TEST_CASE("input  constructor 1  always  returns default initialized", "[input]") {
    input instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("input  constructor 2  valid input  returns input initialized", "[input]") {
    output_point const previous_output{null_hash, 5434u};
    auto script_data = to_chunk(base16_literal("ece424a6bb6ddf4db592c0faed60685047a361b1"));
    byte_reader reader(script_data);
    auto script_result = script::from_data(reader, false);
    REQUIRE(script_result);
    auto script = *script_result;

    uint32_t sequence = 4568656u;

    input instance(previous_output, script, sequence);
    REQUIRE(instance.is_valid());
    REQUIRE(previous_output == instance.previous_output());
    REQUIRE(script == instance.script());
    REQUIRE(sequence == instance.sequence());
}

TEST_CASE("input  constructor 3  valid input  returns input initialized", "[input]") {
    output_point const previous_output{null_hash, 5434u};
    auto script_data = to_chunk(base16_literal("ece424a6bb6ddf4db592c0faed60685047a361b1"));
    byte_reader reader(script_data);
    auto script_result = script::from_data(reader, false);
    REQUIRE(script_result);
    auto script = *script_result;

    uint32_t sequence = 4568656u;

    auto dup_previous_output = previous_output;
    auto dup_script = script;
    input instance(std::move(dup_previous_output), std::move(dup_script), sequence);

    REQUIRE(instance.is_valid());
    REQUIRE(previous_output == instance.previous_output());
    REQUIRE(script == instance.script());
    REQUIRE(sequence == instance.sequence());
}

TEST_CASE("input  constructor 4  valid input  returns input initialized", "[input]") {
    byte_reader reader(valid_raw_input);
    auto expected_result = input::from_data(reader, true);
    REQUIRE(expected_result);
    auto expected = *expected_result;

    input instance(expected);
    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
}

TEST_CASE("input  constructor 5  valid input  returns input initialized", "[input]") {
    byte_reader reader(valid_raw_input);
    auto expected_result = input::from_data(reader, true);
    REQUIRE(expected_result);
    auto expected = *expected_result;

    input instance(std::move(expected));
    REQUIRE(instance.is_valid());
}

TEST_CASE("input from data insufficient data  failure", "[input]") {
    data_chunk data(2);

    byte_reader reader(data);
    auto result = input::from_data(reader, true);
    REQUIRE(!result);
}

TEST_CASE("input from data valid data  success", "[input]") {
    auto const junk = base16_literal("000000000000005739943a9c29a1955dfae2b3f37de547005bfb9535192e5fb0000000000000005739943a9c29a1955dfae2b3f37de547005bfb9535192e5fb0");

    byte_reader reader(junk);
    auto result = input::from_data(reader, true);
    REQUIRE(result);
}

TEST_CASE("input from data valid input  success", "[input]") {
    byte_reader reader(valid_raw_input);
    auto result = input::from_data(reader, true);
    REQUIRE(result);
    auto const instance = *result;
    REQUIRE(instance.is_valid());
    REQUIRE(instance.serialized_size() == valid_raw_input.size());

    // Re-save and compare against original.
    auto const resave = instance.to_data();
    REQUIRE(resave.size() == valid_raw_input.size());
    REQUIRE(resave == valid_raw_input);
}

TEST_CASE("input  factory from data 2  valid input  success", "[input]") {
    byte_reader reader(valid_raw_input);
    auto result = input::from_data(reader, true);
    REQUIRE(result);
    auto const& instance = *result;
    REQUIRE(instance.is_valid());
    REQUIRE(instance.serialized_size() == valid_raw_input.size());

    // Re-save and compare against original.
    auto const resave = instance.to_data();
    REQUIRE(resave.size() == valid_raw_input.size());
    REQUIRE(resave == valid_raw_input);
}

TEST_CASE("input  factory from data 3  valid input  success", "[input]") {
    byte_reader reader(valid_raw_input);
    auto result = input::from_data(reader, true);
    REQUIRE(result);
    auto const& instance = *result;
    REQUIRE(instance.is_valid());
    REQUIRE(instance.serialized_size() == valid_raw_input.size());

    // Re-save and compare against original.
    auto const resave = instance.to_data();
    REQUIRE(resave.size() == valid_raw_input.size());
    REQUIRE(resave == valid_raw_input);
}

TEST_CASE("input  is final  max input sequence  true", "[input]") {
    input const instance({}, {}, max_input_sequence);
    REQUIRE(instance.is_final());
}

TEST_CASE("input  is final  sequence zero  false", "[input]") {
    input const instance({}, {}, 0);
    REQUIRE( ! instance.is_final());
}

TEST_CASE("input  is locked  enabled block type sequence age equals minimum  false", "[input]") {
    static auto const age = 7u;
    static auto const sequence_enabled_block_type_minimum = age;
    input instance({}, {}, sequence_enabled_block_type_minimum);
    auto& prevout = instance.previous_output().validation;
    prevout.height = 42;
    REQUIRE( ! instance.is_locked(prevout.height + age, 0));
}

TEST_CASE("input  is locked  enabled block type sequence age above minimum  false", "[input]") {
    static auto const age = 7u;
    static auto const sequence_enabled_block_type_minimum = age - 1;
    input instance({}, {}, sequence_enabled_block_type_minimum);
    auto& prevout = instance.previous_output().validation;
    prevout.height = 42;
    REQUIRE( ! instance.is_locked(prevout.height + age, 0));
}

TEST_CASE("input  is locked  enabled block type sequence age below minimum  true", "[input]") {
    static auto const age = 7u;
    static auto const sequence_enabled_block_type_minimum = age + 1;
    input instance({}, {}, sequence_enabled_block_type_minimum);
    auto& prevout = instance.previous_output().validation;
    prevout.height = 42;
    REQUIRE(instance.is_locked(prevout.height + age, 0));
}

TEST_CASE("input  is locked  disabled block type sequence age below minimum  false", "[input]") {
    static auto const age = 7u;
    static auto const sequence_disabled_block_type_minimum = relative_locktime_disabled | (age + 1);
    input instance({}, {}, sequence_disabled_block_type_minimum);
    auto& prevout = instance.previous_output().validation;
    prevout.height = 42;
    REQUIRE( ! instance.is_locked(prevout.height + age, 0));
}

TEST_CASE("input  is locked  enabled time type sequence age equals minimum  false", "[input]") {
    static auto const age = 7u;
    static auto const age_seconds = 7u << relative_locktime_seconds_shift;
    static auto const sequence_enabled_time_type_minimum = relative_locktime_time_locked | age;
    input instance({}, {}, sequence_enabled_time_type_minimum);
    auto& prevout = instance.previous_output().validation;
    prevout.median_time_past = 42;
    REQUIRE( ! instance.is_locked(0, prevout.median_time_past + age_seconds));
}

TEST_CASE("input  is locked  enabled time type sequence age above minimum  false", "[input]") {
    static auto const age = 7u;
    static auto const age_seconds = 7u << relative_locktime_seconds_shift;
    static auto const sequence_enabled_time_type_minimum = relative_locktime_time_locked | (age - 1);
    input instance({}, {}, sequence_enabled_time_type_minimum);
    auto& prevout = instance.previous_output().validation;
    prevout.median_time_past = 42;
    REQUIRE( ! instance.is_locked(0, prevout.median_time_past + age_seconds));
}

TEST_CASE("input  is locked  enabled time type sequence age below minimum  true", "[input]") {
    static auto const age = 7u;
    static auto const age_seconds = 7u << relative_locktime_seconds_shift;
    static auto const sequence_enabled_time_type_minimum = relative_locktime_time_locked | (age + 1);
    input instance({}, {}, sequence_enabled_time_type_minimum);
    auto& prevout = instance.previous_output().validation;
    prevout.median_time_past = 42;
    REQUIRE(instance.is_locked(0, prevout.median_time_past + age_seconds));
}

TEST_CASE("input  is locked  disabled time type sequence age below minimum  false", "[input]") {
    static auto const age = 7u;
    static auto const age_seconds = 7u << relative_locktime_seconds_shift;
    static auto const sequence_disabled_time_type_minimum = relative_locktime_disabled | relative_locktime_time_locked | (age + 1);
    input instance({}, {}, sequence_disabled_time_type_minimum);
    auto& prevout = instance.previous_output().validation;
    prevout.median_time_past = 42;
    REQUIRE( ! instance.is_locked(0, prevout.median_time_past + age_seconds));
}

TEST_CASE("input  signature operations  bip16 inactive  returns script sigops", "[input]") {
    auto const raw_script = to_chunk(base16_literal("02acad"));
    byte_reader reader(raw_script);
    auto result = script::from_data(reader, true);
    REQUIRE(result);
    auto const& script = *result;
    input instance;
    instance.set_script(script);
    REQUIRE(script.sigops(false) == instance.signature_operations(false, false));
}

TEST_CASE("input  signature operations  bip16 active cache empty  returns script sigops", "[input]") {
    auto const raw_script = to_chunk(base16_literal("02acad"));
    byte_reader reader(raw_script);
    auto result = script::from_data(reader, true);
    REQUIRE(result);
    auto const& script = *result;
    input instance;
    instance.set_script(script);
    REQUIRE(script.sigops(false) == instance.signature_operations(true, false));
}

TEST_CASE("input  previous output setter 1  roundtrip  success", "[input]") {
    output_point const value{
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        5434u};

    input instance;
    REQUIRE(value != instance.previous_output());
    instance.set_previous_output(value);
    REQUIRE(value == instance.previous_output());
    auto const& restricted = instance;
    REQUIRE(value == restricted.previous_output());
}

TEST_CASE("input  previous output setter 2  roundtrip  success", "[input]") {
    output_point const value{
        hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
        5434u};

    auto dup_value = value;

    input instance;
    REQUIRE(value != instance.previous_output());
    instance.set_previous_output(std::move(dup_value));
    REQUIRE(value == instance.previous_output());
    auto const& restricted = instance;
    REQUIRE(value == restricted.previous_output());
}

TEST_CASE("input  script setter 1  roundtrip  success", "[input]") {
    auto const data = to_chunk(base16_literal("ece424a6bb6ddf4db592c0faed60685047a361b1"));
    byte_reader reader(data);
    auto result = script::from_data(reader, false);
    REQUIRE(result);
    auto const& value = *result;

    input instance;
    REQUIRE(value != instance.script());
    instance.set_script(value);
    REQUIRE(value == instance.script());
    auto const& restricted = instance;
    REQUIRE(value == restricted.script());
}

TEST_CASE("input  script setter 2  roundtrip  success", "[input]") {
    auto const data = to_chunk(base16_literal("ece424a6bb6ddf4db592c0faed60685047a361b1"));
    byte_reader reader(data);
    auto result = script::from_data(reader, false);
    REQUIRE(result);
    auto const& value = *result;

    auto dup_value = value;
    input instance;
    REQUIRE(value != instance.script());
    instance.set_script(std::move(dup_value));
    REQUIRE(value == instance.script());
    auto const& restricted = instance;
    REQUIRE(value == restricted.script());
}

TEST_CASE("input  sequence  roundtrip  success", "[input]") {
    uint32_t value = 1254u;
    input instance;
    REQUIRE(value != instance.sequence());
    instance.set_sequence(value);
    REQUIRE(value == instance.sequence());
}

TEST_CASE("input  operator assign equals 1  always  matches equivalent", "[input]") {
    byte_reader reader1(valid_raw_input);
    auto result1 = input::from_data(reader1, true);
    REQUIRE(result1);
    auto const& expected = *result1;
    
    byte_reader reader2(valid_raw_input);
    auto result2 = input::from_data(reader2, true);
    REQUIRE(result2);
    input instance = *result2;
    REQUIRE(instance == expected);
}

TEST_CASE("input  operator assign equals 2  always  matches equivalent", "[input]") {
    byte_reader reader(valid_raw_input);
    auto result = input::from_data(reader, true);
    REQUIRE(result);
    auto const expected = std::move(*result);
    input instance;
    instance = expected;
    REQUIRE(instance == expected);
}

TEST_CASE("input  operator boolean equals  duplicates  returns true", "[input]") {
    input alpha;
    input beta;
    byte_reader reader(valid_raw_input);
    auto result = input::from_data(reader, true);
    REQUIRE(result);
    alpha = std::move(*result);
    reader.reset();
    result = input::from_data(reader, true);
    REQUIRE(result);
    beta = std::move(*result);
    REQUIRE(alpha == beta);
}

TEST_CASE("input  operator boolean equals  differs  returns false", "[input]") {
    input alpha;
    input beta;
    byte_reader reader(valid_raw_input);
    auto result = input::from_data(reader, true);
    REQUIRE(result);
    alpha = std::move(*result);
    REQUIRE(alpha != beta);
}

TEST_CASE("input  operator boolean not equals  duplicates  returns false", "[input]") {
    input alpha;
    input beta;
    byte_reader reader(valid_raw_input);
    auto result = input::from_data(reader, true);
    REQUIRE(result);
    alpha = std::move(*result);
    reader.reset();
    result = input::from_data(reader, true);
    REQUIRE(result);
    beta = std::move(*result);
    REQUIRE(alpha == beta);
}

TEST_CASE("input  operator boolean not equals  differs  returns true", "[input]") {
    input alpha;
    input beta;
    byte_reader reader(valid_raw_input);
    auto result = input::from_data(reader, true);
    REQUIRE(result);
    alpha = std::move(*result);
    REQUIRE(alpha != beta);
}

// End Test Suite
