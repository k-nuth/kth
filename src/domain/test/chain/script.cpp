// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/infrastructure/machine/sighash_algorithm.hpp>

#include <print>
#include <sstream>
#include "script.hpp"

using namespace kth;
using namespace kd;
using namespace kth::domain::chain;
using namespace kth::domain::machine;
using namespace kth::infrastructure::machine;
using kth::domain::to_flags;


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

    // Construct transaction with one input and no outputs.
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

// Parse a script from string. Supports "RAW:hexhex" prefix for raw bytes
// that bypass from_string (e.g. malformed scripts with truncated PUSHDATA).
std::optional<script> parse_script_string(std::string const& str) {
    if (str.starts_with("RAW:") || str.starts_with("raw:")) {
        auto const decoded = decode_base16(str.substr(4));
        if ( ! decoded) return std::nullopt;
        byte_reader reader(*decoded);
        auto result = script::from_data(reader, false);
        if ( ! result) return std::nullopt;
        return script(std::move(*result));
    }
    script s;
    if ( ! s.from_string(str)) return std::nullopt;
    return s;
}

std::optional<transaction> new_tx_bchn(bchn_script_test const& test) {
    // Parse input script from string (or raw hex).
    auto input_script_opt = parse_script_string(test.script_sig);
    if ( ! input_script_opt) {
        std::println("Failed to parse input script: {}", test.script_sig);
        return std::nullopt;
    }
    auto input_script = std::move(*input_script_opt);

    // Parse output script from string (or raw hex).
    auto output_script_opt = parse_script_string(test.script_pub_key);
    if ( ! output_script_opt) {
        std::println("Failed to parse output script: {}", test.script_pub_key);
        return std::nullopt;
    }
    auto output_script = std::move(*output_script_opt);

    // Build crediting transaction (replicates BCHN's BuildCreditingTransaction).
    // version=1, locktime=0, 1 input (script="0 0", prevout=null, seq=FINAL),
    // 1 output (scriptPubKey=output_script, value=amount).
    script credit_input_script;
    credit_input_script.from_string("0 0");

    auto const amount = test.amount;

    transaction credit_tx{
        1, // version
        0, // locktime
        input::list{
            input{
                output_point{null_hash, no_previous_output},
                std::move(credit_input_script),
                0xffffffff
            }
        },
        output::list{
            output{amount, output_script, std::nullopt}
        }};

    // Build spending transaction (replicates BCHN's BuildSpendingTransaction).
    // version=1, locktime=0, 1 input (prevout=hash(credit_tx):0, scriptSig=input_script, seq=FINAL),
    // 1 output (scriptPubKey=empty, value=credit_tx.outputs[0].value).
    output_point outpoint{credit_tx.hash(), 0};
    outpoint.validation.cache.set_script(std::move(output_script));
    outpoint.validation.cache.set_value(amount);

    return transaction{
        1, // version
        0, // locktime
        input::list{
            input{
                std::move(outpoint),
                std::move(input_script),
                0xffffffff
            }
        },
        output::list{
            output{amount, script{}, std::nullopt}
        }};
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
        << "flags: " << test.flags;
    return out.str();
}

// Start Test Suite: script tests

// Serialization tests.
//------------------------------------------------------------------------------

