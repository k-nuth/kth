// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//#include "script.hpp"

#include <stdint.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <kth/consensus.hpp>
#include <test_helpers.hpp>

#if defined(KTH_CURRENCY_BCH)
#include <bch-rules/script/interpreter.h>
#else
#include <btc-rules/script/interpreter.h>
#endif // KTH_CURRENCY_BCH

// Start Test Suite: consensus script verify

using namespace kth::consensus;

using data_chunk = std::vector<uint8_t>;
using kth::infrastructure::decode_base16;

#if defined(KTH_CURRENCY_BCH)
static
verify_result test_verify(std::string const& transaction, std::string const& prevout_script, size_t& sig_checks, uint32_t tx_input_index=0,
    const uint32_t flags=verify_flags_p2sh, int32_t tx_size_hack = 0, uint64_t amount = 0 ) {
    std::vector<std::vector<uint8_t>> coins;
    auto const tx_data = decode_base16(transaction);
    auto const prevout_script_data = decode_base16(prevout_script);
    REQUIRE(tx_data);
    REQUIRE(prevout_script_data);
    
    // Create empty unlocking script for this test
    data_chunk unlocking_script_data;
    const unsigned char* unlocking_ptr = unlocking_script_data.empty() ? nullptr : &unlocking_script_data[0];

    return verify_script(&(*tx_data)[0], tx_data->size() + tx_size_hack,
        &(*prevout_script_data)[0], prevout_script_data->size(),
        unlocking_ptr, unlocking_script_data.size(),
        tx_input_index, flags, sig_checks, amount, coins);
}
#else

static
verify_result test_verify(std::string const& transaction,
    std::string const& prevout_script, uint64_t prevout_value=0,
    uint32_t tx_input_index=0, const uint32_t flags=verify_flags_p2sh,
    int32_t tx_size_hack=0) {
    auto const tx_data = decode_base16(transaction);
    auto const prevout_script_data = decode_base16(prevout_script);
    REQUIRE(tx_data);
    REQUIRE(prevout_script_data);
    return verify_script(&(*tx_data)[0], tx_data->size() + tx_size_hack,
        &(*prevout_script_data)[0], prevout_script_data->size(), prevout_value,
        tx_input_index, flags);
}
#endif

// Test case derived from:
// github.com/libbitcoin/libbitcoin-explorer/wiki/How-to-Spend-Bitcoin
constexpr char consensus_script_verify_tx[] = "01000000017d01943c40b7f3d8a00a2d62fa1d560bf739a2368c180615b0a7937c0e883e7c000000006b4830450221008f66d188c664a8088893ea4ddd9689024ea5593877753ecc1e9051ed58c15168022037109f0d06e6068b7447966f751de8474641ad2b15ec37f4a9d159b02af68174012103e208f5403383c77d5832a268c9f71480f6e7bfbdfa44904becacfad66163ea31ffffffff01c8af0000000000001976a91458b7a60f11a904feef35a639b6048de8dd4d9f1c88ac00000000";
constexpr char consensus_script_verify_prevout_script[] = "76a914c564c740c6900b93afc9f1bdaef0a9d466adf6ee88ac";

// Test case derived from first witness tx:
constexpr char consensus_script_verify_witness_tx[] = "010000000001015836964079411659db5a4cfddd70e3f0de0261268f86c998a69a143f47c6c83800000000171600149445e8b825f1a17d5e091948545c90654096db68ffffffff02d8be04000000000017a91422c17a06117b40516f9826804800003562e834c98700000000000000004d6a4b424950313431205c6f2f2048656c6c6f20536567576974203a2d29206b656570206974207374726f6e6721204c4c415020426974636f696e20747769747465722e636f6d2f6b6873396e6502483045022100aaa281e0611ba0b5a2cd055f77e5594709d611ad1233e7096394f64ffe16f5b202207e2dcc9ef3a54c24471799ab99f6615847b21be2a6b4e0285918fd025597c5740121021ec0613f21c4e81c4b300426e5e5d30fa651f41e9993223adbe74dbe603c74fb00000000";
constexpr char consensus_script_verify_witness_prevout_script[] = "a914642bda298792901eb1b48f654dd7225d99e5e68c87";

TEST_CASE("consensus script verify null tx throws invalid argument", "[consensus script verify]") {
    size_t sig_checks;
    std::vector<std::vector<uint8_t>> coins;
    data_chunk unlocking_script_data;
    const unsigned char* unlocking_ptr = unlocking_script_data.empty() ? nullptr : &unlocking_script_data[0];
    auto const prevout_script_data = decode_base16(consensus_script_verify_prevout_script);
    REQUIRE(prevout_script_data);
    REQUIRE_THROWS_AS(verify_script(NULL, 1, &(*prevout_script_data)[0], prevout_script_data->size(),
        unlocking_ptr, unlocking_script_data.size(), 0, 0, sig_checks, 0, coins), std::invalid_argument);
}

