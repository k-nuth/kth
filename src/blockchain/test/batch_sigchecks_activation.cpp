// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <kth/infrastructure/utility/timer.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/batch_validate.hpp>

#include "regtest_miner.hpp"
#include "reorg_chain_fixture.hpp"

using namespace kth;
using namespace kth::blockchain;
using namespace kth::test;

// =============================================================================
// The SigChecks limits are consensus only from the May 2020 upgrade
// =============================================================================
//
// A full-validation sync from genesis stopped at mainnet block 364292
// (000000000000000003dd2fdbb484d6d9c349d644d8bbb3cbfa5e67f639a465fe, 2015):
// its second transaction, bb41a757f405890fb0f5856228e23b715702d714d59bf2b1feb70d8b2b4e3e08,
// spends 5569 P2PKH outputs — 5569 SigChecks — and the batch validator rejected
// the batch with transaction_sigchecks_limit. The per-transaction limit of 3000
// and the per-block limit of max_block_size / 141 replaced the SigOps rules at
// the May 2020 upgrade (2020-05-15-sigchecks: "After activation, any transaction
// where the total number of SigChecks ... shall be rejected"); before it, blocks
// with transactions far above 3000 are valid and are in the chain.
//
// The node carries that activation as the bch_enforce_sigchecks flag, which the
// per-height chain_state sets from mainnet height 635259 on. What BCHN does with
// a block below it: the per-transaction limiter is not removed but set to 20000
// (TxSigCheckLimiter::getDisabled — no historical transaction exceeds that), and
// the per-block limiter is applied as always. validate_block_batch counts every
// input's SigChecks through the script interpreter — the count is not itself
// gated, and must not be — and takes the per-transaction limit from the flags of
// the block each transaction is in.
//
// The flag set models the activation at the validator's own boundary: in regtest
// every height-based rule is on, so the pre-activation set is the settings with
// the May 2020 upgrade and everything after it switched off. The mainnet height
// at which chain_state turns the flag on is asserted separately. A batch that
// straddles the activation height cannot be built on regtest; the per-block
// selection is pinned on the limiter itself in batch_sigchecks.cpp.

namespace {

using domain::chain::block;
using domain::chain::input;
using domain::chain::output;
using domain::chain::output_point;
using domain::chain::script;
using domain::chain::transaction;
using domain::machine::opcode;
using domain::machine::operation;

constexpr uint64_t funded_value = 1000;
constexpr uint32_t multisig_keys = 20;   // MAX_PUBKEYS_PER_MULTISIG

// The settings of a node whose per-height rules stop at the November 2019
// upgrade: everything the chain enforced before SigChecks existed, and nothing
// that carries the bch_enforce_sigchecks bit.
settings pre_sigchecks_settings() {
    settings s(domain::config::network::regtest);
    s.bch_fermat = false;
    s.bch_euler = false;
    s.bch_gauss = false;
    s.bch_descartes = false;
    s.bch_lobachevski = false;
    s.bch_galois = false;
    s.bch_leibniz = false;
    return s;
}

// The settings of a node whose rules run from the May 2020 upgrade to the May
// 2024 one: bch_enforce_sigchecks is on, and the May 2025 VM limits are not.
// Those limits price a CHECKMULTISIG at 26000 per key against a budget drawn
// from the unlocking script's size, which a 1-of-20 multisig spent by one
// signature cannot meet; the multisig cases below need the limits off to reach
// the SigChecks limiter at all. The P2PKH cases run under every rule.
settings post_sigchecks_settings() {
    settings s(domain::config::network::regtest);
    s.bch_galois = false;
    s.bch_leibniz = false;
    return s;
}

// A bare 1-of-20 CHECKMULTISIG locking script on the miner's key. One ECDSA
// signature satisfies it and the interpreter tallies 20 SigChecks for it (the
// key count, in legacy multisig mode): the densest SigChecks per byte a spend can
// have, which is what reaching a per-block limit of max_block_size / 141 needs.
script const& multisig_script() {
    static script const locking = [] {
        operation::list ops;
        ops.emplace_back(opcode::push_positive_1);
        for (uint32_t i = 0; i < multisig_keys; ++i) {
            ops.emplace_back(miner_public().to_data(), true);
        }
        ops.emplace_back(data_chunk{uint8_t(multisig_keys)}, true);
        ops.emplace_back(opcode::checkmultisig);
        return script(std::move(ops));
    }();
    return locking;
}

enum class spend_kind { p2pkh, multisig_1_of_20 };

script const& locking_script(spend_kind kind) {
    return kind == spend_kind::p2pkh ? miner_script() : multisig_script();
}

script unlocking_script(spend_kind kind, endorsement const& endorsement) {
    if (kind == spend_kind::p2pkh) {
        return script(script::to_pay_public_key_hash_pattern_unlocking(endorsement, miner_public()));
    }
    operation::list ops;
    ops.emplace_back(opcode::push_size_0);   // the CHECKMULTISIG dummy
    ops.emplace_back(endorsement, true);
    return script(std::move(ops));
}

// A transaction that spends `prev`'s output `index` into `count` outputs of
// funded_value each, locked as `kind`, signed by the miner.
transaction fan_out(transaction const& prev, uint32_t index, uint32_t count, spend_kind kind,
                    domain::script_flags_t flags, uint64_t each = funded_value) {
    // Hashed once: the funding transaction can carry thousands of outputs, and
    // its hash is not cached across calls.
    auto const prev_hash = prev.hash();
    input::list inputs;
    inputs.emplace_back(output_point{prev_hash, index}, script{}, 0xffffffff);
    output::list outputs;
    outputs.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        outputs.emplace_back(each, locking_script(kind), domain::chain::token_data_opt{});
    }
    transaction unsigned_tx(1, 0, std::move(inputs), std::move(outputs));
    unsigned_tx.inputs()[0].previous_output().validation.cache = prev.outputs()[index];