TEST_CASE("script one hash literal same", "[script]") {
    static auto const hash_one = "0000000000000000000000000000000000000000000000000000000000000001"_hash;
    static hash_digest const one_hash{{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
    REQUIRE(one_hash == hash_one);
}

TEST_CASE("script from data testnet 119058 invalid op codes success", "[script]") {
    auto const raw_script = to_chunk("0130323066643366303435313438356531306633383837363437356630643265396130393739343332353534313766653139316438623963623230653430643863333030326431373463336539306366323433393231383761313037623634373337633937333135633932393264653431373731636565613062323563633534353732653302ae"_base16);

    script parsed;
    byte_reader reader(raw_script);
    auto result = script::from_data(reader, false);
    REQUIRE(result);
    parsed = std::move(*result);
}

TEST_CASE("script from data parse success", "[script]") {
    auto const raw_script = to_chunk("3045022100ff1fc58dbd608e5e05846a8e6b45a46ad49878aef6879ad1a7cf4c5a7f853683022074a6a10f6053ab3cddc5620d169c7374cd42c1416c51b9744db2c8d9febfb84d01"_base16);

    byte_reader reader(raw_script);
    auto result = script::from_data(reader, true);
    REQUIRE(result);
    auto const parsed = std::move(*result);
}

TEST_CASE("script from data to data roundtrips", "[script]") {
    auto const normal_output_script = to_chunk("76a91406ccef231c2db72526df9338894ccf9355e8f12188ac"_base16);

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
    auto const weird_raw_script = to_chunk("0c49206c69656b20636174732e483045022100c7387f64e1f4cf654cae3b28a15f7572106d6c1319ddcdc878e636ccb83845e30220050ebf440160a4c0db5623e0cb1562f46401a7ff5b877aa03415ae134e8c71c901534d4f0176519c6375522103b124c48bbff7ebe16e7bd2b2f2b561aa53791da678a73d2777cc1ca4619ab6f72103ad6bb76e00d124f07a22680e39debd4dc4bdb1aa4b893720dd05af3c50560fdd52af67529c63552103b124c48bbff7ebe16e7bd2b2f2b561aa53791da678a73d2777cc1ca4619ab6f721025098a1d5a338592bf1e015468ec5a8fafc1fc9217feb5cb33597f3613a2165e9210360cfabc01d52eaaeb3976a5de05ff0cfa76d0af42d3d7e1b4c233ee8a00655ed2103f571540c81fd9dbf9622ca00cfe95762143f2eab6b65150365bb34ac533160432102bc2b4be1bca32b9d97e2d6fb255504f4bc96e01aaca6e29bfa3f8bea65d8865855af672103ad6bb76e00d124f07a22680e39debd4dc4bdb1aa4b893720dd05af3c50560fddada820a4d933888318a23c28fb5fc67aca8530524e2074b1d185dbf5b4db4ddb0642848868685174519c6351670068"_base16);

    script weird;
    byte_reader reader(weird_raw_script);
    auto result = script::from_data(reader, false);
    REQUIRE(result);
    weird = std::move(*result);

    auto const roundtrip_result = weird.to_data(false);
    REQUIRE(roundtrip_result == weird_raw_script);
}

TEST_CASE("script factory from data chunk test", "[script]") {
    auto const raw = to_chunk("76a914fc7b44566256621affb1541cc9d59f08336d276b88ac"_base16);
    byte_reader reader(raw);
    auto const result_exp = script::from_data(reader, false);
    REQUIRE(result_exp);
    auto const instance = std::move(*result_exp);
    REQUIRE(instance.is_valid());
}



TEST_CASE("script from data - first byte invalid wire code - success", "[script]") {
    auto const raw = to_chunk("bb566a54e38193e381aee4b896e7958ce381afe496e4babae381abe38288e381a3e381a6e7ac91e9a194e38292e5a5aae3828fe3828ce3828be7bea9e58b99e38292e8a8ade38191e381a6e381afe38184e381aae38184"_base16);

    script instance;
    byte_reader reader(raw);
    auto result = script::from_data(reader, false);
    REQUIRE(result);
    instance = std::move(*result);
}

TEST_CASE("script from data - internal invalid wire code - success", "[script]") {
    auto const raw = to_chunk("566a54e38193e381aee4b896e7958ce381afe4bb96e4babae381abe38288e381a3e381a6e7ac91e9a194e38292e5a5aae3828fe3828ce3828be7bea9e58b99e38292e8a8ade38191e381a6e381afe38184e381aae38184"_base16);

    script instance;
    byte_reader reader(raw);
    auto result = script::from_data(reader, false);
    REQUIRE(result);
    instance = std::move(*result);
}

TEST_CASE("script from string - empty - success", "[script]") {
    script instance;
    REQUIRE(instance.from_string(""));
    REQUIRE(instance.operations().empty());
}

TEST_CASE("script from string - two of three multisig - success", "[script]") {
    script instance;
    REQUIRE(instance.from_string(SCRIPT_2_OF_3_MULTISIG));
    auto const& ops = instance.operations();
    REQUIRE(ops.size() == 6u);
    REQUIRE(ops[0] == opcode::push_positive_2);
    REQUIRE(ops[1].to_string(script_flags::no_rules) == "[03dcfd9e580de35d8c2060d76dbf9e5561fe20febd2e64380e860a4d59f15ac864]");
    REQUIRE(ops[2].to_string(script_flags::no_rules) == "[02440e0304bf8d32b2012994393c6a477acf238dd6adb4c3cef5bfa72f30c9861c]");
    REQUIRE(ops[3].to_string(script_flags::no_rules) == "[03624505c6cc3967352cce480d8550490dd68519cd019066a4c302fdfb7d1c9934]");
    REQUIRE(ops[4] == opcode::push_positive_3);
    REQUIRE(ops[5] == opcode::checkmultisig);
}

TEST_CASE("script empty - default - true", "[script]") {
    script instance;
    REQUIRE(instance.empty());
}

TEST_CASE("script empty - empty operations - true", "[script]") {
    script instance(operation::list{});
    REQUIRE(instance.empty());
}

TEST_CASE("script empty - non empty - false", "[script]") {
    script instance(script::to_null_data_pattern(data_chunk{42u}));
    REQUIRE( ! instance.empty());
}

TEST_CASE("script clear - non empty - empty", "[script]") {
    script instance(script::to_null_data_pattern(data_chunk{42u}));
    REQUIRE( ! instance.empty());

    instance.clear();
    REQUIRE(instance.empty());
}

// Pattern matching tests.
//------------------------------------------------------------------------------

// null_data

TEST_CASE("script pattern - null data return only - non standard", "[script]") {
    script instance;
    instance.from_string(SCRIPT_RETURN);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.output_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.input_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.pattern() == infrastructure::machine::script_pattern::non_standard);
}

TEST_CASE("script pattern - null data empty - null data", "[script]") {
    script instance;
    instance.from_string(SCRIPT_RETURN_EMPTY);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.output_pattern() == infrastructure::machine::script_pattern::null_data);
    REQUIRE(instance.input_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.pattern() == infrastructure::machine::script_pattern::null_data);
}

TEST_CASE("script pattern - null data 80 bytes - null data", "[script]") {
    script instance;
    instance.from_string(SCRIPT_RETURN_80);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.output_pattern() == infrastructure::machine::script_pattern::null_data);
    REQUIRE(instance.input_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.pattern() == infrastructure::machine::script_pattern::null_data);
}

TEST_CASE("script pattern - null data 81 bytes - non standard", "[script]") {
    script instance;
    instance.from_string(SCRIPT_RETURN_81);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.output_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.input_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.pattern() == infrastructure::machine::script_pattern::non_standard);
}

