// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <memory>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/blockchain/validate/batch_validate.hpp>
#include <kth/blockchain/validate/validate_header.hpp>
#include <kth/infrastructure/utility/timer.hpp>

#include "reorg_chain_fixture.hpp"
#include "regtest_miner.hpp"

using namespace kth;
using namespace kth::test;

// =============================================================================
// Block proposals (BIP23)
// =============================================================================
//
// A proposal is judged as the next block on the connected tip, with every
// check the connect path makes except proof of work, and nothing is stored.
// A fresh chain's connected tip is its genesis, so these propose blocks at
// height 1. The checks that need a spendable output are in the node's
// getblocktemplate tests, on a chain with connected blocks.

namespace {

using blockchain::block_proposal_result;
using blockchain::block_validity;
using domain::chain::block;
using domain::chain::header;
using domain::chain::transaction;
using outcome = block_proposal_result::outcome;

// A block on `prev` with the merkle root of its transactions and nonce 0: no
// proof of work is asked of a proposal.
block proposal(hash_digest const& prev, uint32_t timestamp, transaction::list txs,
               uint32_t bits = regtest_bits, uint32_t version = 0x20000000) {
    block const candidate(header{version, prev, null_hash, timestamp, bits, 0}, std::move(txs));
    auto const merkle = candidate.generate_merkle_root();
    return block(header{version, prev, merkle, timestamp, bits, 0},
        transaction::list(candidate.transactions()));
}

transaction::list coinbase_only(uint32_t height, uint64_t value) {
    return {coinbase_tx(height, 0, value)};
}

block_proposal_result judge(blockchain::block_chain& chain, block const& candidate) {
    ::asio::io_context io;
    auto result = ::asio::co_spawn(io,
        chain.test_block_proposal(std::make_shared<domain::message::block const>(candidate)),
        ::asio::use_future);
    io.run();
    return result.get();
}

uint32_t now() {
    return uint32_t(zulu_time());
}

hash_digest const& genesis_hash() {
    static hash_digest const hash = block::genesis_regtest().hash();
    return hash;
}

uint64_t subsidy(uint32_t height) {
    return block::subsidy(height, /*retarget*/ false);
}

hash_digest make_hash(uint32_t id) {
    hash_digest hash{};
    hash[0] = uint8_t(id & 0xFF);
    hash[1] = uint8_t((id >> 8) & 0xFF);
    hash[2] = uint8_t((id >> 16) & 0xFF);
    hash[3] = uint8_t((id >> 24) & 0xFF);
    return hash;
}

} // namespace

TEST_CASE("a block proposal on the connected tip is judged and nothing of it is stored",
          "[block_proposal]") {
    chain_fixture fixture("proposal_valid");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    auto const view = chain.chain_view();
    REQUIRE(view);
    REQUIRE(view->tip_hash == genesis_hash());
    auto const built = chain.get_utxo_built_height();

    auto const candidate = proposal(genesis_hash(), now(), coinbase_only(1, subsidy(1)));
    auto const first = judge(chain, candidate);
    CHECK(first.result == outcome::valid);
    CHECK(first.error == error::success);
    CHECK(first.validity == block_validity::valid);

    // Not in the header index, not connected, nothing built: proposed again,
    // it is judged again rather than found.
    CHECK(chain.headers().find(candidate.hash()) == blockchain::header_index::null_index);
    CHECK(chain.chain_view() == view);
    CHECK(chain.get_utxo_built_height() == built);
    CHECK(judge(chain, candidate).result == outcome::valid);
}