    auto const sighash_type = uint8_t(infrastructure::machine::sighash_algorithm::forkid_all);
    auto endorsement = script::create_endorsement(miner_secret(), miner_script(), unsigned_tx, 0,
        sighash_type, flags, prev.outputs()[index].value());
    REQUIRE(endorsement);

    input::list signed_inputs;
    signed_inputs.emplace_back(output_point{prev_hash, index},
        unlocking_script(spend_kind::p2pkh, *endorsement), 0xffffffff);
    return transaction(1, 0, std::move(signed_inputs), output::list(unsigned_tx.outputs()));
}

// The funded outputs, spread over as many funding transactions as the
// per-transaction size cap (1,000,000 bytes) requires. Output `n` is output
// `n % per_tx` of funding transaction `n / per_tx`.
struct funded_outputs {
    std::vector<transaction> txs;
    std::vector<hash_digest> hashes;
    uint32_t per_tx;
};

// A transaction that spends outputs [first, first + count) of the funded
// outputs — one signature per input — into a single output.
transaction fan_in(funded_outputs const& funded, uint32_t first, uint32_t count, spend_kind kind,
                   domain::script_flags_t flags) {
    auto const source = [&](uint32_t n) {
        return std::pair<size_t, uint32_t>{n / funded.per_tx, n % funded.per_tx};
    };
    input::list inputs;
    inputs.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto const [tx, index] = source(first + i);
        inputs.emplace_back(output_point{funded.hashes[tx], index}, script{}, 0xffffffff);
    }
    output::list outputs;
    outputs.emplace_back(uint64_t(count) * funded_value - 1000, miner_script(),
        domain::chain::token_data_opt{});
    transaction unsigned_tx(1, 0, std::move(inputs), std::move(outputs));
    for (uint32_t i = 0; i < count; ++i) {
        auto const [tx, index] = source(first + i);
        unsigned_tx.inputs()[i].previous_output().validation.cache = funded.txs[tx].outputs()[index];
    }

    auto const sighash_type = uint8_t(infrastructure::machine::sighash_algorithm::forkid_all);
    input::list signed_inputs;
    signed_inputs.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto const [tx, index] = source(first + i);
        auto endorsement = script::create_endorsement(miner_secret(), locking_script(kind), unsigned_tx, i,
            sighash_type, flags, funded_value);
        REQUIRE(endorsement);
        signed_inputs.emplace_back(output_point{funded.hashes[tx], index},
            unlocking_script(kind, *endorsement), 0xffffffff);
    }
    return transaction(1, 0, std::move(signed_inputs), output::list(unsigned_tx.outputs()));
}

