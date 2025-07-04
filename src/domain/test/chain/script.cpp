// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/infrastructure/machine/sighash_algorithm.hpp>

#include <sstream>
#include "script.hpp"

using namespace kth;
using namespace kd;
using namespace kth::domain::chain;
using namespace kth::domain::machine;
using namespace kth::infrastructure::machine;

// Helper function to expand a single fork into OR of all forks up to that bit position
// If bit 14 is set (bch_euclid), returns OR of bits 0-14
uint32_t expand_forks(uint32_t highest_fork) {
    if (highest_fork == 0) {
        return 0;
    }
    
    // Find the highest set bit position
    uint32_t highest_bit = 0;
    uint32_t temp = highest_fork;
    while (temp > 1) {
        temp >>= 1;
        highest_bit++;
    }
    
    // Create a mask with all bits from 0 to highest_bit set
    // For example, if highest_bit is 14, this creates 0x00007FFF (bits 0-14 set)
    uint32_t mask = (1U << (highest_bit + 1)) - 1;
    
    return mask;
}

#define SCRIPT_RETURN "return"
#define SCRIPT_RETURN_EMPTY "return []"
#define SCRIPT_RETURN_80 "return [0001020304050607080900010203040506070809000102030405060708090001020304050607080900010203040506070809000102030405060708090001020304050607080900010203040506070809]"
#define SCRIPT_RETURN_81 "return [0001020304050607080900010203040506070809000102030405060708090001020304050607080900010203040506070809000102030405060708090001020304050607080900010203040506070809FF]"

#define SCRIPT_0_OF_3_MULTISIG "0 [03dcfd9e580de35d8c2060d76dbf9e5561fe20febd2e64380e860a4d59f15ac864] [02440e0304bf8d32b2012994393c6a477acf238dd6adb4c3cef5bfa72f30c9861c] [03624505c6cc3967352cce480d8550490dd68519cd019066a4c302fdfb7d1c9934] 3 checkmultisig"
#define SCRIPT_1_OF_3_MULTISIG "1 [03dcfd9e580de35d8c2060d76dbf9e5561fe20febd2e64380e860a4d59f15ac864] [02440e0304bf8d32b2012994393c6a477acf238dd6adb4c3cef5bfa72f30c9861c] [03624505c6cc3967352cce480d8550490dd68519cd019066a4c302fdfb7d1c9934] 3 checkmultisig"
#define SCRIPT_2_OF_3_MULTISIG "2 [03dcfd9e580de35d8c2060d76dbf9e5561fe20febd2e64380e860a4d59f15ac864] [02440e0304bf8d32b2012994393c6a477acf238dd6adb4c3cef5bfa72f30c9861c] [03624505c6cc3967352cce480d8550490dd68519cd019066a4c302fdfb7d1c9934] 3 checkmultisig"
#define SCRIPT_3_OF_3_MULTISIG "3 [03dcfd9e580de35d8c2060d76dbf9e5561fe20febd2e64380e860a4d59f15ac864] [02440e0304bf8d32b2012994393c6a477acf238dd6adb4c3cef5bfa72f30c9861c] [03624505c6cc3967352cce480d8550490dd68519cd019066a4c302fdfb7d1c9934] 3 checkmultisig"
#define SCRIPT_4_OF_3_MULTISIG "4 [03dcfd9e580de35d8c2060d76dbf9e5561fe20febd2e64380e860a4d59f15ac864] [02440e0304bf8d32b2012994393c6a477acf238dd6adb4c3cef5bfa72f30c9861c] [03624505c6cc3967352cce480d8550490dd68519cd019066a4c302fdfb7d1c9934] 3 checkmultisig"

#define SCRIPT_16_OF_16_MULTISIG                                            \
    "16 "                                                                   \
    "[03dcfd9e580de35d8c2060d76dbf9e5561fe20febd2e64380e860a4d59f15ac864] " \
    "[02440e0304bf8d32b2012994393c6a477acf238dd6adb4c3cef5bfa72f30c9861c] " \
    "[03624505c6cc3967352cce480d8550490dd68519cd019066a4c302fdfb7d1c9934] " \
    "[03dcfd9e580de35d8c2060d76dbf9e5561fe20febd2e64380e860a4d59f15ac864] " \
    "[02440e0304bf8d32b2012994393c6a477acf238dd6adb4c3cef5bfa72f30c9861c] " \
    "[03624505c6cc3967352cce480d8550490dd68519cd019066a4c302fdfb7d1c9934] " \
    "[03dcfd9e580de35d8c2060d76dbf9e5561fe20febd2e64380e860a4d59f15ac864] " \
    "[02440e0304bf8d32b2012994393c6a477acf238dd6adb4c3cef5bfa72f30c9861c] " \
    "[03624505c6cc3967352cce480d8550490dd68519cd019066a4c302fdfb7d1c9934] " \
    "[03dcfd9e580de35d8c2060d76dbf9e5561fe20febd2e64380e860a4d59f15ac864] " \
    "[02440e0304bf8d32b2012994393c6a477acf238dd6adb4c3cef5bfa72f30c9861c] " \
    "[03624505c6cc3967352cce480d8550490dd68519cd019066a4c302fdfb7d1c9934] " \
    "[03dcfd9e580de35d8c2060d76dbf9e5561fe20febd2e64380e860a4d59f15ac864] " \
    "[02440e0304bf8d32b2012994393c6a477acf238dd6adb4c3cef5bfa72f30c9861c] " \
    "[03624505c6cc3967352cce480d8550490dd68519cd019066a4c302fdfb7d1c9934] " \
    "[03dcfd9e580de35d8c2060d76dbf9e5561fe20febd2e64380e860a4d59f15ac864] " \
    "16 checkmultisig"

