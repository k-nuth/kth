// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// More block-level scenarios of Bitcoin Cash Node test/functional/feature_block.py (commit
// 07576013c91ff4a3a74acd85f189c69121cdad1b), driven through validate_block_batch on a regtest chain:
//   b30   a coinbase scriptSig of 100 bytes (the maximum) and of 2 bytes (the minimum) is valid; b26 (1 byte), b28 (101 bytes)
//         and their children b27, b29 are failed regressions kept as evidence outside this tree
//   b57   a block whose transaction list was mutated by duplicating its last transaction (CVE-2012-2459: same merkle
//         root and header) is refused, and the valid block with that same header is accepted afterwards on the same
//         node (b57p2, and b72 which repeats the pattern)
//   b58   a spend of an output index that the transaction does not have
//   b89a  a spend of an OP_RETURN output
// and txvalidationcache_tests.cpp tx_mempool_block_doublespend (both signature variants): a block with two spends of one
// output is refused by block validation; a separate mempool unit control applies an accepted block by hand.
// Original: Copyright (c) 2015-2019 The Bitcoin Core developers, 2017-present The Bitcoin developers, MIT license.

#include <test_helpers.hpp>

#include <kth/blockchain/pools/mempool.hpp>

#include "bchn_block_builder.hpp"

using namespace kth;
using namespace kth::test;
using kth::domain::chain::block;
using kth::domain::chain::output;
using kth::domain::chain::output_point;
using kth::domain::chain::script;
using kth::domain::chain::transaction;

namespace {

transaction spend_coinbase(regtest_chain const& chain, uint32_t h, uint64_t fee, domain::script_flags_t flags) {
    auto const& cb = chain.coinbase_at(h);
    output::list outs;
    outs.emplace_back(cb.outputs()[0].value() - fee, miner_script(), domain::chain::token_data_opt{});
    return signed_spend(cb, 0, std::move(outs), flags);
}

code validate_fresh(regtest_chain const& chain, blockchain::settings const& s, char const* tag) {
    chain_fixture fixture(tag);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    return validate_chain(fixture, s, chain.blocks());
}

} // namespace

TEST_CASE("BCHN feature_block b30 - coinbase scriptSig of 100 and of 2 bytes is accepted",
          "[bchn_non_vm][block][coinbase_shape]") {
    blockchain::settings const s(domain::config::network::regtest);
    for (size_t length : {size_t(2), size_t(100)}) {
        regtest_chain chain(110);
        chain.extend_to(101);
        auto const height = chain.height() + 1;
        domain::chain::input::list inputs;
        auto const height_number = infrastructure::machine::number::from_int(int64_t(height));
        REQUIRE(height_number);
        domain::machine::operation::list height_ops;
        height_ops.emplace_back(height_number->data(), true);
        auto const height_script = script(std::move(height_ops)).bytes();
        REQUIRE(height_script.size() <= length);
        data_chunk script_bytes(height_script.begin(), height_script.end());
        script_bytes.resize(length, 0x00);
        inputs.emplace_back(output_point{null_hash, domain::chain::point::null_index},
                            script(std::move(script_bytes), false), 0xffffffff);
        output::list outputs;
        outputs.emplace_back(block::subsidy(height, false), miner_script(), domain::chain::token_data_opt{});
        transaction::list all;
        all.emplace_back(1, 0, std::move(inputs), std::move(outputs));
        chain.append_raw(std::move(all));
        CHECK(validate_fresh(chain, s, length == 2 ? "bchn_b30_2" : "bchn_b30_100") == error::success);
    }
}

