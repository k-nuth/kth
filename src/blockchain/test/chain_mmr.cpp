// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <limits>
#include <vector>

#include <kth/blockchain/chain_mmr.hpp>
#include <kth/blockchain/detail/block_chain_internal.hpp>
#include <kth/domain/chain/block.hpp>
#include <kth/domain/math/mmr.hpp>

#include "reorg_chain_fixture.hpp"

using namespace kth;
using namespace kth::blockchain;
namespace chain_detail = kth::blockchain::detail;

namespace {

header_index::index_t add_header(header_index& headers, hash_digest const& previous,
    uint32_t nonce) {
    domain::chain::header const header{1, previous, null_hash, 1231006505 + nonce, 0x207fffff, nonce};
    auto const added = headers.add(domain::chain::hash(header), header);
    REQUIRE(added.inserted);
    REQUIRE(added.index != header_index::null_index);
    return added.index;
}

std::vector<header_index::index_t> add_chain(header_index& headers, size_t count) {
    std::vector<header_index::index_t> result;
    auto previous = null_hash;
    for (size_t i = 0; i < count; ++i) {
        auto const idx = add_header(headers, previous, uint32_t(i));
        result.push_back(idx);
        previous = headers.get_hash(idx);
    }
    return result;
}

struct mmr_fault_scope {
    explicit mmr_fault_scope(chain_detail::mmr_update_fault fault) {
        chain_detail::fail_mmr_update(fault);
    }
    ~mmr_fault_scope() {
        chain_detail::fail_mmr_update(chain_detail::mmr_update_fault::none);
    }
};

} // namespace

TEST_CASE("chain MMR historical roots and proofs verify against the connected headers", "[shv][chain_mmr]") {
    header_index headers(100);
    auto const chain = add_chain(headers, 33);
    chain_mmr index(headers);
    REQUIRE(index.set_tip(chain.back()));
    REQUIRE(index.leaf_count() == chain.size());

    domain::mmr expected;
    domain::mmr_accumulator client;
    auto previous = null_hash;
    for (uint64_t commitment = 0; commitment < chain.size(); ++commitment) {
        auto const leaf = headers.get_hash(chain[commitment]);
        expected.append(leaf, previous);
        REQUIRE(client.extend(leaf));
        REQUIRE(index.root(commitment) == expected.root(commitment + 1, leaf));
        REQUIRE(index.root(commitment, leaf) == client.root());
        for (uint64_t height = 0; height <= commitment; ++height) {
            INFO("height " << height << ", commitment " << commitment);
            auto const hash = headers.get_hash(chain[height]);
            auto const root = index.proof(height, commitment, true);
            auto const peak = index.proof(hash, commitment, false);
            REQUIRE(root);
            REQUIRE(peak);
            REQUIRE(root->block_height == height);
            REQUIRE(domain::chain::hash(root->header) == hash);
            REQUIRE(root->target == client.root());
            REQUIRE(client.verify_proof_to_root(height, hash, root->proof));
            REQUIRE(client.verify_proof_to_peak(height, hash, peak->proof));
            REQUIRE(peak->target == [&] {
                auto value = hash;
                auto local = height;
                for (auto const& sibling : peak->proof) {
                    value = (local & 1) != 0 ? domain::mmr_node_hash(sibling, value)
                                             : domain::mmr_node_hash(value, sibling);
                    local >>= 1;
                }
                return value;
            }());
            REQUIRE(index.height(hash) == height);
        }
        previous = leaf;
    }
}

TEST_CASE("chain MMR roots equal Knuth's Merkle root for the same leaves", "[shv][chain_mmr]") {
    header_index headers(50);
    chain_mmr index(headers);
    domain::chain::transaction::list transactions;
    auto previous = null_hash;
    for (uint32_t i = 0; i < 33; ++i) {
        transactions.emplace_back(1, i, domain::chain::transaction::ins{}, domain::chain::transaction::outs{});
        auto const hash = transactions.back().hash();
        // The header index accepts an already computed hash; using transaction
        // hashes here gives both Merkle constructions exactly the same leaves.
        domain::chain::header const header{1, previous, null_hash, 1231006505 + i, 0x207fffff, i};
        auto const added = headers.add(hash, header);
        REQUIRE(added.inserted);
        REQUIRE(index.set_tip(added.index));
        domain::chain::block const block{domain::chain::header{}, transactions};
        REQUIRE(index.root(i) == block.generate_merkle_root());
        previous = hash;
    }
}