#define SCRIPT_17_OF_17_MULTISIG                                            \
    "[17] "                                                                 \
    "[03dcfd9e580de35d8c2060d76dbf9e5561fe20febd2e64380e860a4d59f15ac864] " \
    "[02440e0304bf8d32b2012994393c6a477acf238dd6adb4c3cef5bfa72f30c9861c] " \
    "[03624505c6cc3967352cce480d8550490dd68519cd019066a4c302fdfb7d1c9934] " \
    "[03dcfd9e580de35d8c2060d76dbf9e5561fe20febd2e64380e860a4d59f15ac864] " \
    "[02440e0304bf8d32b2012994393c6a477acf238dd6adb4c3cef5bfa72f30c9861c] " \
    "[03624505c6cc3967352cce480d8550490dd68519cd019066a4c302fdfb7d1c9934] " \
    "[03dcfd9e580de35d8c2060d76dbf9e5561fe20febd2e64380e860a4d59f15ac864] " \
    "[02440e0304bf8d32b2012994393c6a477acf238dd6adb4c3cef5bfa72f30c9861c] " \
    "[03624505c6cc3967352cce480d8550490dd68519cd019066a4c302fdfb7d1c9934] " \
    "[03dcfd9e580de35d8c2060d76dbf9e5561fe20febd2e64380e860a4d59f15ac864] " \
    "[02440e0304bf8d32b2012994393c6a477acf238dd6adb4c3cef5bfa72f30c9861c] " \
    "[03624505c6cc3967352cce480d8550490dd68519cd019066a4c302fdfb7d1c9934] " \
    "[03dcfd9e580de35d8c2060d76dbf9e5561fe20febd2e64380e860a4d59f15ac864] " \
    "[02440e0304bf8d32b2012994393c6a477acf238dd6adb4c3cef5bfa72f30c9861c] " \
    "[03624505c6cc3967352cce480d8550490dd68519cd019066a4c302fdfb7d1c9934] " \
    "[03dcfd9e580de35d8c2060d76dbf9e5561fe20febd2e64380e860a4d59f15ac864] " \
    "[02440e0304bf8d32b2012994393c6a477acf238dd6adb4c3cef5bfa72f30c9861c] " \
    "16 checkmultisig"

// Test helpers.
//------------------------------------------------------------------------------

std::optional<transaction> new_tx(script_test const& test) {
    // Parse input script from string.
    script input_script;
    if ( ! input_script.from_string(test.input)) {
        return std::nullopt;
    }

    // Parse output script from string.
    script output_script;
    if ( ! output_script.from_string(test.output)) {
        return std::nullopt;
    }

    // Assign output script to input's prevout validation metadata.
    output_point outpoint;
    outpoint.validation.cache.set_script(std::move(output_script));

    // Cosntruct transaction with one input and no outputs.
    return transaction{
        test.version,
        test.locktime,
        input::list{
            input{
                std::move(outpoint),
                std::move(input_script),
                test.input_sequence
            }
        },
        output::list{}};
}

std::optional<transaction> new_tx_bchn(bchn_script_test const& test) {
    // Parse input script from string.
    script input_script;
    if ( ! input_script.from_string(test.script_sig)) {
        std::cout << "Failed to parse input script: " << test.script_sig << std::endl;
        return std::nullopt;
    }

    // Parse output script from string.
    script output_script;
    if ( ! output_script.from_string(test.script_pub_key)) {      
        std::cout << "Failed to parse output script: " << test.script_pub_key << std::endl;  
        return std::nullopt;
    }

    // Assign output script to input's prevout validation metadata.
    output_point outpoint;
    outpoint.validation.cache.set_script(std::move(output_script));

    // Cosntruct transaction with one input and no outputs.
    return transaction{
        0, //test.version,
        0, //test.locktime,
        input::list{
            input{
                std::move(outpoint),
                std::move(input_script),
                0xffffffff // Use SEQUENCE_FINAL like BCHN
            }
        },
        output::list{}};
}

std::string test_name(script_test const& test) {
    std::stringstream out;
    out << "input: \"" << test.input << "\" "
        << "prevout: \"" << test.output << "\" "
        << "("
        << test.input_sequence << ", "
        << test.locktime << ", "
        << test.version
        << ")";
    return out.str();
}

std::string test_name_bchn(bchn_script_test const& test) {
    std::stringstream out;
    out << "input: \"" << test.script_sig << "\" "
        << "prevout: \"" << test.script_pub_key << "\" "
        << "forks: " << test.forks;
    return out.str();
}

// Start Test Suite: script tests

// Serialization tests.
//------------------------------------------------------------------------------

