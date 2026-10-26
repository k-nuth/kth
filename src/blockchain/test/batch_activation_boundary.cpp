// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#include <kth/infrastructure/utility/timer.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/batch_validate.hpp>
#include <kth/blockchain/validate/validate_header.hpp>
#include <kth/infrastructure/math/hash.hpp>

#include "regtest_miner.hpp"
#include "reorg_chain_fixture.hpp"

using namespace kth;
using namespace kth::blockchain;
using namespace kth::test;

// =============================================================================
// Every block of a batch is validated with the rules of its own height
// =============================================================================
//
// validate_block_batch builds the chain state of its first block from the
// header index and then derived each next block's state from the previous one
// with chain_state::from_pool_ptr. That transition keeps the height: it promotes
// a pool state to the block state AT THE SAME height, which is its contract for
// populate_chain_state. Used block after block, every block of a batch was given
// the flags of the batch's first height, so a batch that began below an
// activation validated the blocks above it with the old rules — leniently for
// anything the activation forbids, and wrongly for anything it introduces.
//
// Regtest activates BIP65 (CHECKLOCKTIMEVERIFY) at height 1351 by freeze, the
// same mechanism mainnet uses at 388381. Below it the opcode is NOP2. A spend
// of a CLTV-locked output whose transaction cannot satisfy the lock is valid at
// 1350 and invalid at 1351, which makes the rules a block is judged by visible
// from outside the validator.
//
// Block versions are the minimum each height requires (2, then 3 from the BIP66
// freeze, then 4 from the BIP65 freeze). Regtest also activates these rules by
// version signalling with no history required, so a block carrying version 4
// below 1351 would turn CLTV on early and hide the boundary.