TEST_CASE("chain MMR disconnect and reconnect restore earlier roots and proofs", "[shv][chain_mmr][reorg]") {
    header_index headers(100);
    auto const chain = add_chain(headers, 17);
    chain_mmr index(headers);
    REQUIRE(index.set_tip(chain[8]));
    auto const old_root = index.root(8);
    auto const old_proof = index.proof(3, 8, true);
    REQUIRE(old_proof);

    REQUIRE(index.set_tip(chain.back()));
    REQUIRE(index.set_tip(chain.back()));
    REQUIRE(index.leaf_count() == 17);
    for (uint32_t height = 16; height > 8; --height) {
        index.disconnect(height);
    }
    REQUIRE(index.leaf_count() == 9);
    REQUIRE(index.root(8) == old_root);
    auto const restored = index.proof(3, 8, true);
    REQUIRE(restored);
    REQUIRE(restored->header == old_proof->header);
    REQUIRE(restored->proof == old_proof->proof);
    REQUIRE(restored->target == old_proof->target);
    REQUIRE_FALSE(index.root(9));
    REQUIRE_FALSE(index.height(headers.get_hash(chain[9])));

    auto const fork = add_header(headers, headers.get_hash(chain[8]), 100);
    auto const tip = add_header(headers, headers.get_hash(fork), 101);
    // Header mapping changes cannot change an existing connected commitment.
    headers.active_set_tip(tip);
    REQUIRE(index.root(8) == old_root);
    REQUIRE_FALSE(index.proof(headers.get_hash(tip), 10, true));
    REQUIRE(index.set_tip(tip));
    REQUIRE(index.leaf_count() == 11);
    REQUIRE(index.root(8) == old_root);
    REQUIRE(index.proof(headers.get_hash(tip), 10, true));
    REQUIRE_FALSE(index.proof(headers.get_hash(chain[9]), 10, true));
    REQUIRE_FALSE(index.root(9, headers.get_hash(chain[9])));

    REQUIRE(index.set_tip(chain.back()));
    REQUIRE(index.leaf_count() == 17);
    REQUIRE(index.root(8) == old_root);
    REQUIRE(index.proof(3, 8, true)->proof == old_proof->proof);
}

TEST_CASE("chain MMR refuses invalid heights, unknown hashes and unrelated roots", "[shv][chain_mmr]") {
    header_index headers(20);
    auto const chain = add_chain(headers, 3);
    chain_mmr index(headers);
    REQUIRE(index.leaf_count() == 0);
    REQUIRE_FALSE(index.root(0));
    REQUIRE_FALSE(index.proof(0, 0, true));
    REQUIRE_FALSE(index.set_tip(header_index::null_index));
    REQUIRE_FALSE(index.set_tip(19));
    REQUIRE(index.set_tip(chain.back()));
    auto const root = index.root(2);
    REQUIRE_FALSE(index.proof(2, 1, false));
    REQUIRE_FALSE(index.proof(0, 3, true));
    REQUIRE_FALSE(index.proof(0, std::numeric_limits<uint64_t>::max(), true));
    REQUIRE_FALSE(index.root(std::numeric_limits<uint64_t>::max()));
    REQUIRE_FALSE(index.height(null_hash));
    REQUIRE_FALSE(index.proof(null_hash, 2, true));
    REQUIRE_FALSE(index.root(2, null_hash));
    auto const unrelated = add_header(headers, null_hash, 100);
    REQUIRE_FALSE(index.set_tip(unrelated));
    REQUIRE(index.root(2) == root);
    REQUIRE(index.leaf_count() == 3);
}