// Heights 1..102 on regtest genesis: 100 coinbase-only blocks, then the coinbase
// of height 1 (mature at 101) fanned out into `outputs` outputs of `kind`, then
// at height 102 one spending transaction per entry of `spends` (each spends the
// next `n` outputs: n SigChecks for P2PKH, 20 n for the 1-of-20 multisig).
// Everything a batch needs is inside the batch, so the validator resolves every
// prevout from what the batch itself created and the only chain it needs is the
// genesis header the fixture already holds.
std::vector<domain::message::block::const_ptr> chain_with_spends(
    spend_kind kind, std::vector<uint32_t> const& spends, domain::script_flags_t flags) {

    uint32_t outputs = 0;
    for (auto const n : spends) outputs += n;
    REQUIRE(outputs > 0);

    constexpr uint32_t len = 102;
    auto const base = uint32_t(zulu_time()) - (len + 30) * 600;
    std::vector<domain::message::block::const_ptr> blocks;
    blocks.reserve(len);

    // 1000 bare 1-of-20 multisig outputs are ~700 KB: under the 1,000,000-byte
    // consensus cap on one transaction. More than that is funded by several
    // transactions, each paid by one output of a splitting transaction.
    constexpr uint32_t outputs_per_funding = 1000;

    auto prev = block::genesis_regtest().hash();
    funded_outputs funded{{}, {}, outputs_per_funding};
    for (uint32_t h = 1; h <= len; ++h) {
        std::vector<transaction> txs;
        uint64_t fees = 0;
        if (h == 101) {
            auto const& coinbase_one = blocks.front()->transactions().front();
            auto const fundings = (outputs + outputs_per_funding - 1) / outputs_per_funding;
            if (fundings == 1) {
                funded.txs.push_back(fan_out(coinbase_one, 0, outputs, kind, flags));
            } else {
                // Each splitter output pays one funding transaction's outputs and
                // its fee; the unused remainder of the last one becomes fee.
                auto const each = uint64_t(outputs_per_funding) * funded_value + 1000;
                auto const splitter = fan_out(coinbase_one, 0, fundings, spend_kind::p2pkh, flags, each);
                txs.push_back(splitter);
                for (uint32_t f = 0; f < fundings; ++f) {
                    auto const count = std::min(outputs_per_funding, outputs - f * outputs_per_funding);
                    funded.txs.push_back(fan_out(splitter, f, count, kind, flags));
                }
            }
            for (auto const& tx : funded.txs) {
                funded.hashes.push_back(tx.hash());
                txs.push_back(tx);
            }
            fees = coinbase_one.outputs()[0].value() - uint64_t(outputs) * funded_value;
        } else if (h == 102) {
            uint32_t first = 0;
            for (auto const n : spends) {
                txs.push_back(fan_in(funded, first, n, kind, flags));
                first += n;
                fees += 1000;
            }
        }
        auto mined = mine_block(prev, h, base + h * 600, 0, std::move(txs), fees);
        prev = mined.hash();
        blocks.push_back(std::make_shared<domain::message::block const>(std::move(mined)));
    }
    return blocks;
}

// The headers go into the index first, as they are on a node (headers are
// synced before any body): the validator builds each block's chain state from
// its parent's entry there.
code validate(chain_fixture& fixture, settings const& s,
              std::vector<domain::message::block::const_ptr> const& blocks) {
    domain::message::header::list headers;
    headers.reserve(blocks.size());
    for (auto const& blk : blocks) {
        headers.push_back(blk->header());
    }
    auto const added = fixture.organizer().add_headers(headers);
    REQUIRE((added.headers_added == blocks.size() || added.error == error::stale_chain));
    return validate_block_batch(fixture.chain(), s, domain::config::network::regtest, blocks, 1);
}

} // namespace

// -----------------------------------------------------------------------------
// Activation: height and flags
// -----------------------------------------------------------------------------

TEST_CASE("sigchecks activation - the flag turns on at the May 2020 mainnet height",
          "[sigchecks][activation]") {
    using domain::chain::chain_state;
    using domain::config::network;
    CHECK_FALSE(chain_state::is_fermat_enabled(364292, network::mainnet));
    CHECK_FALSE(chain_state::is_fermat_enabled(635258, network::mainnet));
    CHECK(chain_state::is_fermat_enabled(635259, network::mainnet));

    auto const pre = pre_sigchecks_settings().enabled_flags();
    auto const post = post_sigchecks_settings().enabled_flags();
    auto const all = settings(network::regtest).enabled_flags();
    CHECK_FALSE(script::is_enabled(pre, domain::machine::script_flags::bch_enforce_sigchecks));
    CHECK(script::is_enabled(post, domain::machine::script_flags::bch_enforce_sigchecks));
    CHECK(script::is_enabled(all, domain::machine::script_flags::bch_enforce_sigchecks));
    CHECK(script::is_enabled(pre, domain::machine::script_flags::bch_sighash_forkid));
    CHECK_FALSE(script::is_enabled(post, domain::machine::script_flags::bch_vm_limits));
    CHECK(script::is_enabled(all, domain::machine::script_flags::bch_vm_limits));
}