TEST_CASE("script one hash literal same", "[script]") {
    static auto const hash_one = hash_literal("0000000000000000000000000000000000000000000000000000000000000001");
    static hash_digest const one_hash{{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
    REQUIRE(one_hash == hash_one);
}

TEST_CASE("script from data testnet 119058 invalid op codes success", "[script]") {
    auto const raw_script = to_chunk(base16_literal("0130323066643366303435313438356531306633383837363437356630643265396130393739343332353534313766653139316438623963623230653430643863333030326431373463336539306366323433393231383761313037623634373337633937333135633932393264653431373731636565613062323563633534353732653302ae"));

    script parsed;
    byte_reader reader(raw_script);
    auto result = script::from_data(reader, false);
    REQUIRE(result);
    parsed = std::move(*result);
}

TEST_CASE("script from data parse success", "[script]") {
    auto const raw_script = to_chunk(base16_literal("3045022100ff1fc58dbd608e5e05846a8e6b45a46ad49878aef6879ad1a7cf4c5a7f853683022074a6a10f6053ab3cddc5620d169c7374cd42c1416c51b9744db2c8d9febfb84d01"));

    byte_reader reader(raw_script);
    auto result = script::from_data(reader, true);
    REQUIRE(result);
    auto const parsed = std::move(*result);
}

TEST_CASE("script from data to data roundtrips", "[script]") {
    auto const normal_output_script = to_chunk(base16_literal("76a91406ccef231c2db72526df9338894ccf9355e8f12188ac"));

    byte_reader reader(normal_output_script);
    auto result = script::from_data(reader, false);
    REQUIRE(result);
    auto const script = std::move(*result);

    REQUIRE(script.is_valid());
    REQUIRE(script.operations().size() == 5u);
    REQUIRE(script.serialized_size(false) == 25u);
    REQUIRE(script.serialized_size(true) == 26u);
    REQUIRE(script.sigops(false) == 1u);
    REQUIRE(script.sigops(true) == 1u);
    REQUIRE(script.pattern() == script_pattern::pay_public_key_hash);

    auto const roundtrip = script.to_data(false);
    REQUIRE(roundtrip == normal_output_script);
}

TEST_CASE("script from data to data weird roundtrips", "[script]") {
    auto const weird_raw_script = to_chunk(base16_literal(
        "0c49206c69656b20636174732e483045022100c7387f64e1f4"
        "cf654cae3b28a15f7572106d6c1319ddcdc878e636ccb83845"
        "e30220050ebf440160a4c0db5623e0cb1562f46401a7ff5b87"
        "7aa03415ae134e8c71c901534d4f0176519c6375522103b124"
        "c48bbff7ebe16e7bd2b2f2b561aa53791da678a73d2777cc1c"
        "a4619ab6f72103ad6bb76e00d124f07a22680e39debd4dc4bd"
        "b1aa4b893720dd05af3c50560fdd52af67529c63552103b124"
        "c48bbff7ebe16e7bd2b2f2b561aa53791da678a73d2777cc1c"
        "a4619ab6f721025098a1d5a338592bf1e015468ec5a8fafc1f"
        "c9217feb5cb33597f3613a2165e9210360cfabc01d52eaaeb3"
        "976a5de05ff0cfa76d0af42d3d7e1b4c233ee8a00655ed2103"
        "f571540c81fd9dbf9622ca00cfe95762143f2eab6b65150365"
        "bb34ac533160432102bc2b4be1bca32b9d97e2d6fb255504f4"
        "bc96e01aaca6e29bfa3f8bea65d8865855af672103ad6bb76e"
        "00d124f07a22680e39debd4dc4bdb1aa4b893720dd05af3c50"
        "560fddada820a4d933888318a23c28fb5fc67aca8530524e20"
        "74b1d185dbf5b4db4ddb0642848868685174519c6351670068"));

    script weird;
    byte_reader reader(weird_raw_script);
    auto result = script::from_data(reader, false);
    REQUIRE(result);
    weird = std::move(*result);

    auto const roundtrip_result = weird.to_data(false);
    REQUIRE(roundtrip_result == weird_raw_script);
}

TEST_CASE("script factory from data chunk test", "[script]") {
    auto const raw = to_chunk(base16_literal("76a914fc7b44566256621affb1541cc9d59f08336d276b88ac"));
    byte_reader reader(raw);
    auto const result_exp = script::from_data(reader, false);
    REQUIRE(result_exp);
    auto const instance = std::move(*result_exp);
    REQUIRE(instance.is_valid());
}



TEST_CASE("script from data  first byte invalid wire code  success", "[script]") {
    auto const raw = to_chunk(base16_literal(
        "bb566a54e38193e381aee4b896e7958ce381afe496e4babae381abe38288e381"
        "a3e381a6e7ac91e9a194e38292e5a5aae3828fe3828ce3828be7bea9e58b99e3"
        "8292e8a8ade38191e381a6e381afe38184e381aae38184"));

    script instance;
    byte_reader reader(raw);
    auto result = script::from_data(reader, false);
    REQUIRE(result);
    instance = std::move(*result);
}

TEST_CASE("script from data  internal invalid wire code  success", "[script]") {
    auto const raw = to_chunk(base16_literal(
        "566a54e38193e381aee4b896e7958ce381afe4bb96e4babae381abe38288e381"
        "a3e381a6e7ac91e9a194e38292e5a5aae3828fe3828ce3828be7bea9e58b99e3"
        "8292e8a8ade38191e381a6e381afe38184e381aae38184"));

    script instance;
    byte_reader reader(raw);
    auto result = script::from_data(reader, false);
    REQUIRE(result);
    instance = std::move(*result);
}

TEST_CASE("script from string  empty  success", "[script]") {
    script instance;
    REQUIRE(instance.from_string(""));
    REQUIRE(instance.operations().empty());
}

TEST_CASE("script from string  two of three multisig  success", "[script]") {
    script instance;
    REQUIRE(instance.from_string(SCRIPT_2_OF_3_MULTISIG));
    auto const& ops = instance.operations();
    REQUIRE(ops.size() == 6u);
    REQUIRE(ops[0] == opcode::push_positive_2);
    REQUIRE(ops[1].to_string(rule_fork::no_rules) == "[03dcfd9e580de35d8c2060d76dbf9e5561fe20febd2e64380e860a4d59f15ac864]");
    REQUIRE(ops[2].to_string(rule_fork::no_rules) == "[02440e0304bf8d32b2012994393c6a477acf238dd6adb4c3cef5bfa72f30c9861c]");
    REQUIRE(ops[3].to_string(rule_fork::no_rules) == "[03624505c6cc3967352cce480d8550490dd68519cd019066a4c302fdfb7d1c9934]");
    REQUIRE(ops[4] == opcode::push_positive_3);
    REQUIRE(ops[5] == opcode::checkmultisig);
}

TEST_CASE("script empty  default  true", "[script]") {
    script instance;
    REQUIRE(instance.empty());
}

TEST_CASE("script empty  empty operations  true", "[script]") {
    script instance(operation::list{});
    REQUIRE(instance.empty());
}

TEST_CASE("script empty  non empty  false", "[script]") {
    script instance(script::to_null_data_pattern(data_chunk{42u}));
    REQUIRE( ! instance.empty());
}

TEST_CASE("script clear  non empty  empty", "[script]") {
    script instance(script::to_null_data_pattern(data_chunk{42u}));
    REQUIRE( ! instance.empty());

    instance.clear();
    REQUIRE(instance.empty());
}

// Pattern matching tests.
//------------------------------------------------------------------------------

// null_data

TEST_CASE("script pattern  null data return only  non standard", "[script]") {
    script instance;
    instance.from_string(SCRIPT_RETURN);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.output_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.input_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.pattern() == infrastructure::machine::script_pattern::non_standard);
}

TEST_CASE("script pattern  null data empty  null data", "[script]") {
    script instance;
    instance.from_string(SCRIPT_RETURN_EMPTY);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.output_pattern() == infrastructure::machine::script_pattern::null_data);
    REQUIRE(instance.input_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.pattern() == infrastructure::machine::script_pattern::null_data);
}

TEST_CASE("script pattern  null data 80 bytes  null data", "[script]") {
    script instance;
    instance.from_string(SCRIPT_RETURN_80);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.output_pattern() == infrastructure::machine::script_pattern::null_data);
    REQUIRE(instance.input_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.pattern() == infrastructure::machine::script_pattern::null_data);
}

TEST_CASE("script pattern  null data 81 bytes  non standard", "[script]") {
    script instance;
    instance.from_string(SCRIPT_RETURN_81);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.output_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.input_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.pattern() == infrastructure::machine::script_pattern::non_standard);
}

// pay_multisig