// pay_multisig

TEST_CASE("script pattern - 0 of 3 multisig - non standard", "[script]") {
    script instance;
    instance.from_string(SCRIPT_0_OF_3_MULTISIG);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.output_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.input_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.pattern() == infrastructure::machine::script_pattern::non_standard);
}

TEST_CASE("script pattern - 1 of 3 multisig - pay multisig", "[script]") {
    script instance;
    instance.from_string(SCRIPT_1_OF_3_MULTISIG);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.output_pattern() == infrastructure::machine::script_pattern::pay_multisig);
    REQUIRE(instance.input_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.pattern() == infrastructure::machine::script_pattern::pay_multisig);
}

TEST_CASE("script pattern - 2 of 3 multisig - pay multisig", "[script]") {
    script instance;
    instance.from_string(SCRIPT_2_OF_3_MULTISIG);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.output_pattern() == infrastructure::machine::script_pattern::pay_multisig);
    REQUIRE(instance.input_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.pattern() == infrastructure::machine::script_pattern::pay_multisig);
}

TEST_CASE("script pattern - 3 of 3 multisig - pay multisig", "[script]") {
    script instance;
    instance.from_string(SCRIPT_3_OF_3_MULTISIG);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.output_pattern() == infrastructure::machine::script_pattern::pay_multisig);
    REQUIRE(instance.input_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.pattern() == infrastructure::machine::script_pattern::pay_multisig);
}

TEST_CASE("script pattern - 4 of 3 multisig - non standard", "[script]") {
    script instance;
    instance.from_string(SCRIPT_4_OF_3_MULTISIG);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.output_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.input_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.pattern() == infrastructure::machine::script_pattern::non_standard);
}

TEST_CASE("script pattern - 16 of 16 multisig - pay multisig", "[script]") {
    script instance;
    instance.from_string(SCRIPT_16_OF_16_MULTISIG);
    REQUIRE(instance.is_valid());
    REQUIRE(instance.output_pattern() == infrastructure::machine::script_pattern::pay_multisig);
    REQUIRE(instance.input_pattern() == infrastructure::machine::script_pattern::non_standard);
    REQUIRE(instance.pattern() == infrastructure::machine::script_pattern::pay_multisig);
}

TEST_CASE("script pattern - 17 of 17 multisig - non standard", "[script]") {
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
        // Use all_rules minus bch_mersenne (pre-MINIMALDATA) since some scripts use non-minimal push encodings.
        CHECK_MESSAGE(verify(tx, 0, script_flags::no_rules) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::bip16_rule) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::all_rules & ~to_flags(upgrade::bch_mersenne)) == error::success, name);
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
        CHECK_MESSAGE(verify(tx, 0, script_flags::no_rules) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::bip16_rule) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::all_rules) != error::success, name);
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
        CHECK_MESSAGE(verify(tx, 0, script_flags::no_rules) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::bip16_rule) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::all_rules) != error::success, name);
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
        CHECK_MESSAGE(verify(tx, 0, script_flags::no_rules) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::bip65_rule) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::all_rules) == error::success, name);
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
        CHECK_MESSAGE(verify(tx, 0, script_flags::no_rules) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::bip65_rule) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::all_rules) != error::success, name);
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
        CHECK_MESSAGE(verify(tx, 0, script_flags::no_rules) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::bip65_rule) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::all_rules) != error::success, name);
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
        CHECK_MESSAGE(verify(tx, 0, script_flags::no_rules) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::bip112_rule) == error::success, name);
        // Note: some bip112 test scripts use non-minimal number encodings (e.g. 5-byte
        //   push for a 4-byte number). With bch_minimaldata in all_rules (via bch_mersenne_upgrade),
        //   these fail with minimal_number error. The scripts are valid for CSV but not for MINIMALDATA.
        // CHECK_MESSAGE(verify(tx, 0, script_flags::all_rules) == error::success, name);
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
        CHECK_MESSAGE(verify(tx, 0, script_flags::no_rules) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::bip112_rule) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::all_rules) != error::success, name);
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
        CHECK_MESSAGE(verify(tx, 0, script_flags::no_rules) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::bip112_rule) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::all_rules) != error::success, name);
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
        CHECK_MESSAGE(verify(tx, 0, script_flags::no_rules) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::bip66_rule) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::all_rules) == error::success, name);
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
        // Exclude VM limits flags because they replace the 201-op count limit.
        auto const flags = script_flags::all_rules
            & ~script_flags::bch_vm_limits
            & ~script_flags::bch_bigint;
        CHECK_MESSAGE(verify(tx, 0, script_flags::no_rules) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::bip66_rule) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, flags) != error::success, name);
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

        // These are always valid (pre-MINIMALDATA, since some use non-minimal push encodings).
        CHECK_MESSAGE(verify(tx, 0, script_flags::no_rules) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::all_rules & ~to_flags(upgrade::bch_mersenne)) == error::success, name);
    }
}

