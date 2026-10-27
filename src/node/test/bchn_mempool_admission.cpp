// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Scenarios of Bitcoin Cash Node's test/functional/mempool_accept.py and
// mempool_spend_coinbase.py (commit 07576013c91ff4a3a74acd85f189c69121cdad1b) driven through the
// real mempool admission of a regtest chain: block_chain::transaction_validate (the verdict,
// nothing admitted) and block_chain::organize (admission and announcement).
// Original: Copyright (c) 2014-present The Bitcoin Core / Bitcoin developers, MIT license.
//
// BCHN answers with a reject string; each scenario states the Knuth code asserted next to it.
// The chain is 100 blocks long: the coinbase of block 1 has matured, that of block 2 has not.

#include <test_helpers.hpp>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>

#include <kth/blockchain/pools/mempool.hpp>

#include "sync_harness.hpp"
#include "../../blockchain/test/bchn_block_builder.hpp"

using namespace kth;
using namespace kth::test;
namespace chain = kth::domain::chain;

namespace {

// Run one of the chain's coroutines on the chain's own pool and wait for it. The wait is
// unbounded on purpose, for the reason given in mempool_admission.cpp.
template <typename Make>
code run_on_chain(blockchain::block_chain& chain, Make make) {
    return ::asio::co_spawn(chain.executor(),
        [make = std::move(make)]() -> ::asio::awaitable<code> { co_return co_await make(); },
        ::asio::use_future).get();
}

struct admission_chain {
    test::chain_fixture fixture;
    std::vector<chain::block> trunk;

    explicit admission_chain(char const* tag, uint32_t trunk_len = 100) : fixture(tag) {
        REQUIRE(fixture.created());
        REQUIRE(fixture.start());
        auto prev = chain::block::genesis_regtest().hash();
        auto const base_time = uint32_t(zulu_time()) - (trunk_len + 30) * block_spacing;
        for (uint32_t h = 1; h <= trunk_len; ++h) {
            trunk.push_back(test::mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
            prev = trunk.back().hash();
        }
        REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == trunk_len);
        persist_headers(fixture, trunk, 1);
        connect_bodies(fixture, trunk, 1);
    }

    blockchain::block_chain& chain() { return fixture.chain(); }
    domain::script_flags_t flags() { return chain().chain_settings().enabled_flags(); }

    chain::transaction const& coinbase(uint32_t h) const { return trunk.at(h - 1).transactions().front(); }

    // A signed spend of the output of coinbase `h`, paying `value`, with the given version, locktime and sequence.
    chain::transaction spend(uint32_t h, uint64_t value, uint32_t version = 1, uint32_t locktime = 0, uint32_t sequence = 0xffffffff) {
        auto const& cb = coinbase(h);
        chain::input::list inputs;
        inputs.emplace_back(chain::output_point{cb.hash(), 0}, chain::script{}, sequence);
        chain::output::list outs;
        outs.emplace_back(value, miner_script(), chain::token_data_opt{});
        chain::transaction unsigned_tx(version, locktime, std::move(inputs), chain::output::list(outs));
        unsigned_tx.inputs()[0].previous_output().validation.cache = cb.outputs()[0];
        auto sig = chain::script::create_endorsement(miner_secret(), miner_script(), unsigned_tx, 0,
            uint8_t(infrastructure::machine::sighash_algorithm::forkid_all), flags(), cb.outputs()[0].value());
        REQUIRE(sig);
        chain::input::list signed_ins;
        signed_ins.emplace_back(chain::output_point{cb.hash(), 0},
            chain::script(chain::script::to_pay_public_key_hash_pattern_unlocking(*sig, miner_public())), sequence);
        return chain::transaction(version, locktime, std::move(signed_ins), std::move(outs));
    }

    code validate(chain::transaction const& tx) {
        auto ptr = std::make_shared<domain::message::transaction>(tx);
        return run_on_chain(chain(), [this, ptr] { return chain().transaction_validate(ptr); });
    }

    code organize(chain::transaction const& tx) {
        auto ptr = std::make_shared<domain::message::transaction>(tx);
        return run_on_chain(chain(), [this, ptr] { return chain().organize(ptr); });
    }
};

constexpr uint64_t fee = 1000;

} // namespace

// "A transaction not in the mempool" / "A final transaction not in the mempool"
TEST_CASE("BCHN mempool_accept - a valid transaction is accepted and admitted", "[bchn_non_vm][mempool][admission]") {
    admission_chain c("bchn_mp_ok");
    auto const tx = c.spend(1, c.coinbase(1).outputs()[0].value() - fee);
    CHECK(c.validate(tx) == error::success);
    CHECK(c.chain().mempool_ref().size() == 0u);          // validation admits nothing
    CHECK(c.organize(tx) == error::success);
    CHECK(c.chain().mempool_ref().contains(tx.hash()));
}