TEST_CASE("script pattern  0 of 3 multisig  non standard", "[script]") {
    script instance;
    instance.from_string(SCRIPT_0_OF_3_MULTISIG);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.output_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.input_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.pattern() == infrastructure::machine::script_pattern::non_standard);
}

TEST_CASE("script pattern  1 of 3 multisig  pay multisig", "[script]") {
    script instance;
    instance.from_string(SCRIPT_1_OF_3_MULTISIG);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.output_pattern() == infrastructure::machine::script_pattern::pay_multisig);
    REQUIRE(instance.input_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.pattern() == infrastructure::machine::script_pattern::pay_multisig);
}

TEST_CASE("script pattern  2 of 3 multisig  pay multisig", "[script]") {
    script instance;
    instance.from_string(SCRIPT_2_OF_3_MULTISIG);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.output_pattern() == infrastructure::machine::script_pattern::pay_multisig);
    REQUIRE(instance.input_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.pattern() == infrastructure::machine::script_pattern::pay_multisig);
}

TEST_CASE("script pattern  3 of 3 multisig  pay multisig", "[script]") {
    script instance;
    instance.from_string(SCRIPT_3_OF_3_MULTISIG);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.output_pattern() == infrastructure::machine::script_pattern::pay_multisig);
    REQUIRE(instance.input_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.pattern() == infrastructure::machine::script_pattern::pay_multisig);
}

TEST_CASE("script pattern  4 of 3 multisig  non standard", "[script]") {
    script instance;
    instance.from_string(SCRIPT_4_OF_3_MULTISIG);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.output_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.input_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.pattern() == infrastructure::machine::script_pattern::non_standard);
}

TEST_CASE("script pattern  16 of 16 multisig  pay multisig", "[script]") {
    script instance;
    instance.from_string(SCRIPT_16_OF_16_MULTISIG);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.output_pattern() == infrastructure::machine::script_pattern::pay_multisig);
    REQUIRE(instance.input_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.pattern() == infrastructure::machine::script_pattern::pay_multisig);
}

TEST_CASE("script pattern  17 of 17 multisig  non standard", "[script]") {
    script instance;
    instance.from_string(SCRIPT_17_OF_17_MULTISIG);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.output_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.input_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.pattern() == infrastructure::machine::script_pattern::non_standard);
}

// Data-driven tests.
//------------------------------------------------------------------------------

// bip16

TEST_CASE("script bip16 valid", "[script]") {
    for (auto const& test : valid_bip16_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        REQUIRE(tx_exp);
        auto const& tx = *tx_exp;
        REQUIRE_MESSAGE(tx.is_valid(), name);

        // These are valid prior to and after BIP16 activation.
        CHECK_MESSAGE(verify(tx, 0, rule_fork::no_rules) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bip16_rule) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::all_rules) == error::success, name);
    }
}

TEST_CASE("script bip16 invalid", "[script]") {
    for (auto const& test : invalid_bip16_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        REQUIRE(tx_exp);
        auto const& tx = *tx_exp;
        REQUIRE_MESSAGE(tx.is_valid(), name);

        // These are invalid prior to and after BIP16 activation.
        CHECK_MESSAGE(verify(tx, 0, rule_fork::no_rules) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bip16_rule) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::all_rules) != error::success, name);
    }
}

TEST_CASE("script bip16 invalidated", "[script]") {
    for (auto const& test : invalidated_bip16_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        REQUIRE(tx_exp);
        auto const& tx = *tx_exp;
        REQUIRE_MESSAGE(tx.is_valid(), name);

        // These are valid prior to BIP16 activation and invalid after.
        CHECK_MESSAGE(verify(tx, 0, rule_fork::no_rules) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bip16_rule) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::all_rules) != error::success, name);
    }
}

// bip65

TEST_CASE("script bip65 valid", "[script]") {
    for (auto const& test : valid_bip65_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        REQUIRE(tx_exp);
        auto const& tx = *tx_exp;
        REQUIRE_MESSAGE(tx.is_valid(), name);

        // These are valid prior to and after BIP65 activation.
        CHECK_MESSAGE(verify(tx, 0, rule_fork::no_rules) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bip65_rule) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::all_rules) == error::success, name);
    }
}

TEST_CASE("script bip65 invalid", "[script]") {
    for (auto const& test : invalid_bip65_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        REQUIRE(tx_exp);
        auto const& tx = *tx_exp;
        REQUIRE_MESSAGE(tx.is_valid(), name);

        // These are invalid prior to and after BIP65 activation.
        CHECK_MESSAGE(verify(tx, 0, rule_fork::no_rules) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bip65_rule) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::all_rules) != error::success, name);
    }
}

TEST_CASE("script bip65 invalidated", "[script]") {
    for (auto const& test : invalidated_bip65_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        REQUIRE(tx_exp);
        auto const& tx = *tx_exp;
        REQUIRE_MESSAGE(tx.is_valid(), name);

        // These are valid prior to BIP65 activation and invalid after.
        CHECK_MESSAGE(verify(tx, 0, rule_fork::no_rules) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bip65_rule) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::all_rules) != error::success, name);
    }
}

// bip112
TEST_CASE("script bip112 valid", "[script]") {
    for (auto const& test : valid_bip112_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        REQUIRE(tx_exp);
        auto const& tx = *tx_exp;
        REQUIRE_MESSAGE(tx.is_valid(), name);

        // These are valid prior to and after BIP112 activation.
        CHECK_MESSAGE(verify(tx, 0, rule_fork::no_rules) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bip112_rule) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::all_rules) == error::success, name);
    }
}

TEST_CASE("script bip112 invalid", "[script]") {
    for (auto const& test : invalid_bip112_scripts) {
               auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        REQUIRE(tx_exp);
        auto const& tx = *tx_exp;
 REQUIRE_MESSAGE(tx.is_valid(), name);

        // These are invalid prior to and after BIP112 activation.
        CHECK_MESSAGE(verify(tx, 0, rule_fork::no_rules) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bip112_rule) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::all_rules) != error::success, name);
    }
}

TEST_CASE("script bip112 invalidated", "[script]") {
    for (auto const& test : invalidated_bip112_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        REQUIRE(tx_exp);
        auto const& tx = *tx_exp;
        REQUIRE_MESSAGE(tx.is_valid(), name);

        // These are valid prior to BIP112 activation and invalid after.
        CHECK_MESSAGE(verify(tx, 0, rule_fork::no_rules) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bip112_rule) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::all_rules) != error::success, name);
    }
}