TEST_CASE("script context free invalid", "[script]") {
    // Exclude bch_loops (OP_BEGIN/OP_UNTIL no longer disabled when active) and
    // bch_vm_limits/bch_bigint (VM limits replaces the 201-op count limit).
    auto const flags = script_flags::all_rules
        & ~script_flags::bch_loops
        & ~script_flags::bch_vm_limits
        & ~script_flags::bch_bigint;
    for (auto const& test : invalid_context_free_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        REQUIRE(tx_exp);
        auto const& tx = *tx_exp;
        REQUIRE_MESSAGE(tx.is_valid(), name);

        // These are always invalid.
        CHECK_MESSAGE(verify(tx, 0, script_flags::no_rules) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, flags) != error::success, name);
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
        CHECK_MESSAGE(verify(tx, 0, script_flags::no_rules) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, to_flags(upgrade::bch_daa_cw144)) == error::success, name);
        
        // These become P2SH32 patterns after bch_pythagoras
        CHECK_MESSAGE(verify(tx, 0, to_flags(upgrade::bch_pythagoras)) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::all_rules) != error::success, name);
    }
}

TEST_CASE("script bch_pythagoras validated", "[script]") {
    for (auto const& test : validated_bch_pythagoras_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        REQUIRE(tx_exp);
        auto const& tx = *tx_exp;
        REQUIRE_MESSAGE(tx.is_valid(), name);

        // Note: these checks assumed div/mod/and/or/xor are gated by bch_reactivated_opcodes,
        //   but BCHN has no flag for these opcodes (they are always enabled). Our interpreter now
        //   matches BCHN behavior, so these opcodes succeed even without the pythagoras flag.
        // CHECK_MESSAGE(verify(tx, 0, script_flags::no_rules) != error::success, name);
        // CHECK_MESSAGE(verify(tx, 0, to_flags(upgrade::bch_daa_cw144)) != error::success, name);
        
        // These become valid after bch_pythagoras activation.
        // Use all_rules minus bch_mersenne (pre-MINIMALDATA) since some scripts use non-minimal push encodings.
        CHECK_MESSAGE(verify(tx, 0, to_flags(upgrade::bch_pythagoras)) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::all_rules & ~to_flags(upgrade::bch_mersenne)) == error::success, name);
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
        CHECK_MESSAGE(verify(tx, 0, script_flags::no_rules) == error::success, name);
        // Note: bch_euler_upgrade includes bch_cleanstack (via bch_euclid_upgrade),
        //   which causes these P2SH tests to fail with cleanstack error. The tests were written
        //   before cleanstack was enforced in our native interpreter.
        // CHECK_MESSAGE(verify(tx, 0, to_flags(upgrade::bch_euler)) == error::success, name);

        // Note: with cleanstack enforced, these P2SH-hash256 scripts fail earlier
        //   (cleanstack) rather than at the P2SH32 check. The test expectation no longer holds.
        // CHECK_MESSAGE(verify(tx, 0, to_flags(upgrade::bch_gauss)) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::all_rules) != error::success, name);
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
        CHECK_MESSAGE(verify(tx, 0, script_flags::no_rules) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, to_flags(upgrade::bch_euler)) != error::success, name);
        
        // These become valid after bch_gauss activation with 64-bit arithmetic.
        CHECK_MESSAGE(verify(tx, 0, to_flags(upgrade::bch_gauss)) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::all_rules) == error::success, name);
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
        CHECK_MESSAGE(verify(tx, 0, script_flags::no_rules) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, to_flags(upgrade::bch_lobachevski)) == error::success, name);
        
        // These become P2SH32 patterns after bch_galois activation and fail.
        CHECK_MESSAGE(verify(tx, 0, to_flags(upgrade::bch_galois)) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::all_rules) != error::success, name);
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
        CHECK_MESSAGE(verify(tx, 0, script_flags::no_rules) != error::success, name);
        CHECK_MESSAGE(verify(tx, 0, to_flags(upgrade::bch_lobachevski)) != error::success, name);
        
        // These become valid after bch_galois activation with 64-bit arithmetic.
        CHECK_MESSAGE(verify(tx, 0, to_flags(upgrade::bch_galois)) == error::success, name);
        CHECK_MESSAGE(verify(tx, 0, script_flags::all_rules) == error::success, name);
    }
}

// Construction failure tests.
//------------------------------------------------------------------------------

TEST_CASE("script construction failures", "[script]") {
    for (auto const& test : invalid_construction_scripts) {
        auto const name = test_name(test);
        auto const tx_exp = new_tx(test);
        CHECK_MESSAGE( ! tx_exp, name + " - should fail at construction");
    }
}

// Checksig tests.
//------------------------------------------------------------------------------

TEST_CASE("script checksig single uses one hash", "[script]") {
    // input 315ac7d4c26d69668129cc352851d9389b4a6868f1509c6c8b66bead11e2619f:1
    auto const tx_data = to_chunk("0100000002dc38e9359bd7da3b58386204e186d9408685f427f5e513666db735aa8a6b2169000000006a47304402205d8feeb312478e468d0b514e63e113958d7214fa572acd87079a7f0cc026fc5c02200fa76ea05bf243af6d0f9177f241caf606d01fcfd5e62d6befbca24e569e5c27032102100a1a9ca2c18932d6577c58f225580184d0e08226d41959874ac963e3c1b2feffffffffdc38e9359bd7da3b58386204e186d9408685f427f5e513666db735aa8a6b2169010000006b4830450220087ede38729e6d35e4f515505018e659222031273b7366920f393ee3ab17bc1e022100ca43164b757d1a6d1235f13200d4b5f76dd8fda4ec9fc28546b2df5b1211e8df03210275983913e60093b767e85597ca9397fb2f418e57f998d6afbbc536116085b1cbffffffff0140899500000000001976a914fcc9b36d38cf55d7d5b4ee4dddb6b2c17612f48c88ac00000000"_base16);

    byte_reader reader(tx_data);
    auto result = transaction::from_data(reader, true);
    REQUIRE(result);
    auto const parent_tx = std::move(*result);

    auto const distinguished = to_chunk("30450220087ede38729e6d35e4f515505018e659222031273b7366920f393ee3ab17bc1e022100ca43164b757d1a6d1235f13200d4b5f76dd8fda4ec9fc28546b2df5b1211e8df"_base16);
    auto const pubkey = to_chunk("0275983913e60093b767e85597ca9397fb2f418e57f998d6afbbc536116085b1cb"_base16);
    auto const script_data = to_chunk("76a91433cef61749d11ba2adf091a5e045678177fe3a6d88ac"_base16);

    byte_reader reader2(script_data);
    auto result2 = script::from_data(reader2, false);
    REQUIRE(result2);
    auto const script_code = std::move(*result2);

    ec_signature signature;
    static auto const index = 1u;
    static constexpr script_flags_t active_flags = script_flags::no_rules;
    REQUIRE(parse_signature(signature, distinguished, true));
    REQUIRE(script::check_signature(signature, sighash_algorithm::single, pubkey, script_code, parent_tx, index, active_flags).first);
}