TEST_CASE("BCHN feature_block b57 / b57p2 / b72 - duplicated-transaction mutation is refused, the valid block with the same header is accepted", "[bchn_non_vm][block][merkle]") {
    blockchain::settings const s(domain::config::network::regtest);
    regtest_chain chain(110);
    chain.extend_to(101);
    auto const a = spend_coinbase(chain, 1, 0, s.enabled_flags());
    auto const b = spend_coinbase(chain, 2, 0, s.enabled_flags());
    // valid block: coinbase, a, b ; mutated: coinbase, a, b, b (same merkle root, same header)
    auto const& valid = chain.append({a, b});
    transaction::list dup(valid.transactions());
    dup.push_back(b);
    block const mutated(valid.header(), std::move(dup));
    REQUIRE(mutated.header().merkle() == mutated.generate_merkle_root());
    auto blocks = chain.blocks();
    blocks.pop_back();
    blocks.push_back(std::make_shared<domain::message::block const>(mutated));
    chain_fixture fixture("bchn_b57");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const ec = validate_chain(fixture, s, blocks);
    INFO("mutated ec " << ec.message());
    CHECK(ec == error::block_internal_double_spend);
    // the valid block with the same header is accepted afterwards on the same node
    blocks.pop_back();
    blocks.push_back(std::make_shared<domain::message::block const>(valid));
    // headers are already in the index; run the batch from height 1 again
    CHECK(blockchain::validate_block_batch(fixture.chain(), s, domain::config::network::regtest, blocks, 1) == error::success);
}


TEST_CASE("BCHN feature_block b58 - spend of an output index that does not exist is refused", "[bchn_non_vm][block]") {
    blockchain::settings const s(domain::config::network::regtest);
    regtest_chain chain(110);
    chain.extend_to(101);
    auto const& cb = chain.coinbase_at(1);
    domain::chain::input::list inputs;
    inputs.emplace_back(output_point{cb.hash(), 1}, script{}, 0xffffffff);
    output::list outs;
    outs.emplace_back(1000, miner_script(), domain::chain::token_data_opt{});
    chain.append({transaction(1, 0, std::move(inputs), std::move(outs))});
    CHECK(validate_fresh(chain, s, "bchn_b58") == error::missing_previous_output);
}


TEST_CASE("BCHN feature_block b89a - spend of an OP_RETURN output is refused", "[bchn_non_vm][block]") {
    blockchain::settings const s(domain::config::network::regtest);
    regtest_chain chain(110);
    chain.extend_to(101);
    auto const& cb = chain.coinbase_at(1);
    output::list outs;
    outs.emplace_back(0, script(domain::machine::operation::list{domain::machine::operation(domain::machine::opcode::return_)}), domain::chain::token_data_opt{});
    outs.emplace_back(cb.outputs()[0].value() - 1000, miner_script(), domain::chain::token_data_opt{});
    auto const carrier = signed_spend(cb, 0, std::move(outs), s.enabled_flags());
    chain.append({carrier}, 1000);
    domain::chain::input::list inputs;
    inputs.emplace_back(output_point{carrier.hash(), 0}, script{}, 0xffffffff);
    output::list o2;
    o2.emplace_back(0, miner_script(), domain::chain::token_data_opt{});
    chain.append({transaction(1, 0, std::move(inputs), std::move(o2))});
    auto const ec = validate_fresh(chain, s, "bchn_b89a");
    INFO("ec " << ec.message());
    CHECK(ec == error::invalid_script);
}

namespace {

blockchain::mempool_entry pool_entry(transaction const& tx, uint64_t tag) {
    return blockchain::mempool_entry{std::make_shared<domain::message::transaction>(tx), 1000u + tag, uint32_t(tx.serialized_size(true)), 1u, tag};
}

} // namespace

