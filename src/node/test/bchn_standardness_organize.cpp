// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Relay admission through block_chain::organize(tx), the entry that inserts into the mempool and announces (sendrawtransaction and the persisted-mempool
// revalidation use it; this tree has no inbound p2p transaction protocol, only send_transaction in network/src/protocols_coro.cpp), for the
// IsStandardTx / AreInputsStandard scenarios of Bitcoin Cash Node (commit 07576013c91ff4a3a74acd85f189c69121cdad1b). organize runs the same checks as
// transaction_validate in the same order (transaction_organizer.cpp), then add() and notify(). Controls (passing): a standard transaction is inserted and
// announced; the same transaction with the switch off, and a non-standard one with the switch off, are inserted and announced; transactions that both
// implementations refuse (dust, versions 0 and 3 after Upgrade 9) get the same code as from transaction_validate and are neither inserted nor announced.
// A fresh chain per case: an inserted transaction spends the funding output, so a second one on the same chain would be a mempool double spend.
// The negatives BCHN refuses and Knuth inserts are a failed regression kept outside this tree (evidence round9/standardness_organize).
// Original: Copyright (c) 2009-present The Bitcoin Core / Bitcoin developers, MIT license.

#include "bchn_standardness_harness.hpp"

using namespace kth;
using namespace kth::test::standardness;

namespace {

struct outcome { code verdict; code organized; bool pooled; bool announced; };

// validate (nothing inserted), then organize on the same chain with a subscription open
template <typename Build>
outcome run(char const* tag, bool require_standard, phase ph, Build build) {
    harness h(tag, require_standard, ph);
    auto const tx = build(h);
    outcome o;
    o.verdict = h.validate(tx);
    auto const channel = h.subscribe();
    o.organized = h.organize(tx);
    o.pooled = h.pooled(tx);
    o.announced = harness::announced(channel, tx.hash());
    h.fixture.chain().unsubscribe_transaction(channel);
    return o;
}

} // namespace

TEST_CASE("BCHN IsStandardTx through organize - standard transactions are inserted and announced", "[bchn_non_vm][standardness][organize]") {
    auto const p2pkh_tx = [](harness& h) { return h.spend(0, harness::pay(p2pkh(), 90'000'000)); };
    DYNAMIC_SECTION("standard P2PKH, all upgrades") {
        auto const o = run("bchn_org_std", true, phase::all, p2pkh_tx);
        CHECK(o.verdict == error::success);
        CHECK(o.organized == error::success);
        CHECK(o.pooled);
        CHECK(o.announced);
    }
    DYNAMIC_SECTION("standard P2PKH before Upgrade 12") {
        auto const o = run("bchn_org_std_u11", true, phase::before_u12, p2pkh_tx);
        CHECK(o.organized == error::success);
        CHECK(o.pooled);
        CHECK(o.announced);
    }
    DYNAMIC_SECTION("standard P2PKH before Upgrade 9") {
        auto const o = run("bchn_org_std_u8", true, phase::before_u9, p2pkh_tx);
        CHECK(o.organized == error::success);
        CHECK(o.pooled);
        CHECK(o.announced);
    }
    DYNAMIC_SECTION("null data of 223 bytes") {
        auto const o = run("bchn_org_nd", true, phase::all, [](harness& h) { return h.spend(0, harness::pay(null_data(223), 0)); });
        CHECK(o.organized == error::success);
        CHECK(o.pooled);
    }
}

TEST_CASE("BCHN IsStandardTx through organize - with the switch off the non-standard transactions are inserted", "[bchn_non_vm][standardness][organize]") {
    DYNAMIC_SECTION("224 bytes of null data, switch off") {
        auto const o = run("bchn_org_off_nd", false, phase::all, [](harness& h) { return h.spend(0, harness::pay(null_data(224), 0)); });
        CHECK(o.organized == error::success);
        CHECK(o.pooled);
        CHECK(o.announced);
    }
    DYNAMIC_SECTION("bare OP_1 output before Upgrade 12, switch off") {
        auto const o = run("bchn_org_off_u11", false, phase::before_u12, [](harness& h) { return h.spend(0, harness::pay(bare_one())); });
        CHECK(o.organized == error::success);
        CHECK(o.pooled);
        CHECK(o.announced);
    }
}

TEST_CASE("BCHN IsStandardTx through organize - what both refuse is neither inserted nor announced", "[bchn_non_vm][standardness][organize]") {
    DYNAMIC_SECTION("dust: 200 satoshis to a P2PK output") {
        auto const o = run("bchn_org_dust", true, phase::all, [](harness& h) { return h.spend(0, harness::pay(p2pk(), 200)); });
        CHECK(o.verdict == error::dusty_transaction);
        CHECK(o.organized == error::dusty_transaction);
        CHECK_FALSE(o.pooled);
        CHECK_FALSE(o.announced);
    }
    DYNAMIC_SECTION("transaction version 3 after Upgrade 9") {
        auto const o = run("bchn_org_v3", true, phase::all, [](harness& h) { return h.spend(0, harness::pay(p2pkh()), 3); });
        CHECK(o.verdict == error::invalid_tx_version);
        CHECK(o.organized == error::invalid_tx_version);
        CHECK_FALSE(o.pooled);
        CHECK_FALSE(o.announced);
    }
    DYNAMIC_SECTION("scriptSig that is not push only") {
        auto const o = run("bchn_org_pushonly", true, phase::all, [](harness& h) {
            domain::machine::operation::list extra;
            extra.emplace_back(domain::machine::opcode::nop);
            return h.spend(1, harness::pay(p2pkh()), 1, std::move(extra));
        });
        CHECK(o.verdict == error::sig_pushonly);
        CHECK(o.organized == error::sig_pushonly);
        CHECK_FALSE(o.pooled);
    }
}

