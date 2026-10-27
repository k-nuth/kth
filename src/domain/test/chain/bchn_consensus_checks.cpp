// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Ports of the non-script parts of Bitcoin Cash Node consensus tests
// (commit 07576013c91ff4a3a74acd85f189c69121cdad1b):
//   - src/test/transaction_tests.cpp: tx_valid, tx_invalid, basic_transaction_tests
//   - src/test/blockcheck_tests.cpp: blockfail
// Original: Copyright (c) 2013-present The Bitcoin Core / Bitcoin developers, MIT license.
//
// Mapping of BCHN entry points to Knuth:
//   CheckRegularTransaction -> transaction::check(max_block_size, transaction_pool = true)
//   CheckCoinbase           -> transaction::check(max_block_size, transaction_pool = false)
//   CheckBlock              -> block::check_body()
// Script execution (VerifyScript) is out of scope here; see coverage.json.

#include <test_helpers.hpp>

#include <kth/domain/constants.hpp>

#include <algorithm>

#include "bchn_tx_vectors_data.hpp"

#include <string>

using namespace kth;
namespace chain = kth::domain::chain;

namespace {

constexpr size_t block_max = 2'000'000'000;   // static_absolute_max_block_size()

chain::transaction parse_tx(std::string const& hex) {
    auto raw = decode_base16(hex);
    REQUIRE(raw);
    byte_reader reader(*raw);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    return std::move(*result);
}

// CheckRegularTransaction / CheckCoinbase as BCHN's harness chooses between them.
code check_like_bchn_harness(chain::transaction const& tx) {
    return tx.check(block_max, /*transaction_pool*/ ! tx.is_coinbase());
}

} // namespace

// BCHN transaction_tests/tx_valid, non-script half: every vector must pass
// CheckCoinbase (coinbase) or CheckRegularTransaction (everything else).
// The VerifyScript half of the case is excluded in coverage.json.
TEST_CASE("BCHN transaction_tests - tx_valid structural check", "[bchn_non_vm][transaction]") {
    for (auto const& v : bchn_tx_vectors::tx_valid()) {
        DYNAMIC_SECTION("tx_valid.json[" << v.json_index << "] " << v.comment) {
            auto const tx = parse_tx(v.tx_hex);
            CHECK(check_like_bchn_harness(tx) == error::success);
        }
    }
}

// BCHN transaction_tests/tx_invalid, the vectors under "Tests for CheckTransaction()".
// In BCHN these are rejected by CheckRegularTransaction before any script runs.
// The remaining tx_invalid vectors fail inside VerifyScript and are excluded.
TEST_CASE("BCHN transaction_tests - tx_invalid CheckTransaction vectors", "[bchn_non_vm][transaction]") {
    struct expectation { int json_index; code expected; };
    // index in tx_invalid.json -> reason, see the comment on that vector
    static expectation const expected[] = {
        {21, error::empty_transaction},                       // No outputs
        {23, error::spend_overflow},                          // Negative output
        {25, error::spend_overflow},                          // MAX_MONEY + 1 output
        {27, error::spend_overflow},                          // MAX_MONEY output + 1 output
        {29, error::transaction_internal_double_spend},       // Duplicate inputs
        {32, error::coinbase_transaction},                    // Coinbase of size 1
        {35, error::coinbase_transaction},                    // Coinbase of size 101
        {37, error::previous_output_null},                    // Null txin, not a coinbase (two inputs)
        {38, error::previous_output_null},                    // Null txin, not a coinbase (two inputs, null second)
    };
    auto const& all = bchn_tx_vectors::tx_invalid();
    for (auto const& e : expected) {
        auto const it = std::find_if(all.begin(), all.end(), [&](auto const& v) { return v.json_index == e.json_index; });
        REQUIRE(it != all.end());
        DYNAMIC_SECTION("tx_invalid.json[" << e.json_index << "] " << it->comment) {
            auto const tx = parse_tx(it->tx_hex);
            // BCHN's tx_invalid harness always uses CheckRegularTransaction
            CHECK(tx.check(block_max, /*transaction_pool*/ true) == e.expected);
            if (tx.is_coinbase()) {
                // What the vector's comment is about: CheckCoinbase's bad-cb-length
                CHECK(tx.check(block_max, /*transaction_pool*/ false) == error::invalid_coinbase_script_size);
            }
        }
    }
}