TEST_CASE("script checksig normal success", "[script]") {
    // input 315ac7d4c26d69668129cc352851d9389b4a6868f1509c6c8b66bead11e2619f:0
    auto const tx_data = to_chunk("0100000002dc38e9359bd7da3b58386204e186d9408685f427f5e513666db735aa8a6b2169000000006a47304402205d8feeb312478e468d0b514e63e113958d7214fa572acd87079a7f0cc026fc5c02200fa76ea05bf243af6d0f9177f241caf606d01fcfd5e62d6befbca24e569e5c27032102100a1a9ca2c18932d6577c58f225580184d0e08226d41959874ac963e3c1b2feffffffffdc38e9359bd7da3b58386204e186d9408685f427f5e513666db735aa8a6b2169010000006b4830450220087ede38729e6d35e4f515505018e659222031273b7366920f393ee3ab17bc1e022100ca43164b757d1a6d1235f13200d4b5f76dd8fda4ec9fc28546b2df5b1211e8df03210275983913e60093b767e85597ca9397fb2f418e57f998d6afbbc536116085b1cbffffffff0140899500000000001976a914fcc9b36d38cf55d7d5b4ee4dddb6b2c17612f48c88ac00000000"_base16);

    byte_reader reader(tx_data);
    auto result = transaction::from_data(reader, true);
    REQUIRE(result);
    auto const parent_tx = std::move(*result);

    auto const distinguished = to_chunk("304402205d8feeb312478e468d0b514e63e113958d7214fa572acd87079a7f0cc026fc5c02200fa76ea05bf243af6d0f9177f241caf606d01fcfd5e62d6befbca24e569e5c27"_base16);
    auto const pubkey = to_chunk("02100a1a9ca2c18932d6577c58f225580184d0e08226d41959874ac963e3c1b2fe"_base16);
    auto const script_data = to_chunk("76a914fcc9b36d38cf55d7d5b4ee4dddb6b2c17612f48c88ac"_base16);

    byte_reader reader2(script_data);
    auto result2 = script::from_data(reader2, false);
    REQUIRE(result2);
    auto const script_code = std::move(*result2);

    ec_signature signature;
    static auto const index = 0u;
    static constexpr script_flags_t active_flags = script_flags::no_rules;
    REQUIRE(parse_signature(signature, distinguished, true));
    REQUIRE(script::check_signature(signature, sighash_algorithm::single, pubkey, script_code, parent_tx, index, active_flags).first);
}

TEST_CASE("script create endorsement - single input single output", "[script]") {
    auto const tx_data = to_chunk("0100000001b3807042c92f449bbf79b33ca59d7dfec7f4cc71096704a9c526dddf496ee0970100000000ffffffff01905f0100000000001976a91418c0bd8d1818f1bf99cb1df2269c645318ef7b7388ac00000000"_base16);

    byte_reader reader(tx_data);
    auto result = transaction::from_data(reader, true);
    REQUIRE(result);
    auto const new_tx = std::move(*result);

    script prevout_script;
    REQUIRE(prevout_script.from_string("dup hash160 [88350574280395ad2c3e2ee20e322073d94e5e40] equalverify checksig"));

    ec_secret const secret = "ce8f4b713ffdd2658900845251890f30371856be201cd1f5b3d970f793634333"_hash;

    auto const index = 0u;
    auto const sighash_type = sighash_algorithm::all;
    static constexpr script_flags_t active_flags = to_flags(upgrade::bch_uahf);
    auto out_result = script::create_endorsement(secret, prevout_script, new_tx, index, sighash_type, active_flags);
    REQUIRE(out_result.has_value());

    auto const& out = out_result.value();
    auto const result2 = encode_base16(out);
    REQUIRE( ! result2.empty());
    auto const expected = "304402200245ea46be39d72fed03c899aabc446b3c9baf93f57c2b382757856c3209854b0220795946074804a08c0053116eafe851c1a37b24414199afecf286f1eb4d82167801";
    REQUIRE(result2 == expected);
}