namespace {

using domain::chain::block;
using domain::chain::input;
using domain::chain::output;
using domain::chain::output_point;
using domain::chain::script;
using domain::chain::transaction;
using domain::machine::opcode;
using domain::machine::operation;

constexpr uint32_t cltv_height = uint32_t(regtest_bip65_freeze);   // 1351
constexpr uint32_t dersig_height = uint32_t(regtest_bip66_freeze); // 1251
constexpr uint32_t funding_height = 101;                            // coinbase 1 mature
constexpr uint64_t funded_value = 100'000;

uint32_t minimum_version(uint32_t height) {
    if (height >= cltv_height) return 4;
    if (height >= dersig_height) return 3;
    return 2;
}

// mine_block with the header version given: the regtest grind, bounded.
block mine_versioned_block(uint32_t version, hash_digest const& prev, uint32_t height,
                           uint32_t timestamp, std::vector<transaction> txs, uint64_t fees) {
    transaction::list all;
    all.reserve(txs.size() + 1);
    all.push_back(coinbase_tx(height, 0, block::subsidy(height, /*retarget*/ false) + fees));
    for (auto& tx : txs) {
        all.push_back(std::move(tx));
    }
    block candidate(domain::chain::header{version, prev, null_hash, timestamp, regtest_bits, 0},
                    std::move(all));
    auto const merkle = candidate.generate_merkle_root();
    for (uint64_t nonce = 0; nonce <= std::numeric_limits<uint32_t>::max(); ++nonce) {
        domain::chain::header header{version, prev, merkle, timestamp, regtest_bits, uint32_t(nonce)};
        if (header.is_valid_proof_of_work(domain::chain::hash(header), /*retarget*/ false)) {
            return block(std::move(header), transaction::list(candidate.transactions()));
        }
    }
    throw std::runtime_error("no nonce satisfied the regtest target");
}

// <far-future locktime> CHECKLOCKTIMEVERIFY DROP <miner key> CHECKSIG.
// The spending transaction has locktime 0 and a final sequence, so CLTV fails
// on both counts once it is enforced; before that it is a NOP and the DROP
// discards the pushed number.
script const& cltv_script() {
    static script const locking = [] {
        operation::list ops;
        ops.emplace_back(infrastructure::machine::number::from_int(int64_t(0x7fffffff))->data(), true);
        ops.emplace_back(opcode::checklocktimeverify);
        ops.emplace_back(opcode::drop);
        ops.emplace_back(miner_public().to_data(), true);
        ops.emplace_back(opcode::checksig);
        return script(std::move(ops));
    }();
    return locking;
}

// Coinbase 1 into one CLTV-locked output, signed by the miner.
transaction fund_cltv(transaction const& coinbase_one, domain::script_flags_t flags) {
    auto const prev_hash = coinbase_one.hash();
    input::list inputs;
    inputs.emplace_back(output_point{prev_hash, 0}, script{}, 0xffffffff);
    output::list outputs;
    outputs.emplace_back(funded_value, cltv_script(), domain::chain::token_data_opt{});
    transaction unsigned_tx(1, 0, std::move(inputs), std::move(outputs));
    unsigned_tx.inputs()[0].previous_output().validation.cache = coinbase_one.outputs()[0];

    auto const sighash_type = uint8_t(infrastructure::machine::sighash_algorithm::forkid_all);
    auto endorsement = script::create_endorsement(miner_secret(), miner_script(), unsigned_tx, 0,
        sighash_type, flags, coinbase_one.outputs()[0].value());
    REQUIRE(endorsement);

    input::list signed_inputs;
    signed_inputs.emplace_back(output_point{prev_hash, 0},
        script(script::to_pay_public_key_hash_pattern_unlocking(*endorsement, miner_public())),
        0xffffffff);
    return transaction(1, 0, std::move(signed_inputs), output::list(unsigned_tx.outputs()));
}

// The CLTV output back to the miner: locktime 0, final sequence, one signature.
transaction spend_cltv(transaction const& funding, domain::script_flags_t flags) {
    auto const prev_hash = funding.hash();
    input::list inputs;
    inputs.emplace_back(output_point{prev_hash, 0}, script{}, 0xffffffff);
    output::list outputs;
    outputs.emplace_back(funded_value - 1000, miner_script(), domain::chain::token_data_opt{});
    transaction unsigned_tx(1, 0, std::move(inputs), std::move(outputs));
    unsigned_tx.inputs()[0].previous_output().validation.cache = funding.outputs()[0];

    auto const sighash_type = uint8_t(infrastructure::machine::sighash_algorithm::forkid_all);
    auto endorsement = script::create_endorsement(miner_secret(), cltv_script(), unsigned_tx, 0,
        sighash_type, flags, funded_value);
    REQUIRE(endorsement);

    operation::list ops;
    ops.emplace_back(*endorsement, true);
    input::list signed_inputs;
    signed_inputs.emplace_back(output_point{prev_hash, 0}, script(std::move(ops)), 0xffffffff);
    return transaction(1, 0, std::move(signed_inputs), output::list(unsigned_tx.outputs()));
}

// Heights 1..len on regtest genesis: the funding at 101, the spend at
// `spend_at`, coinbase only everywhere else. Everything a batch needs is
// inside the batch.
std::vector<domain::message::block::const_ptr> chain_with_cltv_spend(
    uint32_t len, uint32_t spend_at, domain::script_flags_t flags) {

    REQUIRE(spend_at > funding_height);
    REQUIRE(spend_at <= len);

    auto const base = uint32_t(zulu_time()) - (len + 30) * 600;
    std::vector<domain::message::block::const_ptr> blocks;
    blocks.reserve(len);

    auto prev = block::genesis_regtest().hash();
    transaction funding;
    for (uint32_t h = 1; h <= len; ++h) {
        std::vector<transaction> txs;
        uint64_t fees = 0;
        if (h == funding_height) {
            auto const& coinbase_one = blocks.front()->transactions().front();
            funding = fund_cltv(coinbase_one, flags);
            fees = coinbase_one.outputs()[0].value() - funded_value;
            txs.push_back(funding);
        } else if (h == spend_at) {
            txs.push_back(spend_cltv(funding, flags));
            fees = 1000;
        }
        auto mined = mine_versioned_block(minimum_version(h), prev, h, base + h * 600, std::move(txs), fees);
        prev = mined.hash();
        blocks.push_back(std::make_shared<domain::message::block const>(std::move(mined)));
    }
    return blocks;
}

// Headers into the index first, as they are on a node, then the real batch.
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
// A mainnet activation height inside one batch: P2SH at 173805
// -----------------------------------------------------------------------------
//
// Regtest cannot stage this: every BCH rule is on at every regtest height and
// the frozen BIP rules are also switched on by the block's own version. So the
// batch is validated with MAINNET settings and network at mainnet heights, over
// an index that holds a header for every height up to the batch (the chain
// state of a mainnet height reads its retarget and history ancestors by
// height). Those headers carry regtest proof of work; the batch validator judges
// bodies, not work, and the chain state it builds takes only heights, flags,
// timestamps and versions from them.
//
// The output is locked to the hash of a redeem script that evaluates to false.
// Before BIP16 the spend only has to match the hash and is valid; from 173805
// the redeem script runs and the spend is invalid. A batch that begins below
// 173805 and holds the spend at 173805 must therefore fail.

namespace {

constexpr uint32_t p2sh_height = uint32_t(mainnet_bip16_activation_height);   // 173805
constexpr uint32_t mainnet_batch_len = 104;
constexpr uint32_t mainnet_batch_start = p2sh_height - mainnet_batch_len + 1;   // 173702

// Mined at regtest difficulty with the mainnet subsidy for its height.
block mine_mainnet_height_block(hash_digest const& prev, uint32_t height, uint32_t timestamp,
                                std::vector<transaction> txs, uint64_t fees) {
    transaction::list all;
    all.reserve(txs.size() + 1);
    all.push_back(coinbase_tx(height, 0, block::subsidy(height, /*retarget*/ true) + fees));
    for (auto& tx : txs) {
        all.push_back(std::move(tx));
    }
    block candidate(domain::chain::header{0x20000000, prev, null_hash, timestamp, regtest_bits, 0},
                    std::move(all));
    auto const merkle = candidate.generate_merkle_root();
    for (uint64_t nonce = 0; nonce <= std::numeric_limits<uint32_t>::max(); ++nonce) {
        domain::chain::header header{0x20000000, prev, merkle, timestamp, regtest_bits, uint32_t(nonce)};
        if (header.is_valid_proof_of_work(domain::chain::hash(header), /*retarget*/ false)) {
            return block(std::move(header), transaction::list(candidate.transactions()));
        }
    }
    throw std::runtime_error("no nonce satisfied the regtest target");
}

script const& false_redeem_script() {
    static script const redeem = [] {
        operation::list ops;
        ops.emplace_back(opcode::push_size_0);   // OP_FALSE
        return script(std::move(ops));
    }();
    return redeem;
}

script p2sh_locking_script() {
    auto const redeem_hash = bitcoin_short_hash(false_redeem_script().bytes());
    return script(script::to_pay_script_hash_pattern(redeem_hash));
}

// Coinbase of the batch's first block into one P2SH output, legacy signature
// (no forkid: the chain is below the 2017 fork).
transaction fund_p2sh(transaction const& coinbase_first, domain::script_flags_t flags) {
    auto const prev_hash = coinbase_first.hash();
    input::list inputs;
    inputs.emplace_back(output_point{prev_hash, 0}, script{}, 0xffffffff);
    output::list outputs;
    outputs.emplace_back(funded_value, p2sh_locking_script(), domain::chain::token_data_opt{});
    transaction unsigned_tx(1, 0, std::move(inputs), std::move(outputs));
    unsigned_tx.inputs()[0].previous_output().validation.cache = coinbase_first.outputs()[0];

    auto const sighash_type = uint8_t(infrastructure::machine::sighash_algorithm::all);
    auto endorsement = script::create_endorsement(miner_secret(), miner_script(), unsigned_tx, 0,
        sighash_type, flags, coinbase_first.outputs()[0].value());
    REQUIRE(endorsement);

    input::list signed_inputs;
    signed_inputs.emplace_back(output_point{prev_hash, 0},
        script(script::to_pay_public_key_hash_pattern_unlocking(*endorsement, miner_public())),
        0xffffffff);
    return transaction(1, 0, std::move(signed_inputs), output::list(unsigned_tx.outputs()));
}

// The P2SH output back to the miner: the unlocking script is the serialized
// redeem script alone, which is all a pre-BIP16 spend needs.
transaction spend_p2sh(transaction const& funding) {
    auto const prev_hash = funding.hash();
    operation::list ops;
    ops.emplace_back(false_redeem_script().bytes(), true);
    input::list inputs;
    inputs.emplace_back(output_point{prev_hash, 0}, script(std::move(ops)), 0xffffffff);
    output::list outputs;
    outputs.emplace_back(funded_value - 1000, miner_script(), domain::chain::token_data_opt{});
    return transaction(1, 0, std::move(inputs), std::move(outputs));
}

// A header for every mainnet height below the batch, then the batch's blocks:
// the funding in the block where the first coinbase matures, the spend at
// `spend_at`.
struct mainnet_height_chain {
    domain::message::header::list prelude;                  // heights 1 .. batch_start-1
    std::vector<domain::message::block::const_ptr> batch;   // heights batch_start .. batch_start+len-1
};

mainnet_height_chain chain_at_mainnet_heights(uint32_t spend_at, domain::script_flags_t flags) {
    constexpr uint32_t len = mainnet_batch_len;
    constexpr uint32_t start = mainnet_batch_start;
    constexpr uint32_t funding_at = start + 100;
    REQUIRE(spend_at > funding_at);
    REQUIRE(spend_at < start + len);

    uint32_t const top = start + len - 1;
    auto const base = uint32_t(zulu_time()) - (top + 30) * 600;

    mainnet_height_chain out;
    out.prelude.reserve(start - 1);
    out.batch.reserve(len);

    auto prev = block::genesis_regtest().hash();
    for (uint32_t h = 1; h < start; ++h) {
        auto mined = mine_mainnet_height_block(prev, h, base + h * 600, {}, 0);
        prev = mined.hash();
        out.prelude.push_back(mined.header());
    }

    transaction funding;
    for (uint32_t h = start; h <= top; ++h) {
        std::vector<transaction> txs;
        uint64_t fees = 0;
        if (h == funding_at) {
            auto const& coinbase_first = out.batch.front()->transactions().front();
            funding = fund_p2sh(coinbase_first, flags);
            fees = coinbase_first.outputs()[0].value() - funded_value;
            txs.push_back(funding);
        } else if (h == spend_at) {
            txs.push_back(spend_p2sh(funding));
            fees = 1000;
        }
        auto mined = mine_mainnet_height_block(prev, h, base + h * 600, std::move(txs), fees);
        prev = mined.hash();
        out.batch.push_back(std::make_shared<domain::message::block const>(std::move(mined)));
    }
    return out;
}

code validate_at_mainnet_heights(chain_fixture& fixture, settings const& s,
                                 mainnet_height_chain const& chain) {
    // The organizer validates these headers under the fixture's regtest rules;
    // the batch below reads them as mainnet heights.
    constexpr size_t step = 2000;
    for (size_t from = 0; from < chain.prelude.size(); from += step) {
        auto const to = std::min(from + step, chain.prelude.size());
        domain::message::header::list part(chain.prelude.begin() + from, chain.prelude.begin() + to);
        REQUIRE(fixture.organizer().add_headers(part).headers_added == part.size());
    }
    domain::message::header::list headers;
    for (auto const& blk : chain.batch) headers.push_back(blk->header());
    REQUIRE(fixture.organizer().add_headers(headers).headers_added == headers.size());

    return validate_block_batch(fixture.chain(), s, domain::config::network::mainnet,
        chain.batch, mainnet_batch_start);
}

} // namespace

TEST_CASE("activation boundary - the P2SH spend at mainnet 173805 is rejected inside a batch that began below it",
          "[batch][activation_boundary][mainnet_height]") {
    chain_fixture fixture("activation_boundary_p2sh_cross");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    settings const s(domain::config::network::mainnet);
    auto const chain = chain_at_mainnet_heights(p2sh_height, s.enabled_flags());
    REQUIRE(chain.batch.size() == mainnet_batch_len);
    // The spend sits in the batch's last block, at 173805.
    REQUIRE(chain.batch.back()->transactions().size() == 2u);

    // THE assertion. Under the defect the whole batch carried the flags of
    // 173702, BIP16 among them absent, and the spend went through.
    CHECK(validate_at_mainnet_heights(fixture, s, chain) != error::success);
}

TEST_CASE("activation boundary - the same P2SH spend one block below 173805 is valid",
          "[batch][activation_boundary][mainnet_height]") {
    chain_fixture fixture("activation_boundary_p2sh_below");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    settings const s(domain::config::network::mainnet);
    auto const chain = chain_at_mainnet_heights(p2sh_height - 1, s.enabled_flags());
    CHECK(validate_at_mainnet_heights(fixture, s, chain) == error::success);
}

TEST_CASE("activation boundary - the chain state turns CLTV on at the regtest freeze height",
          "[batch][activation_boundary]") {
    // The rule the cases below rely on, read from the real chain state built
    // the way header validation builds it.
    chain_fixture fixture("activation_boundary_state");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    settings const s(domain::config::network::regtest);
    auto const blocks = chain_with_cltv_spend(cltv_height + 1, cltv_height, s.enabled_flags());
    domain::message::header::list headers;
    for (auto const& blk : blocks) headers.push_back(blk->header());
    REQUIRE(fixture.organizer().add_headers(headers).headers_added == blocks.size());

    validate_header const header_validator(s, domain::config::network::regtest);
    auto const& index = fixture.chain().headers();
    auto const state_at = [&](uint32_t height) {
        auto const& blk = *blocks[height - 1];
        auto const parent_idx = index.find(blk.header().previous_block_hash());
        REQUIRE(parent_idx != header_index::null_index);
        auto cs = header_validator.chain_state_at(blk.header(), blk.hash(), height, parent_idx, index);
        REQUIRE(cs);
        return (*cs)->enabled_flags();
    };
    CHECK_FALSE(script::is_enabled(state_at(cltv_height - 1), domain::machine::script_flags::bip65_rule));
    CHECK(script::is_enabled(state_at(cltv_height), domain::machine::script_flags::bip65_rule));
}

TEST_CASE("activation boundary - a block above the activation is judged by its own rules inside a batch that began below it",
          "[batch][activation_boundary]") {
    // One batch from height 1 past 1351, the spend at 1351. CLTV is enforced
    // there, so the batch must be rejected. Under the defect every block carried
    // the flags of height 1 and the spend went through as a NOP.
    chain_fixture fixture("activation_boundary_cross");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    settings const s(domain::config::network::regtest);
    auto const blocks = chain_with_cltv_spend(cltv_height + 1, cltv_height, s.enabled_flags());
    auto const result = validate(fixture, s, blocks);
    CHECK(result != error::success);
}

TEST_CASE("activation boundary - the same spend one block below the activation is a NOP and passes",
          "[batch][activation_boundary]") {
    // The control for the case above: the batch is identical but the spend sits
    // at 1350, where CHECKLOCKTIMEVERIFY is still NOP2.
    chain_fixture fixture("activation_boundary_below");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    settings const s(domain::config::network::regtest);
    auto const blocks = chain_with_cltv_spend(cltv_height + 1, cltv_height - 1, s.enabled_flags());
    CHECK(validate(fixture, s, blocks) == error::success);
}
