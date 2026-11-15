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
data_chunk sample_block() {
    chain::transaction const coinbase(1, 0,
        {chain::input(chain::output_point(null_hash, chain::point::null_index),
            chain::script(data_chunk{0x01, 0x01}, false), 0xffffffff)},
        {chain::output(50, chain::script(data_chunk{0x51}, false), std::nullopt)});
    chain::transaction const pays(1, 0,
        {chain::input(chain::output_point(hash_digest{0x11}, 3), chain::script{}, 0)},
        {chain::output(7, chain::script(data_chunk{0x52}, false), std::nullopt),
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