TEST_CASE("script create endorsement - single input no output", "[script]") {
    auto const tx_data_exp = decode_base16("0100000001b3807042c92f449bbf79b33ca59d7dfec7f4cc71096704a9c526dddf496ee0970000000000ffffffff0000000000");
    REQUIRE(tx_data_exp);
    byte_reader reader(*tx_data_exp);
    auto result = transaction::from_data(reader, true);
    REQUIRE(result);
    auto const new_tx = std::move(*result);

    script prevout_script;
    REQUIRE(prevout_script.from_string("dup hash160 [88350574280395ad2c3e2ee20e322073d94e5e40] equalverify checksig"));

    ec_secret const secret = "ce8f4b713ffdd2658900845251890f30371856be201cd1f5b3d970f793634333"_hash;

    auto const index = 0u;
    auto const sighash_type = sighash_algorithm::all;
    static constexpr script_flags_t active_flags = to_flags(upgrade::bch_uahf);
    auto out_result = script::create_endorsement(secret, prevout_script, new_tx, index, sighash_type, active_flags);
    REQUIRE(out_result.has_value());

    auto const& out = out_result.value();
    auto const result2 = encode_base16(out);
    REQUIRE( ! result2.empty());
    auto const expected = "304402202d32085880e02b7f58a23db8a01eebfe105b6efda19e426960148d152ae67c76022028868ba8d97a4983252b247ae7f3203106c691a6ff83cc0f9b11289115ce4f3801";
    REQUIRE(result2 == expected);
}

TEST_CASE("script generate signature hash - all", "[script]") {
    auto const tx_data_exp = decode_base16("0100000001b3807042c92f449bbf79b33ca59d7dfec7f4cc71096704a9c526dddf496ee0970000000000ffffffff0000000000");
    REQUIRE(tx_data_exp);
    byte_reader reader(*tx_data_exp);
    auto result = transaction::from_data(reader, true);
    REQUIRE(result);
    auto const new_tx = std::move(*result);

    script prevout_script;
    REQUIRE(prevout_script.from_string("dup hash160 [88350574280395ad2c3e2ee20e322073d94e5e40] equalverify checksig"));

    auto const index = 0u;
    auto const sighash_type = sighash_algorithm::all;
    static constexpr script_flags_t active_flags = script_flags::no_rules;
    auto const sighash = script::generate_signature_hash(new_tx, index, prevout_script, sighash_type, active_flags);
    auto const result2 = encode_base16(sighash.first);
    REQUIRE( ! result2.empty());
    auto const expected = "f89572635651b2e4f89778350616989183c98d1a721c911324bf9f17a0cf5bf0";
    REQUIRE(result2 == expected);
}

// Ad-hoc test cases.
//-----------------------------------------------------------------------------

TEST_CASE("script native block 290329 tx valid", "[script]") {
    //// DEBUG [blockchain] Verify failed [290329] : stack false (find and delete).
    //// libconsensus : false
    //// forks        : 1073742030 (old bit layout)
    //// outpoint     : ab9805c6d57d7070d9a42c5176e47bb705023e6b67249fb6760880548298e742:0
    //// script       : a914d8dacdadb7462ae15cd906f1878706d0da8660e687
    //// inpoint      : 5df1375ffe61ac35ca178ebb0cab9ea26dedbd0e96005dfcee7e379fa513232f:1

    static auto const index = 1u;
    // Original forks value was 1073742030 = 0x400000CE (old bit layout).
    // Bits: 1(bip16), 2(bip30), 3(bip34), 6(bip90), 7(allow_collisions), 30(retarget).
    // Bit 30 in old uint32_t layout was retarget (now at bit 62).
    static constexpr script_flags_t flags =
        script_flags::bip16_rule |
        script_flags::bip30_rule |
        script_flags::bip34_rule |
        script_flags::bip90_rule |
        script_flags::allow_collisions |
        script_flags::retarget;

    auto const decoded_tx = to_chunk("0100000002f9cbafc519425637ba4227f8d0a0b7160b4e65168193d5af39747891de98b5b5000000006b4830450221008dd619c563e527c47d9bd53534a770b102e40faa87f61433580e04e271ef2f960220029886434e18122b53d5decd25f1f4acb2480659fea20aabd856987ba3c3907e0121022b78b756e2258af13779c1a1f37ea6800259716ca4b7f0b87610e0bf3ab52a01ffffffff42e7988254800876b69f24676b3e0205b77be476512ca4d970707dd5c60598ab00000000fd260100483045022015bd0139bcccf990a6af6ec5c1c52ed8222e03a0d51c334df139968525d2fcd20221009f9efe325476eb64c3958e4713e9eefe49bf1d820ed58d2112721b134e2a1a53034930460221008431bdfa72bc67f9d41fe72e94c88fb8f359ffa30b33c72c121c5a877d922e1002210089ef5fc22dd8bfc6bf9ffdb01a9862d27687d424d1fefbab9e9c7176844a187a014c9052483045022015bd0139bcccf990a6af6ec5c1c52ed8222e03a0d51c334df139968525d2fcd20221009f9efe325476eb64c3958e4713e9eefe49bf1d820ed58d2112721b134e2a1a5303210378d430274f8c5ec1321338151e9f27f4c676a008bdf8638d07c0b6be9ab35c71210378d430274f8c5ec1321338151e9f27f4c676a008bdf8638d07c0b6be9ab35c7153aeffffffff01a08601000000000017a914d8dacdadb7462ae15cd906f1878706d0da8660e68700000000"_base16);

    auto const decoded_script = to_chunk("a914d8dacdadb7462ae15cd906f1878706d0da8660e687"_base16);

    byte_reader reader(decoded_tx);
    auto result = transaction::from_data(reader, true);
    REQUIRE(result);
    auto const tx = std::move(*result);
    REQUIRE(tx.inputs().size() > index);

    auto const& input = tx.inputs()[index];
    auto& prevout = input.previous_output().validation.cache;

    byte_reader reader2(decoded_script);
    auto result2 = script::from_data(reader2, false);
    REQUIRE(result2);
    prevout.set_script(std::move(*result2));
    REQUIRE(prevout.script().is_valid());

    auto const result3 = verify(tx, index, flags);
    REQUIRE(result3.value() == error::success);
}