TEST_CASE("a block proposal is refused for the first rule it breaks", "[block_proposal]") {
    chain_fixture fixture("proposal_refusals");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    auto const time = now();
    auto const genesis_time = block::genesis_regtest().header().timestamp();

    struct refusal {
        char const* what;
        block candidate;
        code expected;
    };

    // The control: each refusal below changes one thing of this block.
    REQUIRE(judge(chain, proposal(genesis_hash(), time, coinbase_only(1, subsidy(1)))).result ==
        outcome::valid);

    auto bad_merkle = proposal(genesis_hash(), time, coinbase_only(1, subsidy(1)));
    bad_merkle = block(header{bad_merkle.header().version(), genesis_hash(), null_hash, time,
        regtest_bits, 0}, transaction::list(bad_merkle.transactions()));

    // A coinbase with no height in its script.
    domain::chain::input::list inputs;
    inputs.emplace_back(domain::chain::output_point{null_hash, domain::chain::point::null_index},
        domain::chain::script(domain::chain::script::to_pay_public_key_hash_pattern(short_hash{})), 0xffffffff);
    domain::chain::output::list outputs;
    outputs.emplace_back(subsidy(1), miner_script(), domain::chain::token_data_opt{});
    transaction const no_height(1, 0, std::move(inputs), std::move(outputs));

    // Not a coinbase: spends an output that is not there.
    domain::chain::input::list spend_inputs;
    spend_inputs.emplace_back(domain::chain::output_point{make_hash(7), 0}, domain::chain::script{}, 0xffffffff);
    domain::chain::output::list spend_outputs;
    spend_outputs.emplace_back(1000, miner_script(), domain::chain::token_data_opt{});
    transaction const not_coinbase(1, 0, std::move(spend_inputs), std::move(spend_outputs));

    std::vector<refusal> const refusals{
        {"bits other than the next work required",
            proposal(genesis_hash(), time, coinbase_only(1, subsidy(1)), regtest_bits - 1),
            error::incorrect_proof_of_work},
        {"a timestamp not after the median time past",
            proposal(genesis_hash(), genesis_time, coinbase_only(1, subsidy(1))),
            error::timestamp_too_early},
        {"a timestamp more than two hours ahead",
            proposal(genesis_hash(), time + 3 * 3600, coinbase_only(1, subsidy(1))),
            error::futuristic_timestamp},
        {"a merkle root that is not its transactions'", bad_merkle, error::merkle_mismatch},
        {"a coinbase claiming one satoshi more than the subsidy",
            proposal(genesis_hash(), time, coinbase_only(1, subsidy(1) + 1)),
            error::coinbase_value_limit},
        {"a coinbase without the block height",
            proposal(genesis_hash(), time, {no_height}),
            error::coinbase_height_mismatch},
        {"no transactions", proposal(genesis_hash(), time, {}), error::empty_block},
        {"a first transaction that is not a coinbase",
            proposal(genesis_hash(), time, {not_coinbase}),
            error::first_not_coinbase},
    };
    for (auto const& [what, candidate, expected] : refusals) {
        CAPTURE(what);
        auto const result = judge(chain, candidate);
        CHECK(result.result == outcome::invalid);
        CHECK(result.error == expected);
        CHECK(result.validity == block_validity::invalid);
        CHECK_FALSE(result.script);
        CHECK(chain.headers().find(candidate.hash()) == blockchain::header_index::null_index);
    }
}

TEST_CASE("a block proposal the node holds is answered from the header index", "[block_proposal]") {
    chain_fixture fixture("proposal_duplicates");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    // The connected tip.
    auto const genesis = judge(chain, block::genesis_regtest());
    CHECK(genesis.result == outcome::duplicate);
    CHECK(genesis.validity == block_validity::valid);

    // A header the node holds without its body has no verdict.
    auto const mined = mine_block(genesis_hash(), now() - 60, 1, {}, 0);
    REQUIRE(fixture.organizer().add_headers(domain::message::header::list{mined.header()}).headers_added == 1);
    auto const header_only = judge(chain, mined);
    CHECK(header_only.result == outcome::duplicate);
    CHECK(header_only.validity == block_validity::unknown);

    // Once refused, it is known invalid.
    auto const idx = chain.headers().find(mined.hash());
    REQUIRE(idx != blockchain::header_index::null_index);
    chain.headers().add_status(idx, blockchain::header_status::failed_valid);
    auto const refused = judge(chain, mined);
    CHECK(refused.result == outcome::duplicate);
    CHECK(refused.validity == block_validity::invalid);

    // A block the node does not hold at the same height is still judged: its
    // parent is the connected tip, whatever the header chain holds above it.
    auto const sibling = proposal(genesis_hash(), now(), {coinbase_tx(1, 9, subsidy(1))});
    CHECK(judge(chain, sibling).result == outcome::valid);
}

TEST_CASE("a block proposal off the connected tip is not judged", "[block_proposal]") {
    chain_fixture fixture("proposal_off_tip");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    // On a header the node holds above the connected tip, and on a block it
    // does not know. Both would be valid on the tip; neither is judged.
    auto const mined = mine_block(genesis_hash(), now() - 60, 1, {}, 0);
    REQUIRE(fixture.organizer().add_headers(domain::message::header::list{mined.header()}).headers_added == 1);
    for (auto const& parent : {mined.hash(), make_hash(1)}) {
        auto const result = judge(chain, proposal(parent, now(), coinbase_only(2, subsidy(2))));
        CHECK(result.result == outcome::not_on_tip);
        CHECK(result.validity == block_validity::unknown);
    }
}

