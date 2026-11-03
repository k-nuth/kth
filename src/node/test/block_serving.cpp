// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <chrono>
#include <future>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/node/block_serving.hpp>
#include "../../blockchain/test/reorg_chain_fixture.hpp"
#include "sync_harness.hpp"

using namespace kth;
using domain::message::get_headers;
using domain::message::inventory_vector;

namespace {

// Five blocks on the active chain with their headers stored, the first three
// connected, and a two-block side branch from block 1 that never became active.
struct served_chain {
    test::chain_fixture fixture{"block_serving"};
    std::vector<domain::chain::block> trunk;
    std::vector<domain::chain::block> side;

    served_chain() {
        REQUIRE(fixture.created());
        REQUIRE(fixture.start());
        auto const base_time = uint32_t(zulu_time()) - 40u * test::block_spacing;
        auto prev = domain::chain::block::genesis_regtest().hash();
        for (uint32_t h = 1; h <= 5; ++h) {
            trunk.push_back(test::mine_block(prev, h, base_time + h * test::block_spacing, 0, {}, 0));
            prev = trunk.back().hash();
        }
        REQUIRE(fixture.organizer().add_headers(test::headers_of(trunk)).headers_added == trunk.size());
        test::persist_headers(fixture, trunk, 1);
        test::connect_bodies(fixture, {trunk.begin(), trunk.begin() + 3}, 1);

        // Into the index directly: only its place off the active chain matters here.
        prev = trunk[0].hash();
        for (uint32_t h = 2; h <= 3; ++h) {
            side.push_back(test::mine_block(prev, h, base_time + h * test::block_spacing + 1, 1, {}, 0));
            prev = side.back().hash();
            REQUIRE(chain().headers().add(side.back().hash(), side.back().header()).inserted);
        }

        auto const heights = chain().get_last_heights();
        REQUIRE(heights);
        REQUIRE(heights->block == 3u);
        REQUIRE(chain().headers().active_tip_height() == 5);
    }

    blockchain::block_chain& chain() { return fixture.chain(); }

    hash_list answer(hash_list start, hash_digest const& stop = null_hash, size_t limit = max_get_headers) {
        auto const headers = node::answer_header_request(get_headers{std::move(start), stop}, chain(), limit);
        hash_list hashes;
        headers.to_hashes(hashes);
        return hashes;
    }

    domain::message::block::const_ptr serve(hash_digest const& hash) {
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, node::servable_block(chain(), hash), ::asio::use_future);
        io.run_for(std::chrono::seconds(5));
        REQUIRE(result.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
        return result.get();
    }

    std::optional<domain::message::compact_block> compact(hash_digest const& hash) {
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, node::servable_compact_block(chain(), hash), ::asio::use_future);
        io.run_for(std::chrono::seconds(5));
        REQUIRE(result.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
        return result.get();
    }