//TODO: BTC test
TEST_CASE("consensus script verify value overflow throws invalid argument", "[consensus script verify]") {
    size_t sig_checks;
    std::vector<std::vector<uint8_t>> coins;
    data_chunk unlocking_script_data;
    const unsigned char* unlocking_ptr = unlocking_script_data.empty() ? nullptr : &unlocking_script_data[0];
    auto const prevout_script_data = decode_base16(consensus_script_verify_prevout_script);
    REQUIRE(prevout_script_data);
    REQUIRE_THROWS_AS(verify_script(NULL, 1, &(*prevout_script_data)[0], prevout_script_data->size(),
        unlocking_ptr, unlocking_script_data.size(), 0xffffffffffffffff, 0, sig_checks, 0, coins), std::invalid_argument);
}

TEST_CASE("consensus script verify null prevout script throws invalid argument", "[consensus script verify]") {
    size_t sig_checks;
    std::vector<std::vector<uint8_t>> coins;
    data_chunk unlocking_script_data;
    const unsigned char* unlocking_ptr = unlocking_script_data.empty() ? nullptr : &unlocking_script_data[0];
    auto const tx_data = decode_base16(consensus_script_verify_tx);
    REQUIRE(tx_data);
    REQUIRE_THROWS_AS(verify_script(&(*tx_data)[0], tx_data->size(), NULL, 1,
        unlocking_ptr, unlocking_script_data.size(), 0, 0, sig_checks, 0, coins), std::invalid_argument);
}

TEST_CASE("consensus script verify invalid tx tx invalid", "[consensus script verify]") {
    size_t sig_checks;
    const verify_result result = test_verify("42", "42", sig_checks);
    REQUIRE(result == verify_result_tx_invalid);
}

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("consensus script verify invalid input tx input invalid", "[consensus script verify]") {
    size_t sig_checks;
    const verify_result result = test_verify(consensus_script_verify_tx, consensus_script_verify_prevout_script, sig_checks, 1);
    REQUIRE(result == verify_result_tx_input_invalid);
}

TEST_CASE("consensus script verify undersized tx tx invalid", "[consensus script verify]") {
    size_t sig_checks;
    const verify_result result = test_verify(consensus_script_verify_tx, consensus_script_verify_prevout_script, sig_checks, 0, verify_flags_p2sh, -1);
    REQUIRE(result == verify_result_tx_invalid);
}
#else
TEST_CASE("consensus script verify invalid input tx input invalid", "[consensus script verify]") {
    const verify_result result = test_verify(consensus_script_verify_tx, consensus_script_verify_prevout_script, 0, 1);
    REQUIRE(result == verify_result_tx_input_invalid);
}

TEST_CASE("consensus script verify undersized tx tx invalid", "[consensus script verify]") {
    const verify_result result = test_verify(consensus_script_verify_tx, consensus_script_verify_prevout_script, 0, 0, verify_flags_p2sh, -1);
    REQUIRE(result == verify_result_tx_invalid);
}
#endif
TEST_CASE("consensus script verify oversized tx tx size invalid", "[consensus script verify]") {
    size_t sig_checks;
    const verify_result result = test_verify(consensus_script_verify_tx, consensus_script_verify_prevout_script, sig_checks, 0, 0, verify_flags_p2sh, +1);
    REQUIRE(result == verify_result_tx_size_invalid);
}

TEST_CASE("consensus script verify incorrect pubkey hash equalverify", "[consensus script verify]") {
    size_t sig_checks;
    const verify_result result = test_verify(consensus_script_verify_tx, "76a914c564c740c6900b93afc9f1bdaef0a9d466adf6ef88ac", sig_checks);
    REQUIRE(result == verify_result_equalverify);
}