// context free: multisig
TEST_CASE("script multisig valid", "[script]") {
    for (auto const& test : valid_multisig_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        REQUIRE(tx_exp);
        auto const& tx = *tx_exp;
        REQUIRE_MESSAGE(tx.is_valid(), name);

        // These are always valid.
        // These are scripts potentially affected by bip66 (but should not be).
        CHECK_MESSAGE(verify(tx, 0, rule_fork::no_rules) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bip66_rule) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::all_rules) == error::success, name);
    }
}

TEST_CASE("script multisig invalid", "[script]") {
    for (auto const& test : invalid_multisig_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        REQUIRE(tx_exp);
        auto const& tx = *tx_exp;
        REQUIRE_MESSAGE(tx.is_valid(), name);

        // These are always invalid.
        // These are scripts potentially affected by bip66 (but should not be).
        CHECK_MESSAGE(verify(tx, 0, rule_fork::no_rules) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bip66_rule) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::all_rules) != error::success, name);
    }
}

// context free: other

TEST_CASE("script context free valid", "[script]") {
    for (auto const& test : valid_context_free_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        REQUIRE(tx_exp);
        auto const& tx = *tx_exp;
        REQUIRE_MESSAGE(tx.is_valid(), name);

        // These are always valid.
        CHECK_MESSAGE(verify(tx, 0, rule_fork::no_rules) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::all_rules) == error::success, name);
    }
}

TEST_CASE("script context free invalid", "[script]") {
    for (auto const& test : invalid_context_free_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        REQUIRE(bool(tx_exp));
        auto const& tx = *tx_exp;
        REQUIRE_MESSAGE(tx.is_valid(), name);

        // These are always invalid.
        CHECK_MESSAGE(verify(tx, 0, rule_fork::no_rules) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::all_rules) != error::success, name);
    }
}

// bch_pythagoras - May 2025
//------------------------------------------------------------------------------

TEST_CASE("script bch_pythagoras invalidated", "[script]") {
    for (auto const& test : invalidated_bch_pythagoras_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        REQUIRE(tx_exp);
        auto const& tx = *tx_exp;
        REQUIRE_MESSAGE(tx.is_valid(), name);

        // These are valid before bch_pythagoras activation
        CHECK_MESSAGE(verify(tx, 0, rule_fork::no_rules) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bch_daa_cw144) == error::success, name);
        
        // These become P2SH32 patterns after bch_pythagoras
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bch_pythagoras) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::all_rules) != error::success, name);
    }
}

TEST_CASE("script bch_pythagoras validated", "[script]") {
    for (auto const& test : validated_bch_pythagoras_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        REQUIRE(tx_exp);
        auto const& tx = *tx_exp;
        REQUIRE_MESSAGE(tx.is_valid(), name);

        // These are invalid before bch_pythagoras activation
        CHECK_MESSAGE(verify(tx, 0, rule_fork::no_rules) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bch_daa_cw144) != error::success, name);
        
        // These become valid after bch_pythagoras activation
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bch_pythagoras) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::all_rules) == error::success, name);
    }
}

// bch_gauss - May 2022
//------------------------------------------------------------------------------

TEST_CASE("script bch_gauss invalidated", "[script]") {
    for (auto const& test : invalidated_bch_gauss_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        REQUIRE(tx_exp);
        auto const& tx = *tx_exp;
        REQUIRE_MESSAGE(tx.is_valid(), name);

        // These are valid before bch_gauss activation (context-free hash256).
        CHECK_MESSAGE(verify(tx, 0, rule_fork::no_rules) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bch_euler) == error::success, name);
        
        // These become P2SH32 patterns after bch_gauss activation and fail.
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bch_gauss) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::all_rules) != error::success, name);
    }
}

TEST_CASE("script bch_gauss validated", "[script]") {
    for (auto const& test : validated_bch_gauss_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        REQUIRE(tx_exp);
        auto const& tx = *tx_exp;
        REQUIRE_MESSAGE(tx.is_valid(), name);

        // These are invalid before bch_gauss activation (32-bit arithmetic limits).
        CHECK_MESSAGE(verify(tx, 0, rule_fork::no_rules) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bch_euler) != error::success, name);
        
        // These become valid after bch_gauss activation with 64-bit arithmetic.
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bch_gauss) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::all_rules) == error::success, name);
    }
}

// bch_galois - May 2025
//------------------------------------------------------------------------------

TEST_CASE("script bch_galois invalidated", "[script]") {
    for (auto const& test : invalidated_bch_galois_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        REQUIRE(tx_exp);
        auto const& tx = *tx_exp;
        REQUIRE_MESSAGE(tx.is_valid(), name);

        // These are valid before bch_galois activation (context-free hash256).
        CHECK_MESSAGE(verify(tx, 0, rule_fork::no_rules) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bch_lobachevski) == error::success, name);
        
        // These become P2SH32 patterns after bch_galois activation and fail.
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bch_galois) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::all_rules) != error::success, name);
    }
}

TEST_CASE("script bch_galois validated", "[script]") {
    for (auto const& test : validated_bch_galois_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        REQUIRE(tx_exp);
        auto const& tx = *tx_exp;
        REQUIRE_MESSAGE(tx.is_valid(), name);

        // These are invalid before bch_galois activation (32-bit arithmetic limits).
        CHECK_MESSAGE(verify(tx, 0, rule_fork::no_rules) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bch_lobachevski) != error::success, name);
        
        // These become valid after bch_galois activation with 64-bit arithmetic.
        CHECK_MESSAGE(verify(tx, 0, rule_fork::bch_galois) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, rule_fork::all_rules) == error::success, name);
    }
}

// Construction failure tests.
//------------------------------------------------------------------------------

TEST_CASE("script construction failures", "[script]") {
    for (auto const& test : invalid_construction_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        CHECK_MESSAGE( ! tx_exp, name + " - should fail at construction");
        // auto const& tx = *tx_exp;
        // // These scripts fail at construction time due to unrepresentable numbers (>int64_t)
        // // Therefore, the transaction itself is invalid (tx.is_valid() == false)
        // CHECK_MESSAGE(!tx.is_valid(), name + " - should fail at construction");
    }
}

// Checksig tests.
//------------------------------------------------------------------------------