    void store_only(domain::chain::block const& block, uint32_t height) {
        auto const light = test::to_light(block);
        ::asio::io_context io;
        auto result = ::asio::co_spawn(io, chain().store_block(light, height), ::asio::use_future);
        io.run_for(std::chrono::seconds(5));
        REQUIRE(result.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
        REQUIRE(result.get() == error::success);
    }

    hash_digest hash(size_t height) const { return trunk[height - 1].hash(); }
};

} // namespace

TEST_CASE("getheaders is answered from the locator's fork on the active chain", "[block_serving]") {
    served_chain served;
    auto const genesis = domain::chain::block::genesis_regtest().hash();

    // Headers ahead of the connected blocks are not offered.
    CHECK(served.answer({genesis}) == hash_list{served.hash(1), served.hash(2), served.hash(3)});
    CHECK(served.answer({served.hash(2), served.hash(1), genesis}) == hash_list{served.hash(3)});
    CHECK(served.answer({served.hash(3)}).empty());
    CHECK(served.answer({served.hash(5)}).empty());

    // A locator hash the node does not know, or one off the active chain, is
    // skipped; with none left the answer starts after genesis.
    CHECK(served.answer({null_hash, served.hash(1)}) == hash_list{served.hash(2), served.hash(3)});
    CHECK(served.answer({served.side[1].hash(), served.side[0].hash(), served.hash(1)})
        == hash_list{served.hash(2), served.hash(3)});
    CHECK(served.answer({served.side[1].hash()}) == hash_list{served.hash(1), served.hash(2), served.hash(3)});

    // The stop hash ends the answer when it comes after the fork on the active
    // chain, and is ignored otherwise.
    CHECK(served.answer({genesis}, served.hash(2)) == hash_list{served.hash(1), served.hash(2)});
    CHECK(served.answer({served.hash(2)}, served.hash(1)) == hash_list{served.hash(3)});
    CHECK(served.answer({genesis}, served.side[0].hash()) == hash_list{served.hash(1), served.hash(2), served.hash(3)});

    // At most `limit` headers.
    CHECK(served.answer({genesis}, null_hash, 2) == hash_list{served.hash(1), served.hash(2)});
}

TEST_CASE("getheaders with no locator asks for the stop hash's header alone", "[block_serving]") {
    served_chain served;
    CHECK(served.answer({}, served.hash(2)) == hash_list{served.hash(2)});
    CHECK(served.answer({}, served.hash(4)).empty());
    CHECK(served.answer({}, served.side[0].hash()).empty());
    CHECK(served.answer({}, null_hash).empty());
}

TEST_CASE("getdata entries for blocks and compact blocks are block requests", "[block_serving]") {
    using type_id = inventory_vector::type_id;
    auto const hash = domain::chain::block::genesis_regtest().hash();
    CHECK(node::is_block_request({type_id::block, hash}));
    CHECK(node::is_block_request({type_id::compact_block, hash}));
    CHECK_FALSE(node::is_block_request({type_id::transaction, hash}));
    CHECK_FALSE(node::is_block_request({type_id::filtered_block, hash}));
    CHECK_FALSE(node::is_block_request({type_id::double_spend_proof, hash}));
}

TEST_CASE("only stored blocks not known invalid are served", "[block_serving]") {
    served_chain served;
    auto const block = served.serve(served.hash(3));
    REQUIRE(block);
    CHECK(block->hash() == served.hash(3));
    REQUIRE(block->transactions().size() == served.trunk[2].transactions().size());
    CHECK(block->transactions().front().hash() == served.trunk[2].transactions().front().hash());

    // A header without its block, and a hash the node never saw.
    CHECK_FALSE(served.serve(served.hash(4)));
    CHECK_FALSE(served.serve(null_hash));

    // A stored block found invalid, or built on one, is withheld.
    auto& index = served.chain().headers();
    auto const second = index.find(served.hash(2));
    index.add_status(second, blockchain::header_status::failed_valid);
    CHECK_FALSE(served.serve(served.hash(2)));
    auto const first = index.find(served.hash(1));
    index.add_status(first, blockchain::header_status::failed_child);
    CHECK_FALSE(served.serve(served.hash(1)));
    CHECK(served.serve(served.hash(3)));
}

TEST_CASE("compact replies prefill only the coinbase and name the remaining transactions", "[block_serving][compact_blocks]") {
    auto const genesis = domain::chain::block::genesis_regtest();
    auto const extra = test::coinbase_tx(1, 1, 1);
    domain::message::block const block{genesis.header(), {genesis.transactions().front(), extra}};
    auto const compact = node::compact_block_reply(block);
    REQUIRE(compact);
    CHECK(domain::chain::hash(compact->header()) == block.hash());
    REQUIRE(compact->transactions().size() == 1);
    CHECK(compact->transactions().front().index() == 0);
    CHECK(compact->transactions().front().transaction() == block.transactions().front());
    REQUIRE(compact->short_ids().size() == 1);
    CHECK(compact->short_ids().front()
        == domain::message::compact_short_id(domain::message::hash(*compact), extra.hash()));
    CHECK_FALSE(node::compact_block_reply(domain::message::block{}));
}

TEST_CASE("compact serving is restricted to the connected active chain", "[block_serving][compact_blocks]") {
    served_chain served;
    auto const compact = served.compact(served.hash(3));
    REQUIRE(compact);
    CHECK(domain::chain::hash(compact->header()) == served.hash(3));
    served.store_only(served.trunk[3], 4);
    REQUIRE(served.serve(served.hash(4)));
    CHECK_FALSE(served.compact(served.hash(4)));
    served.store_only(served.side[0], 2);
    REQUIRE(served.serve(served.side[0].hash()));
    CHECK_FALSE(served.compact(served.side[0].hash()));
    CHECK_FALSE(served.compact(null_hash));
    auto& index = served.chain().headers();
    index.add_status(index.find(served.hash(2)), blockchain::header_status::failed_valid);
    CHECK_FALSE(served.compact(served.hash(2)));
    // Header-chain membership alone cannot vouch for the connected branch.
    index.active_set_tip(index.find(served.side[0].hash()));
    CHECK_FALSE(served.compact(served.side[0].hash()));
}

TEST_CASE("blocktxn replies preserve requested positions and refuse indexes outside the stored block", "[block_serving][compact_blocks]") {
    auto const genesis = domain::chain::block::genesis_regtest();
    domain::message::block const block{genesis.header(), {
        genesis.transactions().front(), test::coinbase_tx(1, 1, 1), test::coinbase_tx(2, 2, 2)}};
    auto const request = domain::message::get_block_transactions::create(block.hash(), {0, 2});
    REQUIRE(request);
    auto const reply = node::block_transactions_reply(block, *request);
    REQUIRE(reply);
    CHECK(reply->block_hash() == block.hash());
    REQUIRE(reply->transactions().size() == 2);
    CHECK(reply->transactions()[0] == block.transactions()[0]);
    CHECK(reply->transactions()[1] == block.transactions()[2]);

    auto const empty = domain::message::get_block_transactions::create(block.hash(), {});
    REQUIRE(empty);
    auto const empty_reply = node::block_transactions_reply(block, *empty);
    REQUIRE(empty_reply);
    CHECK(empty_reply->transactions().empty());

    auto const past_end = domain::message::get_block_transactions::create(block.hash(), {0, 3});
    REQUIRE(past_end);
    CHECK_FALSE(node::block_transactions_reply(block, *past_end));
    auto const wrong_hash = domain::message::get_block_transactions::create(null_hash, {0});
    REQUIRE(wrong_hash);
    CHECK_FALSE(node::block_transactions_reply(block, *wrong_hash));
}
