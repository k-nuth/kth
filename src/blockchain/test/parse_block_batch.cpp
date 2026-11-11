// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <stdexcept>
#include <vector>

#include <kth/blockchain/validate/batch_validate.hpp>
#include <kth/blockchain/validate/fork_join.hpp>
#include <kth/domain/chain/block.hpp>

using namespace kth;
using namespace kth::blockchain;
namespace chain = kth::domain::chain;

namespace {

// A block of `count` transactions after a coinbase: enough of them that a
// worker parses something, and different per block so no two blocks agree.
data_chunk raw_block(uint32_t salt, size_t count) {
    chain::transaction::list txs;
    txs.emplace_back(1, 0,
        chain::input::list{chain::input(chain::output_point(null_hash, chain::point::null_index),
            chain::script(data_chunk{0x02, uint8_t(salt), uint8_t(salt >> 8)}, false), 0xffffffff)},
        chain::output::list{chain::output(50, chain::script(data_chunk{0x51}, false), std::nullopt)});
    for (size_t t = 0; t < count; ++t) {
        hash_digest prev{};
        prev[0] = uint8_t(salt);
        prev[1] = uint8_t(t);
        txs.emplace_back(1, 0,
            chain::input::list{chain::input(chain::output_point(prev, uint32_t(t)), chain::script{}, 0)},
            chain::output::list{chain::output(t + 1, chain::script(data_chunk{0x52}, false), std::nullopt)});
    }
    chain::block const block(chain::header{1u, null_hash, null_hash, salt, 0u, 0u}, std::move(txs));
    return kth::to_data_chunk(block);
}

std::vector<data_chunk> raw_batch(size_t blocks) {
    std::vector<data_chunk> out;
    for (size_t i = 0; i < blocks; ++i) {
        out.push_back(raw_block(uint32_t(i + 1), 3 + i % 4));
    }
    return out;
}

} // namespace

TEST_CASE("parse_block_batch gives the blocks and identifiers a parse in order gives",
          "[parse_block_batch]") {
    // More blocks than threads on any machine running this, so every worker
    // takes several and the strides interleave.
    auto const raw = raw_batch(fork_join_workers(1'000) * 3 + 1);
    auto const parsed = parse_block_batch(raw);
    REQUIRE(parsed);
    REQUIRE(parsed->blocks.size() == raw.size());
    REQUIRE(parsed->txids.size() == raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        byte_reader reader(byte_span{raw[i].data(), raw[i].size()});
        auto const expected = domain::message::block::from_data(reader, 0u);
        REQUIRE(expected);
        REQUIRE(parsed->blocks[i]);
        CHECK(*parsed->blocks[i] == *expected);
        CHECK(parsed->txids[i] == expected->to_hashes());
    }
}

TEST_CASE("parse_block_batch names the lowest block that does not parse",
          "[parse_block_batch]") {
    auto raw = raw_batch(fork_join_workers(1'000) * 2 + 3);
    auto const truncate = [&](size_t i) {
        raw[i].resize(raw[i].size() / 2);
    };
    // Two damaged blocks: whichever thread reaches its block first, the
    // answer is the lower one, the block a parse in order stops at.
    truncate(raw.size() - 2);
    truncate(3);
    auto const parsed = parse_block_batch(raw);
    REQUIRE_FALSE(parsed);
    REQUIRE(parsed.error().block.has_value());
    CHECK(*parsed.error().block == 3u);

    // Damage below it moves the answer down.
    truncate(1);
    auto const lower = parse_block_batch(raw);
    REQUIRE_FALSE(lower);
    REQUIRE(lower.error().block.has_value());
    CHECK(*lower.error().block == 1u);
}

TEST_CASE("parse_block_batch of no blocks is an empty batch", "[parse_block_batch]") {
    auto const parsed = parse_block_batch(std::vector<data_chunk>{});
    REQUIRE(parsed);
    CHECK(parsed->blocks.empty());
    CHECK(parsed->txids.empty());
}

TEST_CASE("fork_join_for calls every index once and joins before returning", "[parse_block_batch]") {
    size_t const count = fork_join_workers(1'000) * 5 + 2;
    std::vector<int> calls(count, 0);
    REQUIRE(fork_join_for(count, [&](size_t i) { ++calls[i]; }));
    for (auto const c : calls) {
        CHECK(c == 1);
    }

    // A call that throws is reported, not propagated.
    CHECK_FALSE(fork_join_for(count, [](size_t i) {
        if (i == 1) {
            throw std::runtime_error("one call failed");
        }
    }));
}