TEST_CASE("consensus script verify valid true", "[consensus script verify]") {
    size_t sig_checks;
    const verify_result result = test_verify(consensus_script_verify_tx, consensus_script_verify_prevout_script, sig_checks);
    REQUIRE(result == verify_result_eval_true);
}

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("consensus script verify valid true non forkid", "[consensus script verify]") {
    std::string CONSENSUS_FORKID_TX = "0100000001f2ca1aebc2e51b345f87365bdfa4956aaa5443cfb38f58e75318e3a3d3f1462e000000006b483045022100845a35869063291e4610de4939ac76f123a0b11b74e0615694a09206c30afbfc022063347ec313a582ab2025863ebdca71015ffc698eb37dbbf679b2865be944cf79012102fee381c90149e22ae182156c16316c24fe680a0e617646c3d58531112ac82e29ffffffff01b2e60200000000001976a914b96b816f378babb1fe585b7be7a2cd16eb99b3e488ac00000000";
    std::string CONSENSUS_FORKID_TX_PREV_SCRIPT = "76a914b96b816f378babb1fe585b7be7a2cd16eb99b3e488ac";
    size_t sig_checks;
    const verify_result result = test_verify(CONSENSUS_FORKID_TX, CONSENSUS_FORKID_TX_PREV_SCRIPT, sig_checks);
    REQUIRE(result == verify_result_eval_true);
}

TEST_CASE("consensus script verify valid true forkid", "[consensus script verify]") {
    std::string CONSENSUS_FORKID_TX = "0200000001131df2e09cd21da7897b9be4dba49fabb49ca979c569c2e1c80ea942748df144000000006a473044022005faf4f6726817a2a81288694bd73473ccaa610ffd4f356323e861285e5b6119022010eee370ce7be0f4c7cdca9bbc6129b648b2c555499bb2c926637df947c86aae412103e18167f4be43032ce2e62f559fef271902e8ce1aaaf0d518a6a2f40d4e01ad47ffffffff02fa6e1c05000000001976a9140bd183d2e2333b99fc4d5c70377f8bc0645d30b188aca0860100000000001976a91405ef21a2ee76c2015eaf9f1d57b6e85137f05cfc88ac00000000";
    std::string CONSENSUS_FORKID_TX_PREV_SCRIPT = "76a9140bd183d2e2333b99fc4d5c70377f8bc0645d30b188ac";
    int CONSENSUS_FORKID_TX_AMMOUT = 85899498;
    auto flags = SCRIPT_ENABLE_SIGHASH_FORKID;
    size_t sig_checks;
    const verify_result result = test_verify(CONSENSUS_FORKID_TX, CONSENSUS_FORKID_TX_PREV_SCRIPT,sig_checks, 0, flags, 0, CONSENSUS_FORKID_TX_AMMOUT);
    REQUIRE(result == verify_result_eval_true);
}

TEST_CASE("consensus script verify valid true forkid long int", "[consensus script verify]") {
    std::string CONSENSUS_FORKID_TX="02000000016d4a52bbec92aca5014955a2d1d317f54a684dcc8b2ade9f9f1b5f873eb0933100000000694630430220058750aaa604cde20738ba2ae9949685b06f62c441e22e5e1374b422adde3865021f0c27d7f71603a86ebf05e7de0d29923d0616de03424bcbb858408fb8247d50412102a105d2380adc8f6b1148eab7db12098bb3860c5d856a24452fd6c6d63ba53e17feffffff0210270000000000001976a914bf9350c2ff5b147c33cab3307974f9a298b92a0688ac43b7052a010000001976a9149705d4b593f98f34e19a5961199db0ed7f04ebaf88ac23a31100";
    std::string CONSENSUS_FORKID_TX_PREV_SCRIPT = "76a914b939fdc5c4dd28318fd80b4203dc43003c4353ec88ac";
    unsigned long int CONSENSUS_FORKID_TX_AMMOUT = 5000000000;
    auto flags = SCRIPT_ENABLE_SIGHASH_FORKID;
    size_t sig_checks;
    const verify_result result = test_verify(CONSENSUS_FORKID_TX, CONSENSUS_FORKID_TX_PREV_SCRIPT, sig_checks, 0, flags, 0, CONSENSUS_FORKID_TX_AMMOUT);
}
#else

TEST_CASE("consensus script verify valid nested p2wpkh true", "[consensus script verify]") {
    static auto const index = 0u;
    static auto const value = 500000u;
    static const uint32_t flags =
        verify_flags_p2sh |
        verify_flags_dersig |
        verify_flags_nulldummy |
        verify_flags_checklocktimeverify |
        verify_flags_checksequenceverify |
        verify_flags_witness;

    const verify_result result = test_verify(consensus_script_verify_witness_tx, consensus_script_verify_witness_prevout_script, value, index, flags);
    REQUIRE(result == verify_result_eval_true);
}
#endif

// TODO: create negative test vector.
//TEST_CASE("consensus script verify invalid false", "[consensus script verify]")
//{
//    const verify_result result = test_verify(...);
//    REQUIRE(result == verify_result_eval_false);
//}

// End Test Suite