// BCHN basic_transaction_tests
TEST_CASE("BCHN transaction_tests - basic_transaction_tests", "[bchn_non_vm][transaction]") {
    // Random real transaction (e2769b09e784f32f62ef849763d4f45b98e07ba658647343b915ff832b110436)
    auto const hex =
        "01000000016bff7fcd4f8565ef406dd5d63d4ff94f318fe82027fd4dc451b04474019f74b4000000008c4930460221"
        "00da0dc6aecefe1e06efdf05773757deb168820930e3b0d03f46f5fcf150bf990c022100d25b5c87040076e4f253f8"
        "262e763e2dd51e7ff0be157727c4bc42807f17bd39014104e6c26ef67dc610d2cd192484789a6cf9aea9930b944b7e"
        "2db5342b9d9e5b9ff79aff9a2ee1978dd7fd01dfc522ee02283d3b06a9d03acf8096968d7dbb0f9178ffffffff028b"
        "a7940e000000001976a914badeecfdef0507247fc8f74241d73bc039972d7b88ac4094a802000000001976a914c1"
        "0932483fec93ed51f5fe95e72559f2cc7043f988ac00000000";
    auto tx = parse_tx(hex);
    REQUIRE(tx.hash() == "e2769b09e784f32f62ef849763d4f45b98e07ba658647343b915ff832b110436"_hash);
    CHECK(tx.check(block_max, true) == error::success);

    // Check that duplicate txins fail
    auto inputs = tx.inputs();
    inputs.push_back(inputs[0]);
    chain::transaction const duplicated(tx.version(), tx.locktime(), std::move(inputs), chain::output::list(tx.outputs()));
    CHECK(duplicated.check(block_max, true) == error::transaction_internal_double_spend);
}

// -----------------------------------------------------------------------------
// BCHN blockcheck_tests/blockfail
// -----------------------------------------------------------------------------
// The "Oversize block" subcase (a block of ~2 GB built from ~2000 1 MB
// transactions, then one more) is not ported here: it needs about 2 GB of
// resident memory and the checkout shares the machine with other builds. It is
// tracked as pending_resource_limit in coverage.json and not replaced by a
// smaller block.

namespace {

chain::output_point random_outpoint(uint8_t seed) {
    hash_digest h{};
    h.fill(seed);
    return chain::output_point{h, 0};
}

// The block header carries the merkle root of its transactions, because
// block::check_body() also checks it (BCHN's harness switches that check off).
chain::block block_of(chain::transaction::list txs) {
    chain::block candidate(chain::header{}, std::move(txs));
    chain::header header{1, null_hash, candidate.generate_merkle_root(), 0, 0, 0};
    return chain::block(std::move(header), chain::transaction::list(candidate.transactions()));
}

chain::transaction coinbase_with_script_size(size_t script_size) {
    chain::input::list inputs;
    inputs.emplace_back(chain::output_point{null_hash, chain::point::null_index},
                        chain::script(data_chunk(script_size, 0x00), false), 0xffffffff);
    chain::output::list outputs;
    outputs.emplace_back(42, chain::script{}, chain::token_data_opt{});
    return chain::transaction(1, 0, std::move(inputs), std::move(outputs));
}

} // namespace