// TEST_CASE("script checksig single uses one hash", "[script]") {
//     // input 315ac7d4c26d69668129cc352851d9389b4a6868f1509c6c8b66bead11e2619f:1
//     data_chunk tx_data;
//     decode_base16(tx_data, "0100000002dc38e9359bd7da3b58386204e186d9408685f427f5e513666db735aa8a6b2169000000006a47304402205d8feeb312478e468d0b514e63e113958d7214fa572acd87079a7f0cc026fc5c02200fa76ea05bf243af6d0f9177f241caf606d01fcfd5e62d6befbca24e569e5c27032102100a1a9ca2c18932d6577c58f225580184d0e08226d41959874ac963e3c1b2feffffffffdc38e9359bd7da3b58386204e186d9408685f427f5e513666db735aa8a6b2169010000006b4830450220087ede38729e6d35e4f515505018e659222031273b7366920f393ee3ab17bc1e022100ca43164b757d1a6d1235f13200d4b5f76dd8fda4ec9fc28546b2df5b1211e8df03210275983913e60093b767e85597ca9397fb2f418e57f998d6afbbc536116085b1cbffffffff0140899500000000001976a914fcc9b36d38cf55d7d5b4ee4dddb6b2c17612f48c88ac00000000");
//     byte_reader reader(tx_data);
//     auto result = transaction::from_data(reader, true);
//     REQUIRE(result);
//     auto const parent_tx = std::move(*result);

//     data_chunk distinguished;
//     decode_base16(distinguished, "30450220087ede38729e6d35e4f515505018e659222031273b7366920f393ee3ab17bc1e022100ca43164b757d1a6d1235f13200d4b5f76dd8fda4ec9fc28546b2df5b1211e8df");

//     data_chunk pubkey;
//     decode_base16(pubkey, "0275983913e60093b767e85597ca9397fb2f418e57f998d6afbbc536116085b1cb");

//     data_chunk script_data;
//     decode_base16(script_data, "76a91433cef61749d11ba2adf091a5e045678177fe3a6d88ac");

//     byte_reader reader2(script_data);
//     auto result2 = script::from_data(reader2, false);
//     REQUIRE(result2);
//     auto const script_code = std::move(*result2);

//     ec_signature signature;
//     static auto const index = 1u;
//     static auto const strict = true;
//     static auto const active_forks = 0u; // No active forks for test
//     REQUIRE(parse_signature(signature, distinguished, strict));
//     REQUIRE(script::check_signature(signature, sighash_algorithm::single, pubkey, script_code, parent_tx, index, active_forks).first);
// }

// TEST_CASE("script checksig normal success", "[script]") {
//     // input 315ac7d4c26d69668129cc352851d9389b4a6868f1509c6c8b66bead11e2619f:0
//     data_chunk tx_data;
//     decode_base16(tx_data, "0100000002dc38e9359bd7da3b58386204e186d9408685f427f5e513666db735aa8a6b2169000000006a47304402205d8feeb312478e468d0b514e63e113958d7214fa572acd87079a7f0cc026fc5c02200fa76ea05bf243af6d0f9177f241caf606d01fcfd5e62d6befbca24e569e5c27032102100a1a9ca2c18932d6577c58f225580184d0e08226d41959874ac963e3c1b2feffffffffdc38e9359bd7da3b58386204e186d9408685f427f5e513666db735aa8a6b2169010000006b4830450220087ede38729e6d35e4f515505018e659222031273b7366920f393ee3ab17bc1e022100ca43164b757d1a6d1235f13200d4b5f76dd8fda4ec9fc28546b2df5b1211e8df03210275983913e60093b767e85597ca9397fb2f418e57f998d6afbbc536116085b1cbffffffff0140899500000000001976a914fcc9b36d38cf55d7d5b4ee4dddb6b2c17612f48c88ac00000000");
//     byte_reader reader(tx_data);
//     auto result = transaction::from_data(reader, true);
//     REQUIRE(result);
//     auto const parent_tx = std::move(*result);

//     data_chunk distinguished;
//     decode_base16(distinguished, "304402205d8feeb312478e468d0b514e63e113958d7214fa572acd87079a7f0cc026fc5c02200fa76ea05bf243af6d0f9177f241caf606d01fcfd5e62d6befbca24e569e5c27");

//     data_chunk pubkey;
//     decode_base16(pubkey, "02100a1a9ca2c18932d6577c58f225580184d0e08226d41959874ac963e3c1b2fe");

//     data_chunk script_data;
//     decode_base16(script_data, "76a914fcc9b36d38cf55d7d5b4ee4dddb6b2c17612f48c88ac");

//     byte_reader reader2(script_data);
//     auto result2 = script::from_data(reader2, false);
//     REQUIRE(result2);
//     auto const script_code = std::move(*result2);

//     ec_signature signature;
//     static auto const index = 0u;
//     static auto const active_forks = 0u; // No active forks for test
//     REQUIRE(parse_signature(signature, distinguished, true));
//     REQUIRE(script::check_signature(signature, sighash_algorithm::single, pubkey, script_code, parent_tx, index, active_forks).first);
// }

// TEST_CASE("script create endorsement  single input single output  expected", "[script]") {
//     data_chunk tx_data;
//     decode_base16(tx_data, "0100000001b3807042c92f449bbf79b33ca59d7dfec7f4cc71096704a9c526dddf496ee0970100000000ffffffff01905f0100000000001976a91418c0bd8d1818f1bf99cb1df2269c645318ef7b7388ac00000000");
//     byte_reader reader(tx_data);
//     auto result = transaction::from_data(reader, true);
//     REQUIRE(result);
//     auto const new_tx = std::move(*result);

//     script prevout_script;
//     REQUIRE(prevout_script.from_string("dup hash160 [88350574280395ad2c3e2ee20e322073d94e5e40] equalverify checksig"));

//     ec_secret const secret = hash_literal("ce8f4b713ffdd2658900845251890f30371856be201cd1f5b3d970f793634333");

//     auto const index = 0u;
//     auto const sighash_type = sighash_algorithm::all;
//     auto const active_forks = rule_fork::bch_uahf; // Test: BCH UAHF fork enabled
//     auto out_result = script::create_endorsement(secret, prevout_script, new_tx, index, sighash_type, active_forks);
//     REQUIRE(out_result.has_value());
    
//     auto const& out = out_result.value();
//     auto const result2 = encode_base16(out);
//     auto const expected = "3044022038a78aea2ea081df81087e6f8694343f7fbbfead6e12715bd068ea7c6ee62209022035dbb33e356dd4c2e4b41055550510b5ccdbc72d7ecc0cefe212f59acf4905a701";

