// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The "Oversize block" subcase of Bitcoin Cash Node src/test/blockcheck_tests.cpp, blockfail (commit
// 07576013c91ff4a3a74acd85f189c69121cdad1b): a block of about 2 GB made of ~2000 transactions of ~1 MB
// is accepted by the block-size check, and one more transaction makes it exceed the limit (bad-blk-length).
// Original: Copyright (c) 2013-2015 The Bitcoin Core developers, 2017-present The Bitcoin developers, MIT license.
//
// RESOURCES: the block is held in memory once (about 2 GB of transaction data, plus a few MB). Only one
// instance may run, and the test is HIDDEN (tag [.]) so a default run of the suite never allocates it:
//     kth_domain_test "[bchn_2gb]"        # needs ~2.2 GB of free RAM
// It is the real BCHN case: the limit is the absolute 2,000,000,000 bytes (MAX_CONSENSUS_BLOCK_SIZE), no
// parameter is lowered. BCHN disables the merkle check; Knuth's check_body does it, so the header carries the
// real merkle root of the transactions. The 32 MB dynamic limit is another case (validate_block_batch).

#include <test_helpers.hpp>

#include <algorithm>
#include <vector>

using namespace kth;
namespace chain = kth::domain::chain;

namespace {

constexpr size_t absolute_max = 2'000'000'000;
constexpr size_t one_megabyte = 1'000'000;

chain::transaction fat_op_return_tx(uint32_t seed, chain::script const& locking) {
    hash_digest h{};
    h.fill(0x77);
    h[0] = uint8_t(seed);
    h[1] = uint8_t(seed >> 8);
    h[2] = uint8_t(seed >> 16);
    chain::input::list inputs;
    inputs.emplace_back(chain::output_point{h, 0}, chain::script{}, 0xffffffff);
    chain::output::list outputs;
    outputs.emplace_back(42, locking, chain::token_data_opt{});
    return chain::transaction(1, 0, std::move(inputs), std::move(outputs));
}

hash_digest merkle_of(std::vector<hash_digest> hashes) {
    while (hashes.size() > 1) {
        if (hashes.size() % 2 != 0) hashes.push_back(hashes.back());
        std::vector<hash_digest> next;
        for (size_t i = 0; i < hashes.size(); i += 2) {
            data_chunk both(hashes[i].begin(), hashes[i].end());
            both.insert(both.end(), hashes[i + 1].begin(), hashes[i + 1].end());
            next.push_back(bitcoin_hash(both));
        }
        hashes = std::move(next);
    }
    return hashes.front();
}

} // namespace

TEST_CASE("BCHN blockcheck_tests - blockfail, oversize block (2 GB)", "[.][bchn_2gb][bchn_non_vm][block]") {
    // coinbase only block, to measure the base size like the original
    chain::input::list cb_inputs;
    cb_inputs.emplace_back(chain::output_point{null_hash, chain::point::null_index}, chain::script(data_chunk(10, 0x00), false), 0xffffffff);
    chain::output::list cb_outputs;
    cb_outputs.emplace_back(42, chain::script{}, chain::token_data_opt{});
    chain::transaction const coinbase(1, 0, std::move(cb_inputs), std::move(cb_outputs));

    // OP_RETURN OP_PUSHDATA4 <ONE_MEGABYTE - 200 zero bytes>
    size_t const payload = one_megabyte - 200;
    data_chunk raw{0x6a, 0x4e, uint8_t(payload), uint8_t(payload >> 8), uint8_t(payload >> 16), uint8_t(payload >> 24)};
    raw.insert(raw.end(), payload, 0x00);
    byte_reader reader(raw);
    auto locking = chain::script::from_data(reader, false);
    REQUIRE(locking);

    chain::block const base_block(chain::header{1, null_hash, null_hash, 0, 0, 0}, chain::transaction::list{coinbase});
    size_t const base_size = base_block.serialized_size();
    size_t const fat_size = fat_op_return_tx(0, *locking).serialized_size(true);
    size_t const max_count = (absolute_max - base_size) / fat_size;
    REQUIRE(fat_size <= max_tx_size);                       // consensus transaction cap (PR728): each one is valid on its own

    // The transactions, hashed once for the merkle root, then moved into the block
    chain::transaction::list txs;
    txs.reserve(max_count + 2);
    std::vector<hash_digest> hashes;
    hashes.reserve(max_count + 2);
    txs.push_back(coinbase);
    hashes.push_back(coinbase.hash());
    for (size_t i = 0; i < max_count; ++i) {
        txs.push_back(fat_op_return_tx(uint32_t(i + 1), *locking));
        hashes.push_back(txs.back().hash());
    }
    chain::block block(chain::header{1, null_hash, merkle_of(hashes), 0, 0, 0}, std::move(txs));

    // Still accepted at this point: the largest block under the limit
    REQUIRE(block.serialized_size() <= absolute_max);
    REQUIRE(block.serialized_size() + fat_size > absolute_max);
    CHECK(block.check_body() == error::success);

    // One more transaction goes over the maximum allowed block size
    block.transactions().push_back(fat_op_return_tx(uint32_t(max_count + 1), *locking));
    REQUIRE(block.serialized_size() > absolute_max);
    CHECK(block.check_body() == error::block_size_limit);       // bad-blk-length
}