TEST_CASE("BCHN blockcheck_tests - blockfail", "[bchn_non_vm][block]") {
    // empty block: no coinbase
    {
        chain::block const block;
        CHECK(block.check_body() == error::empty_block);       // bad-cb-missing
    }

    // Coinbase only
    auto const coinbase = coinbase_with_script_size(10);
    {
        chain::transaction::list txs;
        txs.push_back(coinbase);
        CHECK(block_of(std::move(txs)).check_body() == error::success);
    }

    // No coinbase: the first transaction spends a non-null outpoint
    {
        chain::input::list inputs;
        inputs.emplace_back(random_outpoint(0x11), chain::script(data_chunk(10, 0x00), false), 0xffffffff);
        chain::output::list outputs;
        outputs.emplace_back(42, chain::script{}, chain::token_data_opt{});
        chain::transaction::list txs;
        txs.emplace_back(1, 0, std::move(inputs), std::move(outputs));
        CHECK(block_of(std::move(txs)).check_body() == error::first_not_coinbase);   // bad-cb-missing
    }

    // Invalid coinbase: scriptSig of length 0
    {
        chain::transaction::list txs;
        txs.push_back(coinbase_with_script_size(0));
        CHECK(block_of(std::move(txs)).check_body() == error::invalid_coinbase_script_size);   // bad-cb-length
    }
}

// -----------------------------------------------------------------------------
// BCHN validation_tests/block_subsidy_test and subsidy_limit_test
// (src/test/validation_tests.cpp, Copyright (c) 2011-2019 The Bitcoin Core developers,
// 2017-2021 The Bitcoin developers, MIT license)
// -----------------------------------------------------------------------------
// block_subsidy_test also runs with "just another interval" (1000 blocks);
// block::subsidy only offers the mainnet and regtest intervals, so that third
// run has no entry point and is tracked as excluded in coverage.json.

namespace {

void check_subsidy_halvings(bool retarget) {
    constexpr int max_halvings = 64;
    uint64_t const initial_subsidy = 50 * satoshi_per_bitcoin;
    uint64_t const interval = subsidy_interval(retarget);

    // for height == 0
    uint64_t previous_subsidy = 2 * initial_subsidy;
    for (int halvings = 0; halvings < max_halvings; ++halvings) {
        auto const height = halvings * interval;
        auto const subsidy = chain::block::subsidy(height, retarget);
        CHECK(subsidy <= initial_subsidy);
        CHECK(subsidy == previous_subsidy / 2);
        previous_subsidy = subsidy;
    }
    CHECK(chain::block::subsidy(max_halvings * interval, retarget) == 0u);
}

} // namespace