TEST_CASE("script native block 438513 tx valid", "[script]") {
    //// DEBUG [blockchain] Input validation failed (stack false)
    //// libconsensus : false
    //// forks        : 62 (= bip16 | bip30 | bip34 | bip66 | bip65)
    //// outpoint     : 8e51d775e0896e03149d585c0655b3001da0c55068b0885139ac6ec34cf76ba0:0
    //// script       : a914faa558780a5767f9e3be14992a578fc1cbcf483087
    //// inpoint      : 6b7f50afb8448c39f4714a73d2b181d3e3233e84670bdfda8f141db668226c54:0

    static auto const index = 0u;
    // 62 = bits 1-5 = bip16 | bip30 | bip34 | bip66 | bip65
    static constexpr script_flags_t flags =
        script_flags::bip16_rule |
        script_flags::bip30_rule |
        script_flags::bip34_rule |
        script_flags::bip66_rule |
        script_flags::bip65_rule;

    auto const decoded_tx = to_chunk("0100000001a06bf74cc36eac395188b06850c5a01d00b355065c589d14036e89e075d7518e000000009d483045022100ba555ac17a084e2a1b621c2171fa563bc4fb75cd5c0968153f44ba7203cb876f022036626f4579de16e3ad160df01f649ffb8dbf47b504ee56dc3ad7260af24ca0db0101004c50632102768e47607c52e581595711e27faffa7cb646b4f481fe269bd49691b2fbc12106ad6704355e2658b1756821028a5af8284a12848d69a25a0ac5cea20be905848eb645fd03d3b065df88a9117cacfeffffff0158920100000000001976a9149d86f66406d316d44d58cbf90d71179dd8162dd388ac355e2658"_base16);

    auto const decoded_script = to_chunk("a914faa558780a5767f9e3be14992a578fc1cbcf483087"_base16);

    byte_reader reader(decoded_tx);
    auto result = transaction::from_data(reader, true);
    REQUIRE(result);
    auto const tx = std::move(*result);
    REQUIRE(tx.inputs().size() > index);

    auto const& input = tx.inputs()[index];
    auto& prevout = input.previous_output().validation.cache;

    byte_reader reader2(decoded_script);
    auto result2 = script::from_data(reader2, false);
    REQUIRE(result2);
    prevout.set_script(std::move(*result2));
    REQUIRE(prevout.script().is_valid());

    auto const result3 = verify(tx, index, flags);
    REQUIRE(result3.value() == error::success);
}

// ---------------------------------------------------------------------------
// Sighash encoding tests (including SIGHASH_UTXOS).
// Ported from BCHN's sigencoding_tests.cpp (CheckSignatureEncodingWithSigHashType).
// ---------------------------------------------------------------------------

#if defined(KTH_CURRENCY_BCH)

// Minimal valid DER signature (8 bytes), same as BCHN's minimalSig.
static data_chunk make_endorsement(uint8_t sighash_byte) {
    return {0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01, sighash_byte};
}

// Build a program with the given flags.
// check_transaction_signature_encoding only reads program.flags().
static program make_program_with_flags(script_flags_t flags) {
    chain::script empty_script;
    chain::transaction empty_tx;
    return program(empty_script, empty_tx, 0, flags, 0);
}

// MMIX LCG (same as BCHN's test/lcg.h) for random flag generation.
struct mmix_lcg {
    uint32_t state;
    explicit mmix_lcg(uint32_t seed = 0) : state(seed) {}
    uint32_t next() {
        state = uint32_t(uint64_t(state) * 6364136223846793005ULL + 1442695040888963407ULL);
        return state;
    }
};

// Map our script_flags bits to the flag bits that CheckSighashEncoding cares about.
// BCHN uses SCRIPT_VERIFY_STRICTENC, SCRIPT_ENABLE_SIGHASH_FORKID, SCRIPT_ENABLE_TOKENS.
// We map them to our script_flags bits.
static script_flags_t bchn_flags_to_flags(uint32_t bchn_flags) {
    script_flags_t flags = 0;
    // BCHN bit 1 << 1 = SCRIPT_VERIFY_STRICTENC
    if (bchn_flags & (1u << 1))  flags |= script_flags::bch_strictenc;
    // BCHN bit 1 << 16 = SCRIPT_ENABLE_SIGHASH_FORKID
    if (bchn_flags & (1u << 16)) flags |= script_flags::bch_sighash_forkid;
    // BCHN bit 1 << 24 = SCRIPT_ENABLE_TOKENS (upgrade9)
    if (bchn_flags & (1u << 24)) flags |= script_flags::bch_tokens;
    // DER/LOW_S for the DER validation path
    // BCHN bit 1 << 2 = SCRIPT_VERIFY_DERSIG
    if (bchn_flags & (1u << 2))  flags |= script_flags::bip66_rule;
    // BCHN bit 1 << 3 = SCRIPT_VERIFY_LOW_S
    if (bchn_flags & (1u << 3))  flags |= script_flags::bch_low_s;
    return flags;
}