// -----------------------------------------------------------------------------
// The per-transaction limit, around the activation: 3000 / 3001 (the mainnet
// case) and 20000 / 20001 (what BCHN keeps before the activation)
// -----------------------------------------------------------------------------

TEST_CASE("sigchecks activation - a 3001-SigCheck transaction is valid before the limit exists",
          "[sigchecks][activation]") {
    chain_fixture fixture("sigchecks_pre_3001");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const pre = pre_sigchecks_settings();
    auto const blocks = chain_with_spends(spend_kind::p2pkh, {3001}, pre.enabled_flags());
    REQUIRE(blocks.back()->transactions()[1].inputs().size() == 3001u);

    auto const ec = validate(fixture, pre, blocks);
    CHECK(ec == error::success);
    CHECK(ec != error::transaction_sigchecks_limit);
}

TEST_CASE("sigchecks activation - a 3001-SigCheck transaction is rejected once the limit exists",
          "[sigchecks][activation]") {
    chain_fixture fixture("sigchecks_post_3001");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    settings const post(domain::config::network::regtest);
    auto const blocks = chain_with_spends(spend_kind::p2pkh, {3001}, post.enabled_flags());
    CHECK(validate(fixture, post, blocks) == error::transaction_sigchecks_limit);
}

TEST_CASE("sigchecks activation - 3000 SigChecks are within the limit either way",
          "[sigchecks][activation]") {
    chain_fixture fixture("sigchecks_3000");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    settings const post(domain::config::network::regtest);
    CHECK(validate(fixture, post, chain_with_spends(spend_kind::p2pkh, {3000}, post.enabled_flags()))
        == error::success);

    auto const pre = pre_sigchecks_settings();
    CHECK(validate(fixture, pre, chain_with_spends(spend_kind::p2pkh, {3000}, pre.enabled_flags()))
        == error::success);
}

TEST_CASE("sigchecks activation - before the activation a transaction may reach 20000 SigChecks, not more",
          "[sigchecks][activation]") {
    // 1-of-20 multisig: 20 SigChecks per input. 1000 inputs reach exactly the
    // limit BCHN keeps before the activation; 1001 exceed it.
    chain_fixture fixture("sigchecks_pre_20000");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const pre = pre_sigchecks_settings();
    CHECK(validate(fixture, pre, chain_with_spends(spend_kind::multisig_1_of_20, {1000}, pre.enabled_flags()))
        == error::success);
    CHECK(validate(fixture, pre, chain_with_spends(spend_kind::multisig_1_of_20, {1001}, pre.enabled_flags()))
        == error::transaction_sigchecks_limit);
}

TEST_CASE("sigchecks activation - after the activation the multisig count is held to 3000 too",
          "[sigchecks][activation]") {
    // 150 inputs are 3000 SigChecks; 151 are 3020.
    chain_fixture fixture("sigchecks_post_multisig");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const post = post_sigchecks_settings();
    CHECK(validate(fixture, post, chain_with_spends(spend_kind::multisig_1_of_20, {150}, post.enabled_flags()))
        == error::success);
    CHECK(validate(fixture, post, chain_with_spends(spend_kind::multisig_1_of_20, {151}, post.enabled_flags()))
        == error::transaction_sigchecks_limit);
}

// -----------------------------------------------------------------------------
// The per-block limit applies before and after the activation alike
// -----------------------------------------------------------------------------

TEST_CASE("sigchecks activation - the per-block limit is enforced before and after the activation",
          "[sigchecks][activation][block_limit]") {
    // regtest's block size is 32'000'000 bytes, so the per-block limit is
    // 226'950 SigChecks. 76 transactions of 150 multisig inputs are 3000 SigChecks
    // each — within the per-transaction limit under both rule sets — and 228'000
    // in the block.
    auto const post = post_sigchecks_settings();
    auto const pre = pre_sigchecks_settings();
    std::vector<uint32_t> const spends(76, 150);

    chain_fixture fixture("sigchecks_block_limit");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    auto const over_pre = chain_with_spends(spend_kind::multisig_1_of_20, spends, pre.enabled_flags());
    REQUIRE(over_pre.back()->transactions().size() == 77u);
    CHECK(validate(fixture, pre, over_pre) == error::block_sigchecks_limit);

    auto const over_post = chain_with_spends(spend_kind::multisig_1_of_20, spends, post.enabled_flags());
    CHECK(validate(fixture, post, over_post) == error::block_sigchecks_limit);
}