TEST_CASE("chain SHV is opt-in and unavailable during a connected-chain transition", "[shv][chain_mmr]") {
    test::chain_fixture fixture("shv_enabled");
    REQUIRE(fixture.created());
    SECTION("disabled by default") {
        REQUIRE_FALSE(fixture.settings().mmr_index);
        REQUIRE(fixture.start());
        auto const& chain = fixture.chain();
        REQUIRE_FALSE(chain.shv_enabled());
        REQUIRE_FALSE(chain.shv_root(0));
        REQUIRE_FALSE(chain.shv_height(domain::chain::block::genesis_regtest().hash()));
        REQUIRE_FALSE(chain.shv_proof(0, 0, true));
        REQUIRE_FALSE(chain.shv_proof(null_hash, 0, true));
    }
    SECTION("enabled and rebuilt at restart") {
        fixture.settings().mmr_index = true;
        REQUIRE(fixture.start());
        auto& chain = fixture.chain();
        auto const genesis = domain::chain::block::genesis_regtest().hash();
        REQUIRE(chain.shv_enabled());
        REQUIRE(chain.shv_root(0) == genesis);
        REQUIRE(chain.shv_height(genesis) == 0);
        REQUIRE(chain.shv_proof(0, 0, true)->proof.empty());
        REQUIRE(chain.shv_proof(genesis, 0, false)->target == genesis);
        REQUIRE(chain.begin_transition());
        REQUIRE_FALSE(chain.shv_root(0));
        REQUIRE_FALSE(chain.shv_height(genesis));
        REQUIRE_FALSE(chain.shv_proof(0, 0, true));
        REQUIRE_FALSE(chain.shv_proof(genesis, 0, true));
        chain.end_transition();
        REQUIRE(chain.shv_root(0) == genesis);
        REQUIRE(fixture.restart());
        REQUIRE(fixture.chain().shv_root(0) == genesis);
        REQUIRE(fixture.chain().stop());
        REQUIRE_FALSE(fixture.chain().shv_root(0));
        REQUIRE_FALSE(fixture.chain().shv_height(genesis));
        REQUIRE_FALSE(fixture.chain().shv_proof(0, 0, true));
        REQUIRE_FALSE(fixture.chain().shv_proof(genesis, 0, true));
    }
}

TEST_CASE("a rejected MMR update turns SHV off and still publishes the chain view", "[shv][chain_mmr]") {
    test::chain_fixture fixture("shv_failed_publish");
    fixture.settings().mmr_index = true;
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    auto const previous = chain.chain_view();
    REQUIRE(previous);
    auto const unrelated = add_header(chain.headers(), null_hash, 100);
    chain.headers().active_set_tip(unrelated);
    REQUIRE(chain.begin_transition());
    REQUIRE(chain.publish_chain_view(0) == error::success);
    REQUIRE(chain.chain_view() != previous);
    chain.end_transition();
    REQUIRE_FALSE(chain.shv_root(0));
    REQUIRE_FALSE(chain.shv_proof(0, 0, true));
}

TEST_CASE("MMR update failures turn SHV off without stopping the chain", "[shv][chain_mmr]") {
    for (auto fault : {chain_detail::mmr_update_fault::reject_tip, chain_detail::mmr_update_fault::allocation}) {
        {
            test::chain_fixture fixture("shv_start_failure");
            fixture.settings().mmr_index = true;
            REQUIRE(fixture.created());
            mmr_fault_scope const failing(fault);
            REQUIRE(fixture.start());
            REQUIRE_FALSE(fixture.chain().shv_root(0));
            REQUIRE_FALSE(fixture.chain().shv_proof(0, 0, true));
        }
        {
            test::chain_fixture fixture("shv_update_failure");
            fixture.settings().mmr_index = true;
            REQUIRE(fixture.created());
            REQUIRE(fixture.start());
            auto& chain = fixture.chain();
            auto const previous = chain.chain_view();
            REQUIRE(chain.begin_transition());
            mmr_fault_scope const failing(fault);
            REQUIRE(chain.publish_chain_view(0) == error::success);
            REQUIRE(chain.chain_view() != previous);
            chain.end_transition();
            REQUIRE_FALSE(chain.shv_root(0));
            REQUIRE_FALSE(chain.shv_proof(0, 0, true));
        }
    }
}