TEST_CASE("BCHN validation_tests - block_subsidy_test (mainnet and regtest intervals)", "[bchn_non_vm][subsidy]") {
    SECTION("as in main") { check_subsidy_halvings(true); REQUIRE(subsidy_interval(true) == 210'000u); }
    SECTION("as in regtest") { check_subsidy_halvings(false); REQUIRE(subsidy_interval(false) == 150u); }
}

TEST_CASE("BCHN validation_tests - subsidy_limit_test", "[bchn_non_vm][subsidy]") {
    uint64_t sum = 0;
    for (uint64_t height = 0; height < 14'000'000; height += 1000) {
        auto const subsidy = chain::block::subsidy(height, true);
        CHECK(subsidy <= 50 * satoshi_per_bitcoin);
        sum += 1000 * subsidy;
        CHECK(sum <= max_satoshi_supply);
    }
    CHECK(sum == 2099999997690000ULL);
}

// -----------------------------------------------------------------------------
// BCHN transaction_tests/txsize_activation_test
// -----------------------------------------------------------------------------
// ContextualCheckTransaction's undersize rule: transactions under 100 bytes are
// invalid from the Magnetic Anomaly upgrade (Nov 2018), under 65 bytes from the
// Upgrade 9 (May 2023). Knuth applies it inside transaction::accept, selecting the
// minimum by the script flags of the block's height.

TEST_CASE("BCHN transaction_tests - txsize_activation_test", "[bchn_non_vm][transaction][min_tx_size]") {
    namespace upgrade = kth::domain::machine::upgrade;
    auto const before_magnetic = kth::domain::to_flags(upgrade::bch_pythagoras);
    auto const magnetic = kth::domain::to_flags(upgrade::bch_euclid);
    auto const before_upgrade9 = kth::domain::to_flags(upgrade::bch_gauss);
    auto const upgrade9 = kth::domain::to_flags(upgrade::bch_descartes);

    auto const accept = [](chain::transaction const& tx, kth::domain::script_flags_t flags) {
        return tx.accept(flags, 600'000, 1'700'000'000, 1'000'000, false, false, false);
    };

    // A minimally-sized transaction: no inputs, no outputs
    chain::transaction const min_tx;
    REQUIRE(min_tx.serialized_size(true) < min_transaction_size_euclid);
    CHECK(accept(min_tx, before_magnetic) == error::success);
    CHECK(accept(min_tx, magnetic) == error::transaction_size_limit);       // bad-txns-undersize

    // A transaction of exactly 65 bytes: one null input and one empty output, padded by the scriptSig
    auto const small_tx = [] {
        chain::input::list inputs;
        inputs.emplace_back(chain::output_point{null_hash, chain::point::null_index}, chain::script{}, 0xffffffff);
        chain::output::list outputs;
        outputs.emplace_back(0, chain::script{}, chain::token_data_opt{});
        chain::transaction base(1, 0, inputs, outputs);
        auto const padding = min_transaction_size_descartes - base.serialized_size(true);
        REQUIRE(min_transaction_size_descartes >= base.serialized_size(true));
        chain::input::list padded;
        padded.emplace_back(chain::output_point{null_hash, chain::point::null_index},
                            chain::script(data_chunk(padding, 0x00), false), 0xffffffff);
        return chain::transaction(1, 0, std::move(padded), std::move(outputs));
    }();
    REQUIRE(small_tx.serialized_size(true) == min_transaction_size_descartes);
    REQUIRE(small_tx.serialized_size(true) < min_transaction_size_euclid);
    CHECK(accept(small_tx, before_upgrade9) == error::transaction_size_limit);   // bad-txns-undersize
    CHECK(accept(small_tx, upgrade9) == error::success);
}

// -----------------------------------------------------------------------------
// BCHN test/functional/feature_tx_version.py, consensus half
// -----------------------------------------------------------------------------
// Before Upgrade 9 an out-of-range version is only a relay rule: a block carrying
// such transactions is valid. From Upgrade 9 the version must be 1 or 2 by
// consensus. (The relay half -- "was not accepted: version" before Upgrade 9 --
// is policy with no entry point yet, tracked in coverage.json.)

TEST_CASE("BCHN feature_tx_version - transaction version is 1 or 2 from Upgrade 9", "[bchn_non_vm][transaction][tx_version]") {
    namespace upgrade = kth::domain::machine::upgrade;
    auto const before_upgrade9 = kth::domain::to_flags(upgrade::bch_gauss);
    auto const upgrade9 = kth::domain::to_flags(upgrade::bch_descartes);

    auto const spend_with_version = [](uint32_t version) {
        hash_digest h{};
        h.fill(0x31);
        chain::output prevout(100'000, chain::script(data_chunk(25, 0x51), false), chain::token_data_opt{});
        data_chunk const push(32, 0x5a);
        kth::domain::machine::operation::list ops;
        ops.emplace_back(push, true);
        ops.emplace_back(push, true);
        chain::input::list inputs;
        inputs.emplace_back(chain::output_point{h, 0}, chain::script(std::move(ops)), 0xffffffff);
        chain::output::list outputs;
        outputs.emplace_back(90'000, chain::script(data_chunk(25, 0x51), false), chain::token_data_opt{});
        chain::transaction tx(version, 0, std::move(inputs), std::move(outputs));
        tx.inputs()[0].previous_output().validation.cache = prevout;
        tx.inputs()[0].previous_output().validation.height = 100;
        return tx;
    };
    auto const accept = [](chain::transaction const& tx, kth::domain::script_flags_t flags) {
        return tx.accept(flags, 600'000, 1'700'000'000, 1'000'000, false, false, false);
    };

    for (uint32_t version : {1u, 2u}) {
        DYNAMIC_SECTION("version " << version) {
            CHECK(accept(spend_with_version(version), before_upgrade9) == error::success);
            CHECK(accept(spend_with_version(version), upgrade9) == error::success);
        }
    }
    for (uint32_t version : {0u, 3u, 123456u}) {
        DYNAMIC_SECTION("version " << version) {
            CHECK(accept(spend_with_version(version), before_upgrade9) == error::success);     // valid in a block
            CHECK(accept(spend_with_version(version), upgrade9) == error::invalid_tx_version);
        }
    }
}

// -----------------------------------------------------------------------------
// BCHN transaction_tests/test_Get (value half) and checktxinput_test (availability half)
// -----------------------------------------------------------------------------
// test_Get also asserts AreInputsStandard (policy, no Knuth entry point: pending in coverage.json);
// checktxinput_test also runs scripts through a block (VM) and uses a TestChain100Setup node.

namespace {

chain::output cache_output(uint64_t value) {
    return chain::output(value, chain::script(data_chunk(25, 0x51), false), chain::token_data_opt{});
}

chain::transaction spend_inputs(std::vector<std::pair<uint8_t, uint64_t>> const& inputs_with_cached_value, uint64_t out_value, bool cache_last) {
    chain::input::list inputs;
    for (auto const& [seed, value] : inputs_with_cached_value) {
        hash_digest h{};
        h.fill(seed);
        inputs.emplace_back(chain::output_point{h, seed}, chain::script(data_chunk(70, 0x00), false), 0xffffffff);
        (void)value;
    }
    chain::output::list outputs;
    outputs.emplace_back(out_value, chain::script(data_chunk(25, 0x51), false), chain::token_data_opt{});
    chain::transaction tx(1, 0, std::move(inputs), std::move(outputs));
    for (size_t i = 0; i < inputs_with_cached_value.size(); ++i) {
        if (i + 1 == inputs_with_cached_value.size() && ! cache_last) continue;
        tx.inputs()[i].previous_output().validation.cache = cache_output(inputs_with_cached_value[i].second);
        tx.inputs()[i].previous_output().validation.height = 100;
    }
    return tx;
}

constexpr uint64_t cent = 1'000'000;

} // namespace

TEST_CASE("BCHN transaction_tests - test_Get (value of the inputs)", "[bchn_non_vm][transaction]") {
    // three inputs worth 50, 21 and 22 CENT (SetupDummyInputs' coins)
    auto const tx = spend_inputs({{1, 50 * cent}, {2, 21 * cent}, {3, 22 * cent}}, 90 * cent, true);
    CHECK(tx.total_input_value() == (50 + 21 + 22) * cent);
    CHECK(tx.fees() == 3 * cent);
}

TEST_CASE("BCHN transaction_tests - checktxinput_test (an input must be available)", "[bchn_non_vm][transaction]") {
    namespace upgrade = kth::domain::machine::upgrade;
    auto const flags = kth::domain::to_flags(upgrade::bch_gauss);
    auto const accept = [&](chain::transaction const& tx) {
        return tx.accept(flags, 600'000, 1'700'000'000, 1'000'000, false, false, false);
    };
    // two inputs, outputs worth 22 CENT, the second coin worth zero (as in the original)
    CHECK(accept(spend_inputs({{1, 50 * cent}, {2, 0}}, 22 * cent, true)) == error::success);
    // the second coin gone (spent): the transaction is no longer acceptable
    CHECK(accept(spend_inputs({{1, 50 * cent}, {2, 0}}, 22 * cent, false)) == error::missing_previous_output);
}