// Revalidation after a rule change. The upgrade masks are settings captured when the chain starts, so a phase change in a running chain is not a supported
// operation; what the node does support is a restart, which re-admits the persisted mempool (block_chain::load_mempool_from_disk -> organize). The first
// chain stands for the node before the upgrade, the second for the same node after it; the file is the mempool.dat the first one writes on shutdown.
// block_chain::update_for_reorg has a TODO for the outgoing blocks (mempool.cpp), so an in-process reorg or upgrade does not revalidate, and that is not reproduced.
TEST_CASE("BCHN transaction version rule - a pooled transaction is dropped when the node restarts after Upgrade 9", "[bchn_non_vm][standardness][organize]") {
    // version 3 is only non-standard before Upgrade 9 and consensus-invalid from it
    harness before("bchn_org_reval_before", false, phase::before_u9);
    auto const tx = before.spend(0, harness::pay(p2pkh()), 3);
    REQUIRE(before.organize(tx) == error::success);
    REQUIRE(before.pooled(tx));
    REQUIRE(before.fixture.chain().dump_mempool_to_disk());

    harness after("bchn_org_reval_after", false, phase::all);
    REQUIRE(after.funding.hash() == before.funding.hash());
    std::error_code ec;
    std::filesystem::copy_file(before.fixture.dir() / "mempool.dat", after.fixture.dir() / "mempool.dat", std::filesystem::copy_options::overwrite_existing, ec);
    REQUIRE_FALSE(ec);

    CHECK(after.validate(tx) == error::invalid_tx_version);
    auto const admitted = ::asio::co_spawn(after.fixture.chain().executor(),
        [&]() -> ::asio::awaitable<size_t> { co_return co_await after.fixture.chain().load_mempool_from_disk(); }, ::asio::use_future).get();
    CHECK(admitted == 0u);
    CHECK_FALSE(after.pooled(tx));
}

// rpc_rawtransaction.py ("sendrawtransaction with missing input", -25 "Missing inputs"): the relay entry refuses a transaction whose input is unknown,
// inserts nothing and announces nothing.
TEST_CASE("BCHN rpc_rawtransaction - organize refuses a transaction with a missing input", "[bchn_non_vm][standardness][organize]") {
    auto const o = run("bchn_org_orphan", true, phase::all, [](harness& h) {
        chain::input::list inputs;
        inputs.emplace_back(chain::output_point{hash_digest{{0x00, 0x00, 0x00, 0x00, 0xa0, 0xce, 0x6e, 0x35, 0x69, 0xa2, 0x7d, 0xe3, 0x1a, 0xce, 0x9e, 0xed,
                                                              0x79, 0xec, 0xfb, 0xd8, 0xfd, 0xf8, 0xc3, 0x84, 0x7e, 0x05, 0x99, 0xed, 0x24, 0x4e, 0x1d, 0x1d}}, 1},
                            chain::script{}, 0xffffffff);
        return chain::transaction(1, 0, std::move(inputs), harness::pay(p2pkh()));
    });
    CHECK(o.organized == error::missing_previous_output);
    CHECK_FALSE(o.pooled);
    CHECK_FALSE(o.announced);
}

// wallet_abandonconflict.py restarts the node with a higher -minrelaytxfee and expects the previous mempool transactions not to come back. Knuth's relay fee is
// byte_fee_satoshis; the first chain admits at the default fee, the second starts with a fee the transaction does not pay and reloads the file.
TEST_CASE("BCHN wallet_abandonconflict - a restart with a higher relay fee does not reload the pooled transaction", "[bchn_non_vm][standardness][organize]") {
    harness before("bchn_org_fee_before", true, phase::all);
    auto const tx = before.spend(0, harness::pay(p2pkh()));
    REQUIRE(before.organize(tx) == error::success);
    REQUIRE(before.fixture.chain().dump_mempool_to_disk());

    harness after("bchn_org_fee_after", true, phase::all, 1'000'000.0f);
    REQUIRE(after.funding.hash() == before.funding.hash());
    std::error_code ec;
    std::filesystem::copy_file(before.fixture.dir() / "mempool.dat", after.fixture.dir() / "mempool.dat", std::filesystem::copy_options::overwrite_existing, ec);
    REQUIRE_FALSE(ec);

    CHECK(after.validate(tx) == error::insufficient_fee);
    auto const admitted = ::asio::co_spawn(after.fixture.chain().executor(),
        [&]() -> ::asio::awaitable<size_t> { co_return co_await after.fixture.chain().load_mempool_from_disk(); }, ::asio::use_future).get();
    CHECK(admitted == 0u);
    CHECK_FALSE(after.pooled(tx));
}