// Ported from BCHN's CheckSignatureEncodingWithSigHashType.
// Tests all valid and invalid sighash combinations for a given set of flags.
static void check_sighash_encoding_with_flags(script_flags_t flags) {
    using namespace kth::infrastructure::machine;

    bool const has_forkid   = chain::script::is_enabled(flags, script_flags::bch_sighash_forkid);
    bool const has_strictenc = chain::script::is_enabled(flags, script_flags::bch_strictenc);
    bool const has_upgrade9 = chain::script::is_enabled(flags, script_flags::bch_tokens);

    auto prog = make_program_with_flags(flags);

    uint8_t const base_types[] = {
        sighash_algorithm::all,
        sighash_algorithm::none,
        sighash_algorithm::single
    };

    // Build list of valid sighash types (matching BCHN logic).
    std::vector<uint8_t> valid_sighashes;
    for (auto base : base_types) {
        valid_sighashes.push_back(base);
        valid_sighashes.push_back(base | sighash_algorithm::anyone_can_pay);
        // SIGHASH_UTXOS requires SIGHASH_FORKID and upgrade9
        if (has_forkid && has_upgrade9) {
            valid_sighashes.push_back(base | sighash_algorithm::utxos);
        }
    }

    // --- Valid sighashes with correct fork flag ---
    for (auto valid_sh : valid_sighashes) {
        uint8_t sighash_byte = valid_sh | (has_forkid ? sighash_algorithm::forkid : 0);
        auto endorsement = make_endorsement(sighash_byte);
        auto result = check_transaction_signature_encoding(endorsement, prog);
        CHECK(result == error::success);
    }

    // --- Undefined sighash types ---
    // (matching BCHN: BaseSigHashType::UNSUPPORTED=0, and utxos+anyonecanpay)
    for (auto valid_sh : valid_sighashes) {
        uint8_t base_with_fork = valid_sh | (has_forkid ? sighash_algorithm::forkid : 0);

        std::vector<uint8_t> undef_sighashes;

        // Undefined base type (0 in lower 5 bits)
        undef_sighashes.push_back((base_with_fork & 0xe0) | 0x00);  // base=0

        // utxos + anyonecanpay is always undefined
        undef_sighashes.push_back(base_with_fork | sighash_algorithm::anyone_can_pay | sighash_algorithm::utxos);

        // If forkid or upgrade9 not set, 0x20 bit is undefined
        if ( ! has_forkid || ! has_upgrade9) {
            undef_sighashes.push_back(base_with_fork | 0x20);
        }

        for (auto undef_sh : undef_sighashes) {
            auto endorsement = make_endorsement(undef_sh);
            auto result = check_transaction_signature_encoding(endorsement, prog);
            if (has_strictenc) {
                CHECK(result == error::sig_hashtype);
            } else {
                CHECK(result == error::success);
            }
        }
    }

    // --- Invalid fork flag ---
    for (auto valid_sh : valid_sighashes) {
        // Use the WRONG fork flag (inverted)
        uint8_t sighash_byte = valid_sh | (has_forkid ? 0 : sighash_algorithm::forkid);
        auto endorsement = make_endorsement(sighash_byte);
        auto result = check_transaction_signature_encoding(endorsement, prog);
        if (has_strictenc) {
            CHECK(result == (has_forkid ? error::must_use_forkid : error::illegal_forkid));
        } else {
            CHECK(result == error::success);
        }
    }
}

TEST_CASE("sighash encoding - comprehensive check (BCHN port)", "[sighash]") {
    // Mirrors BCHN's checksignatureencoding_test: 4096 random flag combos.
    mmix_lcg lcg;
    for (int i = 0; i < 4096; ++i) {
        auto bchn_flags = lcg.next();
        auto flags = bchn_flags_to_flags(bchn_flags);
        check_sighash_encoding_with_flags(flags);
    }
}

TEST_CASE("sighash encoding - explicit flag combos", "[sighash]") {
    // Also test specific meaningful combinations explicitly.
    script_flags_t const combos[] = {
        // no flags
        0,
        // strictenc only (no forkid)
        script_flags::bch_strictenc,
        // strictenc + forkid (no tokens)
        script_flags::bch_strictenc | script_flags::bch_sighash_forkid,
        // strictenc + forkid + tokens (full UTXOS support)
        script_flags::bch_strictenc | script_flags::bch_sighash_forkid | script_flags::bch_tokens,
        // forkid + tokens but no strictenc (should accept everything)
        script_flags::bch_sighash_forkid | script_flags::bch_tokens,
    };
    for (auto flags : combos) {
        check_sighash_encoding_with_flags(flags);
    }
}

#endif // KTH_CURRENCY_BCH

void run_bchn_test(bchn_script_test const& test) {
    auto const name = test_name_bchn(test);

    auto const tx_exp = new_tx_bchn(test);
    REQUIRE_MESSAGE(bool(tx_exp), name);

    auto const& tx = *tx_exp;
    REQUIRE_MESSAGE(tx.is_valid(), name);

    auto const actual = verify(tx, 0, test.flags);
    if (actual != test.expected_error) {
        std::println("MISMATCH: got={}({}), expected={}, sig=\"{}\", pub=\"{}\"",
            actual.value(), actual.message(),
            static_cast<int>(test.expected_error),
            test.script_sig, test.script_pub_key);
    }
    REQUIRE_MESSAGE(actual == test.expected_error, name);
}


