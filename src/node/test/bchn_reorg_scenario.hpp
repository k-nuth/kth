// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The chain scenario shared by the ports of Bitcoin Cash Node feature_block.py forks (see
// bchn_feature_block_reorg.cpp): a mature regtest trunk, blocks that each spend one trunk coinbase, and the calls that
// make the node adopt or leave a branch, all through the real node stack of reorg_cycle.cpp.

#ifndef KTH_NODE_TEST_BCHN_REORG_SCENARIO_HPP
#define KTH_NODE_TEST_BCHN_REORG_SCENARIO_HPP

#include <test_helpers.hpp>

#include <chrono>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>

#include <kth/node/sync/block_tasks.hpp>
#include <kth/node/sync/messages.hpp>
#include <kth/node/sync/reorg.hpp>

#include "sync_harness.hpp"

#include "sync_harness.hpp"

namespace kth::test::bchn_reorg {

using namespace kth;
using namespace kth::node::sync;

constexpr uint32_t trunk_len = 105;
constexpr uint64_t fee = 1000;


// P2PKH spend of output `index` of `prev_tx`, signed with the miner key, to arbitrary `outputs`.
inline domain::chain::transaction signed_spend_to(
    domain::chain::transaction const& prev_tx, uint32_t index, domain::chain::output::list outputs,
    domain::script_flags_t flags) {
    auto const prev_value = prev_tx.outputs()[index].value();
    auto const prev_hash = prev_tx.hash();
    domain::chain::input::list inputs;
    inputs.emplace_back(domain::chain::output_point{prev_hash, index}, domain::chain::script{}, 0xffffffff);
    domain::chain::transaction unsigned_tx(1, 0, std::move(inputs), domain::chain::output::list(outputs));
    unsigned_tx.inputs()[0].previous_output().validation.cache = prev_tx.outputs()[index];
    auto const sighash_type = uint8_t(infrastructure::machine::sighash_algorithm::forkid_all);
    auto endorsement = domain::chain::script::create_endorsement(
        miner_secret(), miner_script(), unsigned_tx, 0, sighash_type, flags, prev_value);
    REQUIRE(endorsement);
    domain::chain::input::list signed_inputs;
    signed_inputs.emplace_back(domain::chain::output_point{prev_hash, index},
        domain::chain::script(domain::chain::script::to_pay_public_key_hash_pattern_unlocking(*endorsement, miner_public())),
        0xffffffff);
    return domain::chain::transaction(1, 0, std::move(signed_inputs), std::move(outputs));
}

struct scenario {
    explicit scenario(char const* tag, bool bip34 = true) : fixture(tag) {
        REQUIRE(fixture.created());
        fixture.settings().bip34 = bip34;
        REQUIRE(fixture.start());
        base_time = uint32_t(zulu_time()) - (trunk_len + 30) * block_spacing;

        auto prev = domain::chain::block::genesis_regtest().hash();
        for (uint32_t h = 1; h <= trunk_len; ++h) {
            trunk.push_back(mine_block(prev, h, base_time + h * block_spacing, 0, {}, 0));
            prev = trunk.back().hash();
        }
        REQUIRE(fixture.organizer().add_headers(headers_of(trunk)).headers_added == trunk_len);
        persist_headers(fixture, trunk, 1);
        connect_bodies(fixture, trunk, 1);
    }

    // A block of height `height` on `parent` that spends the coinbase of trunk block `out` (1-based).
    domain::chain::block next(hash_digest const& parent, uint32_t height, uint32_t salt, uint32_t out) {
        auto const& cb = trunk[out - 1].transactions().front();
        // the fee differs per block, so two blocks spending the same coinbase carry different transactions
        auto const block_fee = fee + salt;
        auto const spend = spend_p2pkh(cb, 0, cb.outputs()[0].value() - block_fee, fixture.chain().chain_settings().enabled_flags());
        return mine_block(parent, height, base_time + height * block_spacing + salt, salt, {spend}, block_fee);
    }

    bool stored(domain::chain::block const& b) {
        return fixture.chain().headers().find(b.hash()) != blockchain::header_index::null_index;
    }

    // Give the organizer the headers of `blocks`; returns its answer.
    auto headers(std::vector<domain::chain::block> const& blocks) {
        return fixture.organizer().add_headers(headers_of(blocks));
    }

    // Switch to the branch the organizer asked for.
    blockchain::block_chain::switch_result switch_to(blockchain::header_organize_result const& r) {
        reorg_outcome reorg;
        ::asio::io_context ctx;
        ::asio::co_spawn(ctx,
            execute_reorg(fixture.chain(), fixture.organizer(), r.reorg_branch_head, uint32_t(r.reorg_fork_height),
                [] { return false; }, real_persister(fixture.chain())),
            [&reorg](std::exception_ptr, reorg_outcome result) { reorg = result; });
        ctx.run_for(std::chrono::seconds(30));
        REQUIRE_FALSE(reorg.fatal);
        return reorg.result;
    }

    hash_digest active_hash(uint32_t height) {
        auto const& index = fixture.chain().headers();
        auto const idx = index.active_at(int32_t(height));
        REQUIRE(idx != blockchain::header_index::null_index);
        return index.get_hash(idx);
    }

    uint32_t connected_height() {
        auto const heights = fixture.chain().get_last_heights();
        REQUIRE(heights);
        return uint32_t(heights->block);
    }

    // Whether output `index` of `txid` is in the UTXO set; an output the store never held is absent, any other
    // failure of the store is not an answer.
    bool unspent(hash_digest const& txid, uint32_t index = 0) {
        auto& chain = fixture.chain();
        auto const at = connected_height();
        auto const direct = chain.get_utxo(domain::chain::output_point{txid, index}, at);
        if (direct) return true;
        if (direct.error() == database::result_code::key_not_found) return false;
        return utxo_present(chain, txid, index, at);
    }

    chain_fixture fixture;
    uint32_t base_time{0};
    std::vector<domain::chain::block> trunk;
};


} // namespace kth::test::bchn_reorg

#endif // KTH_NODE_TEST_BCHN_REORG_SCENARIO_HPP