TEST_CASE("a block proposal judged while a chain transition runs gets no verdict",
          "[block_proposal]") {
    chain_fixture fixture("proposal_transition");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    auto const candidate = proposal(genesis_hash(), now(), coinbase_only(1, subsidy(1)));

    // A transition may be changing the UTXO set the proposal reads, so what
    // the proposal reads then describes no state: it is answered as off the
    // tip, the answer a client takes to ask again.
    REQUIRE(chain.begin_transition());
    auto const during = judge(chain, candidate);
    CHECK(during.result == outcome::not_on_tip);
    chain.end_transition();
    CHECK(judge(chain, candidate).result == outcome::valid);
}

#if defined(KTH_CURRENCY_BCH)
// Chipnet allows a minimum-difficulty block once it is stamped more than two
// of its own intervals after its parent: 120 s when blocks are 60 ticks. The
// index below is built by hand, as in the ASERT anchor tests: header
// validation is contextual, and the ancestors' own proof of work is not what
// is tested. Each proposal carries a wrong merkle root, so a header that
// passes is refused for that instead, before anything reads the UTXO set.
TEST_CASE("a chipnet block proposal stamped more than 120 s after its parent may be minimum difficulty",
          "[block_proposal][chipnet]") {
    namespace chain = kth::domain::chain;
    constexpr auto network = domain::config::network::chipnet;
    constexpr uint32_t anchor = chipnet_asert_anchor_block_height;
    constexpr uint32_t ancestor_time = chipnet_asert_anchor_block_ancestor_time;
    // Faster blocks start a few blocks after the ASERT anchor, and the last
    // 400 blocks come one second apart, so ASERT asks for more than the
    // minimum difficulty.
    constexpr uint32_t slow_until = anchor + 30;
    constexpr uint32_t tip = slow_until + 400;

    chain_fixture fixture("proposal_chipnet", network);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& index = fixture.chain().headers();

    // 600 s apart up to slow_until, with the anchor's parent at its recorded
    // time, then one second apart.
    auto const time_at = [&](uint32_t height) -> uint32_t {
        if (height <= slow_until) {
            return uint32_t(int64_t(ancestor_time) - (int64_t(anchor) - 1 - int64_t(height)) * 600);
        }
        return uint32_t(ancestor_time + (slow_until - (anchor - 1)) * 600 + (height - slow_until));
    };
    auto previous = chain::block::genesis_chipnet().hash();
    auto parent = index.find(previous);
    REQUIRE(parent != blockchain::header_index::null_index);
    for (uint32_t height = 1; height <= tip; ++height) {
        chain::header const ancestor{0x20000000, previous, make_hash(height),
            time_at(height), retarget_proof_of_work_limit, height};
        previous = chain::hash(ancestor);
        auto const added = index.add(previous, ancestor);
        REQUIRE(added.inserted);
        parent = added.index;
    }
    REQUIRE(index.get_height(parent) == int32_t(tip));

    blockchain::settings s(network);
    s.checkpoints.clear();
    s.derive_checkpoints();
    s.full_validation = true;
    s.cantor_activation_time = time_at(anchor + 10);
    blockchain::validate_header const validator(s, network);

    // A block that would be refused for its merkle root once its header passes.
    auto const candidate = [&](uint32_t timestamp, uint32_t bits) {
        auto const coinbase = coinbase_tx(tip + 1, 0, 1);
        return std::make_shared<domain::message::block const>(chain::block(
            chain::header{0x20000000, previous, null_hash, timestamp, bits, 0}, {coinbase}));
    };
    auto const judge_header = [&](uint32_t timestamp, uint32_t bits) {
        blockchain::batch_rejection rejection;
        auto const ec = blockchain::validate_block_proposal(fixture.chain(), s, network,
            candidate(timestamp, bits), tip + 1, parent, rejection);
        CHECK(rejection.block == std::optional<size_t>{0});
        return ec;
    };

    auto const on_time = time_at(tip) + 60;
    auto const state = validator.chain_state_at(candidate(on_time, 0)->header(),
        null_hash, tip + 1, parent, index);
    REQUIRE(state);
    REQUIRE((*state)->is_faster_blocks_active());
    auto const asert = (*state)->work_required();
    REQUIRE(asert != 0);
    REQUIRE(asert != retarget_proof_of_work_limit);

    // At 120 s the ASERT bits are the next work required, and minimum
    // difficulty is not.
    auto const at_limit = time_at(tip) + 120;
    CHECK(judge_header(on_time, asert) == error::merkle_mismatch);
    CHECK(judge_header(at_limit, asert) == error::merkle_mismatch);
    CHECK(judge_header(at_limit, retarget_proof_of_work_limit) == error::incorrect_proof_of_work);

    // One second later minimum difficulty is the next work required, and the
    // ASERT bits are not.
    auto const late = time_at(tip) + 121;
    CHECK(judge_header(late, retarget_proof_of_work_limit) == error::merkle_mismatch);
    CHECK(judge_header(late, asert) == error::incorrect_proof_of_work);
}
#endif