// "A transaction in the mempool" (txn-already-in-mempool)
TEST_CASE("BCHN mempool_accept - the same transaction twice", "[bchn_non_vm][mempool][admission]") {
    admission_chain c("bchn_mp_twice");
    auto const tx = c.spend(1, c.coinbase(1).outputs()[0].value() - fee);
    REQUIRE(c.organize(tx) == error::success);
    auto const again = c.organize(tx);
    CHECK(again == error::double_spend_mempool);       // BCHN: txn-already-in-mempool
    CHECK(c.chain().mempool_ref().size() == 1u);
}

// "A transaction that conflicts with an unconfirmed tx" (txn-mempool-conflict)
TEST_CASE("BCHN mempool_accept - a spend of an output a pooled transaction already spends", "[bchn_non_vm][mempool][admission]") {
    admission_chain c("bchn_mp_conflict");
    auto const value = c.coinbase(1).outputs()[0].value();
    REQUIRE(c.organize(c.spend(1, value - fee)) == error::success);
    auto const conflicting = c.spend(1, value - 2 * fee);
    CHECK(c.organize(conflicting) == error::double_spend_mempool);
    CHECK( ! c.chain().mempool_ref().contains(conflicting.hash()));
}

// "A transaction with missing inputs, that never existed"
TEST_CASE("BCHN mempool_accept - a spend of an output that never existed", "[bchn_non_vm][mempool][admission]") {
    admission_chain c("bchn_mp_missing");
    auto tx = c.spend(1, c.coinbase(1).outputs()[0].value() - fee);
    hash_digest ghost{};
    ghost.fill(0x5a);
    chain::input::list inputs;
    inputs.emplace_back(chain::output_point{ghost, 0}, tx.inputs()[0].script(), 0xffffffff);
    chain::transaction const orphan(1, 0, std::move(inputs), chain::output::list(tx.outputs()));
    CHECK(c.validate(orphan) == error::missing_previous_output);
}

// Structural rules at the head of the mempool_accept scenarios
TEST_CASE("BCHN mempool_accept - transaction structure", "[bchn_non_vm][mempool][admission]") {
    admission_chain c("bchn_mp_struct");
    auto const base = c.spend(1, c.coinbase(1).outputs()[0].value() - fee);
    auto const with_outputs = [&](chain::output::list outs) {
        return chain::transaction(base.version(), base.locktime(), chain::input::list(base.inputs()), std::move(outs));
    };
    SECTION("control") { CHECK(c.validate(base) == error::success); }
    SECTION("no outputs") { CHECK(c.validate(with_outputs({})) == error::empty_transaction); }
    SECTION("negative output value (as a huge unsigned)") {
        CHECK(c.validate(with_outputs({chain::output(uint64_t(-1), miner_script(), chain::token_data_opt{})})) == error::spend_overflow);
    }
    SECTION("output above the maximum money") {
        CHECK(c.validate(with_outputs({chain::output(max_satoshi_supply + 1, miner_script(), chain::token_data_opt{})})) == error::spend_overflow);
    }
    SECTION("sum of outputs above the maximum money") {
        CHECK(c.validate(with_outputs({chain::output(max_satoshi_supply, miner_script(), chain::token_data_opt{}),
                                       chain::output(1, miner_script(), chain::token_data_opt{})})) == error::spend_overflow);
    }
    SECTION("a coinbase") {
        chain::input::list inputs;
        inputs.emplace_back(chain::output_point{null_hash, chain::point::null_index}, chain::script(data_chunk(10, 0x51), false), 0xffffffff);
        chain::transaction const coinbase(1, 0, std::move(inputs), chain::output::list(base.outputs()));
        CHECK(c.validate(coinbase) == error::coinbase_transaction);
    }
}