TEST_CASE("BCHN txvalidationcache_tests - block double spend and mempool removal unit control",
          "[bchn_non_vm][block][mempool]") {
    blockchain::settings const s(domain::config::network::regtest);
    regtest_chain base(110);
    base.extend_to(101);
    auto const spend0 = spend_coinbase(base, 1, 1000, s.enabled_flags());
    auto const spend1 = spend_coinbase(base, 1, 2000, s.enabled_flags());   // same output, other fee: another transaction
    REQUIRE(spend0.hash() != spend1.hash());

    // The validator rejects the block double spend independently of mempool state.
    // The local pools below are separate unit controls, not connected to validate_fresh.
    regtest_chain both = base;
    both.append({spend0, spend1}, 3000);
    for (int pooled : {-1, 0, 1}) {
        blockchain::mempool pool{0x1111u, 0x2222u};
        if (pooled >= 0) REQUIRE(pool.add(pool_entry(pooled == 0 ? spend0 : spend1, 1)));
        char const* tag = pooled < 0 ? "bchn_txvc_none" : pooled == 0 ? "bchn_txvc_0" : "bchn_txvc_1";
        CHECK(validate_fresh(both, s, tag) == error::block_internal_double_spend);
    }

    // Independent controls: the single-spend block is valid; manually applying
    // that block to a local mempool removes its conflicting transaction.
    regtest_chain one = base;
    auto const& accepted = one.append({spend0}, 1000);
    blockchain::mempool pool{0x1111u, 0x2222u};
    REQUIRE(pool.add(pool_entry(spend1, 2)));
    CHECK(validate_fresh(one, s, "bchn_txvc_final") == error::success);
    pool.remove_for_block(accepted);
    CHECK(pool.size() == 0u);
}

// p2p_invalid_tx.py "Test spending of oversized outputs fails": an output whose scriptPubKey is longer than MAX_SCRIPT_SIZE
// (10000 bytes) can be created but never spent. This batch resolves the creator and spend together, so the
// rejection is invalid_script rather than an absent prevout. Spending at the inclusive limit is the control.
TEST_CASE("BCHN p2p_invalid_tx - an output with a scriptPubKey over 10000 bytes cannot be spent",
          "[bchn_non_vm][block][unspendable]") {
    blockchain::settings const s(domain::config::network::regtest);

    auto const build = [&](size_t script_size, char const* tag) {
        regtest_chain chain(115);
        chain.extend_to(101);
        // OP_TRUE OP_FALSE OP_IF PUSHDATA2 <padding> OP_ENDIF, exactly `script_size` bytes
        // One skipped data push keeps operation cost bounded while testing size.
        auto const payload_size = script_size - 7;
        data_chunk raw{0x51, 0x00, 0x63, 0x4d, uint8_t(payload_size), uint8_t(payload_size >> 8)};
        raw.insert(raw.end(), payload_size, 0x50);
        raw.push_back(0x68);
        byte_reader reader(raw);
        auto locking = script::from_data(reader, false);
        REQUIRE(locking);
        auto const& cb = chain.coinbase_at(1);
        output::list outs;
        outs.emplace_back(cb.outputs()[0].value() - 1000, std::move(*locking), domain::chain::token_data_opt{});
        auto const creator = signed_spend(cb, 0, std::move(outs), s.enabled_flags());
        chain.append({creator}, 1000);

        domain::chain::input::list ins;
        ins.emplace_back(output_point{creator.hash(), 0}, script{}, 0xffffffff);
        output::list o2;
        o2.emplace_back(creator.outputs()[0].value() - 1000, miner_script(), domain::chain::token_data_opt{});
        chain.append({transaction(1, 0, std::move(ins), std::move(o2))}, 1000);
        return validate_fresh(chain, s, tag);
    };

    // the output itself is created fine: a chain that stops at its creation is valid
    {
        regtest_chain chain(110);
        chain.extend_to(101);
        constexpr size_t payload_size = 10'001 - 7;
        data_chunk raw{0x51, 0x00, 0x63, 0x4d, uint8_t(payload_size), uint8_t(payload_size >> 8)};
        raw.insert(raw.end(), payload_size, 0x50);
        raw.push_back(0x68);
        byte_reader reader(raw);
        auto locking = script::from_data(reader, false);
        REQUIRE(locking);
        auto const& cb = chain.coinbase_at(1);
        output::list outs;
        outs.emplace_back(cb.outputs()[0].value() - 1000, std::move(*locking), domain::chain::token_data_opt{});
        chain.append({signed_spend(cb, 0, std::move(outs), s.enabled_flags())}, 1000);
        CHECK(validate_fresh(chain, s, "bchn_oversize_create") == error::success);
    }
    // The maximum-size output remains spendable; one more byte is refused.
    CHECK(build(10'000, "bchn_maxsize_spend") == error::success);
    CHECK(build(10'001, "bchn_oversize_spend") == error::invalid_script);
}