//     REQUIRE(result2 == expected);
// }

TEST_CASE("script create endorsement  single input no output  expected", "[script]") {
    data_chunk tx_data;
    decode_base16(tx_data, "0100000001b3807042c92f449bbf79b33ca59d7dfec7f4cc71096704a9c526dddf496ee0970000000000ffffffff0000000000");
    byte_reader reader(tx_data);
    auto result = transaction::from_data(reader, true);
    REQUIRE(result);
    auto const new_tx = std::move(*result);

    script prevout_script;
    REQUIRE(prevout_script.from_string("dup hash160 [88350574280395ad2c3e2ee20e322073d94e5e40] equalverify checksig"));

    ec_secret const secret = hash_literal("ce8f4b713ffdd2658900845251890f30371856be201cd1f5b3d970f793634333");

    auto const index = 0u;
    auto const sighash_type = sighash_algorithm::all;
    auto const active_forks = rule_fork::bch_uahf; // Enable BCH UAHF fork for proper BCH behavior
    auto out_result = script::create_endorsement(secret, prevout_script, new_tx, index, sighash_type, active_forks);
    REQUIRE(out_result.has_value());
    
    auto const& out = out_result.value();
    auto const result2 = encode_base16(out);
    auto const expected = "304402207b7390c5835b71a874d47f08675688baac0ff2e936761f725b8ec56878b599a20220673f3643dd95f5b7041cd6b78ed0eb32b0a32c8069e61af9ac81bd50e891279e01";
    REQUIRE(result2 == expected);
}

TEST_CASE("script generate signature hash  all  expected", "[script]") {
    data_chunk tx_data;
    decode_base16(tx_data, "0100000001b3807042c92f449bbf79b33ca59d7dfec7f4cc71096704a9c526dddf496ee0970000000000ffffffff0000000000");
    byte_reader reader(tx_data);
    auto result = transaction::from_data(reader, true);
    REQUIRE(result);
    auto const new_tx = std::move(*result);

    script prevout_script;
    REQUIRE(prevout_script.from_string("dup hash160 [88350574280395ad2c3e2ee20e322073d94e5e40] equalverify checksig"));

    endorsement out;
    auto const index = 0u;
    auto const sighash_type = sighash_algorithm::all;
    auto const sighash = script::generate_signature_hash(new_tx, index, prevout_script, sighash_type, 0u);
    auto const result2 = encode_base16(sighash.first);
    auto const expected = "f77b8f47aba71c8347e77810d9b545efc312e985b22f732e5fc8c76a87e89919";
    REQUIRE(result2 == expected);
}

// Ad-hoc test cases.
//-----------------------------------------------------------------------------

// TEST_CASE("script native block 290329 tx valid", "[script]") {
//     //// DEBUG [blockchain] Verify failed [290329] : stack false (find and delete).
//     //// libconsensus : false
//     //// forks        : 1073742030
//     //// outpoint     : ab9805c6d57d7070d9a42c5176e47bb705023e6b67249fb6760880548298e742:0
//     //// script       : a914d8dacdadb7462ae15cd906f1878706d0da8660e687
//     //// inpoint      : 5df1375ffe61ac35ca178ebb0cab9ea26dedbd0e96005dfcee7e379fa513232f:1
//     //// transaction  : 0100000002f9cbafc519425637ba4227f8d0a0b7160b4e65168193d5af39747891de98b5b5000000006b4830450221008dd619c563e527c47d9bd53534a770b102e40faa87f61433580e04e271ef2f960220029886434e18122b53d5decd25f1f4acb2480659fea20aabd856987ba3c3907e0121022b78b756e2258af13779c1a1f37ea6800259716ca4b7f0b87610e0bf3ab52a01ffffffff42e7988254800876b69f24676b3e0205b77be476512ca4d970707dd5c60598ab00000000fd260100483045022015bd0139bcccf990a6af6ec5c1c52ed8222e03a0d51c334df139968525d2fcd20221009f9efe325476eb64c3958e4713e9eefe49bf1d820ed58d2112721b134e2a1a53034930460221008431bdfa72bc67f9d41fe72e94c88fb8f359ffa30b33c72c121c5a877d922e1002210089ef5fc22dd8bfc6bf9ffdb01a9862d27687d424d1fefbab9e9c7176844a187a014c9052483045022015bd0139bcccf990a6af6ec5c1c52ed8222e03a0d51c334df139968525d2fcd20221009f9efe325476eb64c3958e4713e9eefe49bf1d820ed58d2112721b134e2a1a5303210378d430274f8c5ec1321338151e9f27f4c676a008bdf8638d07c0b6be9ab35c71210378d430274f8c5ec1321338151e9f27f4c676a008bdf8638d07c0b6be9ab35c7153aeffffffff01a08601000000000017a914d8dacdadb7462ae15cd906f1878706d0da8660e68700000000

//     static auto const index = 1u;
//     static auto const forks = static_cast<rule_fork>(1073742030);
//     static auto const encoded_script = "a914d8dacdadb7462ae15cd906f1878706d0da8660e687";
//     static auto const encoded_tx = "0100000002f9cbafc519425637ba4227f8d0a0b7160b4e65168193d5af39747891de98b5b5000000006b4830450221008dd619c563e527c47d9bd53534a770b102e40faa87f61433580e04e271ef2f960220029886434e18122b53d5decd25f1f4acb2480659fea20aabd856987ba3c3907e0121022b78b756e2258af13779c1a1f37ea6800259716ca4b7f0b87610e0bf3ab52a01ffffffff42e7988254800876b69f24676b3e0205b77be476512ca4d970707dd5c60598ab00000000fd260100483045022015bd0139bcccf990a6af6ec5c1c52ed8222e03a0d51c334df139968525d2fcd20221009f9efe325476eb64c3958e4713e9eefe49bf1d820ed58d2112721b134e2a1a53034930460221008431bdfa72bc67f9d41fe72e94c88fb8f359ffa30b33c72c121c5a877d922e1002210089ef5fc22dd8bfc6bf9ffdb01a9862d27687d424d1fefbab9e9c7176844a187a014c9052483045022015bd0139bcccf990a6af6ec5c1c52ed8222e03a0d51c334df139968525d2fcd20221009f9efe325476eb64c3958e4713e9eefe49bf1d820ed58d2112721b134e2a1a5303210378d430274f8c5ec1321338151e9f27f4c676a008bdf8638d07c0b6be9ab35c71210378d430274f8c5ec1321338151e9f27f4c676a008bdf8638d07c0b6be9ab35c7153aeffffffff01a08601000000000017a914d8dacdadb7462ae15cd906f1878706d0da8660e68700000000";

