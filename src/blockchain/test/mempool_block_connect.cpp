// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <vector>

#include <kth/blockchain/pools/mempool.hpp>
#include <kth/domain.hpp>

#include "reorg_chain_fixture.hpp"

using namespace kth;
using namespace kth::domain::chain;
using kth::blockchain::mempool_entry;

// =============================================================================
// Telling the mempool a block was connected
// =============================================================================
//
// The mempool's writes are serialized by the chain's validation mutex — the one
// the transaction organizer holds while admitting. So the block-connect path
// does not call mempool::remove_for_block itself; it asks the chain, which takes
// that lock at high priority. These pin what the chain's operation does.
//
// The raw form exists because the connect path holds bytes, not parsed blocks,
// and parsing every block during initial sync purely for a mempool that is empty
// would be the only reason to parse it at all. It decides whether to parse under
// the lock, so an admission cannot land between "nothing to remove" and the
// removal.

namespace {

hash_digest make_hash(uint64_t seed) {
    hash_digest h{};
    for (int i = 0; i < 8; ++i) {
        h[i] = static_cast<uint8_t>(seed >> (8 * i));
    }
    h[16] = static_cast<uint8_t>(seed >> 5);
    h[31] = static_cast<uint8_t>(seed >> 11);
    return h;
}

output_point op(uint64_t seed, uint32_t index) {
    return output_point{make_hash(seed), index};
}

transaction make_tx(std::vector<output_point> const& prevouts, uint32_t num_outputs, uint64_t tag) {
    input::list ins;
    ins.reserve(prevouts.size());
    for (auto const& po : prevouts) {
        ins.emplace_back(po, script{}, 0xffffffffu);
    }

    output::list outs;
    outs.reserve(num_outputs);
    for (uint32_t i = 0; i < num_outputs; ++i) {
        output o;
        o.set_value(1000u + tag * 100u + i);
        o.set_script(script{});
        outs.push_back(std::move(o));
    }

    return transaction{1u, 0u, std::move(ins), std::move(outs)};
}

transaction_const_ptr as_ptr(transaction const& tx) {
    return std::make_shared<domain::message::transaction>(tx);
}

mempool_entry entry_for(transaction_const_ptr const& tx, uint64_t tag) {
    return mempool_entry{
        .tx = tx,
        .fee = 1000u + tag,
        .size = static_cast<uint32_t>(tx->serialized_size(true)),
        .sigchecks = 1u,
        .time_seen = tag};
}

// A block carrying `txs` after a coinbase. Only its transactions matter here —
// the mempool never looks at the header.
block block_with(std::vector<transaction> txs) {
    transaction::list all;
    all.reserve(txs.size() + 1);
    all.push_back(make_tx({output_point{null_hash, point::null_index}}, 1, 999));
    for (auto& tx : txs) {
        all.push_back(std::move(tx));
    }
    return block{header{}, std::move(all)};
}

data_chunk serialize(block const& blk) {
    data_chunk raw(blk.serialized_size());
    byte_writer writer(raw);
    auto const written = blk.to_data(writer);
    REQUIRE(written);
    return raw;
}

} // namespace

TEST_CASE("a connected block drops what it confirmed, and conflicts with it", "[mempool][connect]") {
    test::chain_fixture fixture("mp_connect");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    auto& pool = chain.mempool_ref();

    auto const confirmed = as_ptr(make_tx({op(1, 0)}, 1, 1));   // in the block
    auto const conflict = as_ptr(make_tx({op(2, 0)}, 1, 2));    // spends what the block spends
    auto const child = as_ptr(make_tx({output_point{conflict->hash(), 0}}, 1, 3));
    auto const bystander = as_ptr(make_tx({op(9, 0)}, 1, 4));   // unrelated

    REQUIRE(pool.add(entry_for(confirmed, 1)));
    REQUIRE(pool.add(entry_for(conflict, 2)));
    REQUIRE(pool.add(entry_for(child, 3)));
    REQUIRE(pool.add(entry_for(bystander, 4)));
    REQUIRE(pool.size() == 4);

    // The block confirms one pooled transaction and spends the outpoint another
    // pooled one was spending.
    auto const blk = block_with({*confirmed, make_tx({op(2, 0)}, 1, 5)});
    chain.mempool_remove_for_block(blk);

    CHECK_FALSE(pool.entry(confirmed->hash()));   // confirmed
    CHECK_FALSE(pool.entry(conflict->hash()));    // double-spent by the block
    CHECK_FALSE(pool.entry(child->hash()));       // its child goes with it
    CHECK(pool.entry(bystander->hash()));         // untouched
}

TEST_CASE("the raw form does the same as the parsed one", "[mempool][connect]") {
    test::chain_fixture fixture("mp_raw");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    auto& pool = chain.mempool_ref();

    auto const confirmed = as_ptr(make_tx({op(1, 0)}, 1, 1));
    auto const bystander = as_ptr(make_tx({op(9, 0)}, 1, 2));
    REQUIRE(pool.add(entry_for(confirmed, 1)));
    REQUIRE(pool.add(entry_for(bystander, 2)));

    auto const raw = serialize(block_with({*confirmed}));
    CHECK_FALSE(chain.mempool_remove_for_block(byte_span{raw.data(), raw.size()}));

    CHECK_FALSE(pool.entry(confirmed->hash()));
    CHECK(pool.entry(bystander->hash()));
}

TEST_CASE("with an empty pool the raw form never parses the block", "[mempool][connect]") {
    // Which is the point of it: during initial sync there is nothing to remove,
    // and parsing every block to discover that would be the only reason to parse
    // it. Bytes that could not possibly parse still succeed, because they are
    // never looked at.
    test::chain_fixture fixture("mp_empty");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();

    REQUIRE(chain.mempool_ref().size() == 0);

    data_chunk const garbage{0x00, 0x01, 0x02};
    CHECK_FALSE(chain.mempool_remove_for_block(byte_span{garbage.data(), garbage.size()}));
}

TEST_CASE("with a pool to update the raw form reports bytes it cannot parse", "[mempool][connect]") {
    // The same bytes, and now they matter: the caller has to hear that the pool
    // was not updated rather than take silence for a block that confirmed
    // nothing.
    test::chain_fixture fixture("mp_bad");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    auto& pool = chain.mempool_ref();

    auto const pooled = as_ptr(make_tx({op(1, 0)}, 1, 1));
    REQUIRE(pool.add(entry_for(pooled, 1)));

    data_chunk const garbage{0x00, 0x01, 0x02};
    CHECK(chain.mempool_remove_for_block(byte_span{garbage.data(), garbage.size()}));

    // And it left the pool alone.
    CHECK(pool.size() == 1);
    CHECK(pool.entry(pooled->hash()));
}