// "A transaction with duplicate inputs": BCHN switches its algorithm at 120 inputs, so the sizes around
// it are covered, with the duplicate at the start, the end and the middle.
TEST_CASE("BCHN mempool_accept - duplicate inputs at every size", "[bchn_non_vm][mempool][admission]") {
    admission_chain c("bchn_mp_dups");
    auto const base = c.spend(1, c.coinbase(1).outputs()[0].value() - fee);
    for (size_t count : {size_t(2), size_t(3), size_t(119), size_t(120), size_t(121), size_t(400)}) {
        for (char const* where : {"first", "middle", "last"}) {
            DYNAMIC_SECTION(count << " inputs, duplicate at the " << where) {
                chain::input::list inputs;
                for (size_t i = 0; i < count; ++i) {
                    hash_digest h{};
                    h.fill(0x70);
                    h[1] = uint8_t(i);
                    h[2] = uint8_t(i >> 8);
                    inputs.emplace_back(chain::output_point{h, uint32_t(i)}, chain::script(data_chunk(4, 0x51), false), 0xffffffff);
                }
                size_t const at = std::string(where) == "first" ? 0 : std::string(where) == "last" ? count - 1 : count / 2;
                inputs[at == 0 ? count - 1 : 0] = inputs[at];       // repeat one of them
                chain::transaction const tx(1, 0, std::move(inputs), chain::output::list(base.outputs()));
                CHECK(tx.check(1'000'000, true) == error::transaction_internal_double_spend);
            }
        }
    }
    // control: the same shapes with all inputs distinct are not refused by the structural check
    chain::input::list distinct;
    for (size_t i = 0; i < 400; ++i) {
        hash_digest h{};
        h.fill(0x70);
        h[1] = uint8_t(i);
        h[2] = uint8_t(i >> 8);
        distinct.emplace_back(chain::output_point{h, uint32_t(i)}, chain::script(data_chunk(4, 0x51), false), 0xffffffff);
    }
    chain::transaction const ok(1, 0, std::move(distinct), chain::output::list(base.outputs()));
    CHECK(ok.check(1'000'000, true) == error::success);
}

// "A timelocked transaction" (bad-txns-nonfinal)
TEST_CASE("BCHN mempool_accept - locktime", "[bchn_non_vm][mempool][admission]") {
    admission_chain c("bchn_mp_locktime");
    auto const value = c.coinbase(1).outputs()[0].value() - fee;
    CHECK(c.validate(c.spend(1, value, 1, /*locktime*/ 50, /*sequence*/ 0xfffffffe)) == error::success);          // already passed
    CHECK(c.validate(c.spend(1, value, 1, /*locktime*/ 500, /*sequence*/ 0xfffffffe)) == error::transaction_non_final);
    CHECK(c.validate(c.spend(1, value, 1, /*locktime*/ 500, /*sequence*/ 0xffffffff)) == error::success);         // final inputs ignore it
}

// "A transaction that is locked by BIP68 sequence logic" (non-BIP68-final)
TEST_CASE("BCHN mempool_accept - BIP68 sequence lock controls", "[bchn_non_vm][mempool][admission]") {
    admission_chain c("bchn_mp_bip68");
    auto const value = c.coinbase(1).outputs()[0].value() - fee;
    // the coinbase of block 1 is 100 blocks old for the next block: these are the accepting controls.
    // The refusal of a lock of 101 blocks (sequence_locked) is NOT here: the pool is never given the
    // bip68 rule flag for BCH, failed_regression csv_rules_activation in coverage.json.
    CHECK(c.validate(c.spend(1, value, 2, 0, 100)) == error::success);
    CHECK(c.validate(c.spend(1, value, 1, 0, 101)) == error::success);       // version 1: no relative lock
}

// mempool_spend_coinbase.py: a coinbase can be spent in the block N + 100 or later
TEST_CASE("BCHN mempool_spend_coinbase - maturity in the mempool", "[bchn_non_vm][mempool][admission]") {
    admission_chain c("bchn_mp_maturity");
    CHECK(c.validate(c.spend(1, c.coinbase(1).outputs()[0].value() - fee)) == error::success);     // 100 deep
    CHECK(c.validate(c.spend(2, c.coinbase(2).outputs()[0].value() - fee)) == error::coinbase_maturity);   // 99 deep
}

// mining_prioritisetransaction.py "Create a free transaction. Should be rejected": a transaction that pays no fee is refused for relay
// (BCHN: min relay fee not met, code 66). The BCHN threshold is 1 satoshi per byte (-minrelaytxfee 1000 sat/kB); Knuth prices a
// transaction as byte_fee_satoshis * size + sigop_fee_satoshis * sigops (0.1 and 100), so only the free case coincides.
TEST_CASE("BCHN mining_prioritisetransaction - a free transaction is refused", "[bchn_non_vm][mempool][admission]") {
    admission_chain c("bchn_mp_free");
    auto const value = c.coinbase(1).outputs()[0].value();
    CHECK(c.validate(c.spend(1, value)) == error::insufficient_fee);              // pays nothing
    CHECK(c.validate(c.spend(1, value - 100000)) == error::success);               // control: pays a fee
}
