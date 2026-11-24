// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <span>
#include <vector>

#include <kth/blockchain/utxo_builder.hpp>
#include <kth/domain/chain/block.hpp>
#include <kth/domain/machine/opcode.hpp>

using namespace kth;
using namespace kth::blockchain;
namespace chain = kth::domain::chain;

namespace {

// A coinbase, a transaction with a spendable and an OP_RETURN output, and one
// spending the first.
data_chunk sample_block(uint64_t seed = 0) {
    chain::transaction const coinbase(1, 0,
        {chain::input(chain::output_point(null_hash, chain::point::null_index),
            chain::script(data_chunk{0x01, 0x01}, false), 0xffffffff)},
        {chain::output(50 + seed, chain::script(data_chunk{0x51}, false), std::nullopt)});
    chain::transaction const pays(1, 0,
        {chain::input(chain::output_point(hash_digest{0x11}, 3), chain::script{}, 0)},
        {chain::output(7 + seed, chain::script(data_chunk{0x52}, false), std::nullopt),
         chain::output(0, chain::script(data_chunk{uint8_t(domain::machine::opcode::return_), 0x00}, false),
             std::nullopt),
         chain::output(8, chain::script(data_chunk{0x53}, false), std::nullopt)});
    chain::transaction const spends(1, 0,
        {chain::input(chain::output_point(pays.hash(), 0), chain::script{}, 0)},
        {chain::output(6, chain::script(data_chunk{0x54}, false), std::nullopt)});
    chain::block const block(chain::header{1u, null_hash, null_hash, 0u, 0u, 0u},
        {coinbase, pays, spends});
    return kth::to_data_chunk(block);
}

bool same(utxo_compact_block const& a, utxo_compact_block const& b) {
    if (a.outputs.size() != b.outputs.size() || a.inputs.size() != b.inputs.size()) return false;
    for (size_t i = 0; i < a.outputs.size(); ++i) {
        auto const& x = a.outputs[i];
        auto const& y = b.outputs[i];
        if (x.key != y.key || x.raw.data() != y.raw.data() || x.raw.size() != y.raw.size()
            || x.coinbase != y.coinbase || x.tx_start != y.tx_start) return false;
    }
    for (size_t i = 0; i < a.inputs.size(); ++i) {
        if (a.inputs[i].prev_key != b.inputs[i].prev_key) return false;
    }
    return true;
}

} // namespace

TEST_CASE("utxo block parse: identifiers the caller has replace hashing", "[utxo_block_parse]") {
    auto const raw = sample_block();
    byte_reader reader(raw);
    auto const block = chain::block::from_data(reader);
    REQUIRE(block);
    auto const txids = block->to_hashes();

    auto const hashed = parse_utxo_block(byte_span{raw});
    REQUIRE(hashed);
    auto const given = parse_utxo_block(byte_span{raw}, txids);
    REQUIRE(given);
    CHECK(hashed->outputs.size() == 4);     // the OP_RETURN output is left out
    CHECK(hashed->inputs.size() == 2);      // the coinbase input is left out
    CHECK(same(*hashed, *given));
}

TEST_CASE("utxo block parse: the keys are built from the identifiers given", "[utxo_block_parse]") {
    auto const raw = sample_block();
    byte_reader reader(raw);
    auto const block = chain::block::from_data(reader);
    REQUIRE(block);
    auto txids = block->to_hashes();
    txids[1] = hash_digest{0x42};

    auto const given = parse_utxo_block(byte_span{raw}, txids);
    REQUIRE(given);
    auto const expected = utxoz::make_outpoint(std::span<uint8_t const, 32>{txids[1].data(), 32}, 2);
    bool found = false;
    for (auto const& out : given->outputs) {
        found = found || out.key == expected;
    }
    CHECK(found);
}

TEST_CASE("utxo block parse: identifiers that do not match the block are refused", "[utxo_block_parse]") {
    auto const raw = sample_block();
    byte_reader reader(raw);
    auto const block = chain::block::from_data(reader);
    REQUIRE(block);
    auto txids = block->to_hashes();
    txids.pop_back();
    CHECK_FALSE(parse_utxo_block(byte_span{raw}, txids));
    txids.push_back(null_hash);
    txids.push_back(null_hash);
    CHECK_FALSE(parse_utxo_block(byte_span{raw}, txids));
}

TEST_CASE("utxo block parse: a run of blocks gives what each block gives alone", "[utxo_block_parse]") {
    std::vector<data_chunk> raw;
    for (uint64_t seed = 0; seed < 5; ++seed) raw.push_back(sample_block(seed));
    std::vector<byte_span> blocks(raw.begin(), raw.end());

    auto const run = parse_utxo_blocks(blocks);
    REQUIRE(run);
    REQUIRE(run->size() == blocks.size());
    for (size_t i = 0; i < blocks.size(); ++i) {
        auto const alone = parse_utxo_block(blocks[i]);
        REQUIRE(alone);
        CHECK(same((*run)[i], *alone));
    }
    // Different blocks, so different identifiers: none was given another's.
    CHECK((*run)[0].outputs[0].key != (*run)[1].outputs[0].key);
}

TEST_CASE("utxo block parse: a run hashes only the blocks without identifiers", "[utxo_block_parse]") {
    std::vector<data_chunk> raw;
    for (uint64_t seed = 0; seed < 4; ++seed) raw.push_back(sample_block(seed));
    std::vector<byte_span> blocks(raw.begin(), raw.end());

    // Block 0 has none, block 1 has marked ones, and the list ends before 2 and 3.
    hash_list const marked{hash_digest{0x01}, hash_digest{0x02}, hash_digest{0x03}};
    std::vector<std::span<hash_digest const>> const txids{{}, marked};

    auto const run = parse_utxo_blocks(blocks, txids);
    REQUIRE(run);
    for (size_t i : {size_t(0), size_t(2), size_t(3)}) {
        auto const alone = parse_utxo_block(blocks[i]);
        REQUIRE(alone);
        CHECK(same((*run)[i], *alone));
    }
    auto const given = parse_utxo_block(blocks[1], marked);
    REQUIRE(given);
    CHECK(same((*run)[1], *given));
    CHECK((*run)[1].outputs[0].key
        == utxoz::make_outpoint(std::span<uint8_t const, 32>{marked[0].data(), 32}, 0));
}

TEST_CASE("utxo block parse: a run names the block that could not be parsed", "[utxo_block_parse]") {
    std::vector<data_chunk> raw;
    for (uint64_t seed = 0; seed < 4; ++seed) raw.push_back(sample_block(seed));
    raw[2].resize(raw[2].size() - 5);
    std::vector<byte_span> blocks(raw.begin(), raw.end());

    auto const run = parse_utxo_blocks(blocks);
    REQUIRE_FALSE(run);
    CHECK(run.error().block == 2);

    // Identifiers that do not match their block are refused the same way.
    hash_list const one{hash_digest{0x01}};
    raw[2] = sample_block(2);
    blocks.assign(raw.begin(), raw.end());
    std::vector<std::span<hash_digest const>> const txids{{}, one};
    auto const mismatched = parse_utxo_blocks(blocks, txids);
    REQUIRE_FALSE(mismatched);
    CHECK(mismatched.error().block == 1);
}

TEST_CASE("utxo block parse: an empty run gives no blocks", "[utxo_block_parse]") {
    auto const run = parse_utxo_blocks({});
    REQUIRE(run);
    CHECK(run->empty());
}