//     data_chunk decoded_tx;
//     REQUIRE(decode_base16(decoded_tx, encoded_tx));

//     data_chunk decoded_script;
//     REQUIRE(decode_base16(decoded_script, encoded_script));

//     byte_reader reader(decoded_tx);
//     auto result = transaction::from_data(reader, true);
//     REQUIRE(result);
//     auto const tx = std::move(*result);
//     REQUIRE(tx.inputs().size() > index);

//     auto const& input = tx.inputs()[index];
//     auto& prevout = input.previous_output().validation.cache;

//     prevout.set_script(create<script>(decoded_script, false));
//     REQUIRE(prevout.script().is_valid());

//     ////std::cout << prevout.script().to_string(forks) << std::endl;
//     ////std::cout << input.script().to_string(forks) << std::endl;
//     ////std::cout << input.witness().to_string() << std::endl;

//     auto const result2 = verify(tx, index, forks);
//     REQUIRE(result2.value() == error::success);
// }

// TEST_CASE("script native block 438513 tx valid", "[script]") {
//     //// DEBUG [blockchain] Input validation failed (stack false)
//     //// libconsensus : false
//     //// forks        : 62
//     //// outpoint     : 8e51d775e0896e03149d585c0655b3001da0c55068b0885139ac6ec34cf76ba0:0
//     //// script       : a914faa558780a5767f9e3be14992a578fc1cbcf483087
//     //// inpoint      : 6b7f50afb8448c39f4714a73d2b181d3e3233e84670bdfda8f141db668226c54:0
//     //// transaction  : 0100000001a06bf74cc36eac395188b06850c5a01d00b355065c589d14036e89e075d7518e000000009d483045022100ba555ac17a084e2a1b621c2171fa563bc4fb75cd5c0968153f44ba7203cb876f022036626f4579de16e3ad160df01f649ffb8dbf47b504ee56dc3ad7260af24ca0db0101004c50632102768e47607c52e581595711e27faffa7cb646b4f481fe269bd49691b2fbc12106ad6704355e2658b1756821028a5af8284a12848d69a25a0ac5cea20be905848eb645fd03d3b065df88a9117cacfeffffff0158920100000000001976a9149d86f66406d316d44d58cbf90d71179dd8162dd388ac355e2658

//     static auto const index = 0u;
//     static auto const forks = static_cast<rule_fork>(62);
//     static auto const encoded_script = "a914faa558780a5767f9e3be14992a578fc1cbcf483087";
//     static auto const encoded_tx = "0100000001a06bf74cc36eac395188b06850c5a01d00b355065c589d14036e89e075d7518e000000009d483045022100ba555ac17a084e2a1b621c2171fa563bc4fb75cd5c0968153f44ba7203cb876f022036626f4579de16e3ad160df01f649ffb8dbf47b504ee56dc3ad7260af24ca0db0101004c50632102768e47607c52e581595711e27faffa7cb646b4f481fe269bd49691b2fbc12106ad6704355e2658b1756821028a5af8284a12848d69a25a0ac5cea20be905848eb645fd03d3b065df88a9117cacfeffffff0158920100000000001976a9149d86f66406d316d44d58cbf90d71179dd8162dd388ac355e2658";

//     data_chunk decoded_tx;
//     REQUIRE(decode_base16(decoded_tx, encoded_tx));

//     data_chunk decoded_script;
//     REQUIRE(decode_base16(decoded_script, encoded_script));

//     byte_reader reader(decoded_tx);
//     auto result = transaction::from_data(reader, true);
//     REQUIRE(result);
//     auto const tx = std::move(*result);
//     REQUIRE(tx.inputs().size() > index);

//     auto const& input = tx.inputs()[index];
//     auto& prevout = input.previous_output().validation.cache;

//     // prevout.set_script(create<script>(decoded_script, false));
//     byte_reader reader2(decoded_script);
//     auto result2 = script::from_data(reader2, false);
//     REQUIRE(result2);
//     prevout.set_script(std::move(*result2));

//     REQUIRE(prevout.script().is_valid());

//     auto const result3 = verify(tx, index, forks);
//     REQUIRE(result3.value() == error::success);
// }

TEST_CASE("BCHN script tests", "[script]") {

// struct script_test {
//     std::string script_sig;         ///< The scriptSig (input script)
//     std::string script_pub_key;     ///< The scriptPubKey (output script)  
//     uint32_t forks;                 ///< Active fork rules (OR of kth::domain::machine::rule_fork values)
//     error::error_code_t expected_error; ///< Expected error code
//     std::string comment;            ///< Test description/comment
// };

    // Iterate through all test chunks
    for (size_t chunk_idx = 0; chunk_idx < all_script_test_chunks.size(); ++chunk_idx) {
        DYNAMIC_SECTION("Chunk " + std::to_string(chunk_idx) + " (" + std::to_string(all_script_test_chunks[chunk_idx]->size()) + " tests)") {
            // std::cout << "Testing chunk " << chunk_idx << " (" 
            //           << all_script_test_chunks[chunk_idx]->size() << " tests)..." << std::endl;
            
            for (size_t test_idx = 0; test_idx < all_script_test_chunks[chunk_idx]->size(); ++test_idx) {
                auto const& test = (*all_script_test_chunks[chunk_idx])[test_idx];
                auto const name = test_name_bchn(test);
                
                DYNAMIC_SECTION("Test " + std::to_string(test_idx) + ": " + name) {
                    auto const tx_exp = new_tx_bchn(test);
                    REQUIRE_MESSAGE(bool(tx_exp), name);

                    auto const& tx = *tx_exp;
                    REQUIRE_MESSAGE(tx.is_valid(), name);

                    // Expand the single fork to include all forks up to that bit position
                    auto const expanded_forks = expand_forks(test.forks);
                    REQUIRE_MESSAGE(verify(tx, 0, expanded_forks) == test.expected_error, name);
                }
            }
            
            // std::cout << "Chunk " << chunk_idx << " completed." << std::endl;
        }
    }
}

// End Test Suite
