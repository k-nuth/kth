// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Canonical transaction ordering (CTOR) as Bitcoin Cash Node defines it
// (commit 07576013c91ff4a3a74acd85f189c69121cdad1b): consensus/merkle.cpp and
// the "tx-ordering" check of CheckBlock, comparing transaction ids with
// base_blob::Compare (src/uint256.h), i.e. most significant byte first, which is
// the LAST byte of the little-endian digest.
// Original: Copyright (c) The Bitcoin developers, MIT license.

#include <test_helpers.hpp>

#include <algorithm>
#include <vector>

using namespace kth;
namespace chain = kth::domain::chain;

namespace {

// Both digests are captured in locals so each range belongs to one object.
bool ctor_less(chain::transaction const& a, chain::transaction const& b) {
    auto const ha = a.hash();
    auto const hb = b.hash();
    return std::lexicographical_compare(ha.rbegin(), ha.rend(), hb.rbegin(), hb.rend());
}

chain::transaction tx_with_seed(uint32_t seed) {
    hash_digest h{};
    h.fill(uint8_t(seed));
    h[0] = uint8_t(seed >> 8);
    chain::input::list inputs;
    inputs.emplace_back(chain::output_point{h, seed}, chain::script(data_chunk(3, 0x51), false), 0xffffffff);
    chain::output::list outputs;
    outputs.emplace_back(1000 + seed, chain::script(data_chunk(3, 0x51), false), chain::token_data_opt{});
    return chain::transaction(1, 0, std::move(inputs), std::move(outputs));
}

chain::transaction coinbase() {
    chain::input::list inputs;
    inputs.emplace_back(chain::output_point{null_hash, chain::point::null_index}, chain::script(data_chunk(4, 0x51), false), 0xffffffff);
    chain::output::list outputs;
    outputs.emplace_back(1, chain::script(data_chunk(1, 0x51), false), chain::token_data_opt{});
    return chain::transaction(1, 0, std::move(inputs), std::move(outputs));
}

chain::block block_of(std::vector<chain::transaction> const& txs) {
    chain::transaction::list all;
    all.push_back(coinbase());
    for (auto const& tx : txs) all.push_back(tx);
    return chain::block(chain::header{1, null_hash, null_hash, 0, 0, 0}, std::move(all));
}

std::vector<chain::transaction> many(size_t n) {
    std::vector<chain::transaction> txs;
    for (uint32_t i = 0; i < n; ++i) txs.push_back(tx_with_seed(i + 1));
    return txs;
}

} // namespace

TEST_CASE("BCHN CTOR - block::is_canonical_ordered accepts ascending order (most significant byte first)",
          "[bchn_non_vm][ctor]") {
    for (size_t n : {size_t(2), size_t(3), size_t(8), size_t(40)}) {
        DYNAMIC_SECTION(n << " transactions") {
            auto txs = many(n);
            std::sort(txs.begin(), txs.end(), ctor_less);
            REQUIRE(std::is_sorted(txs.begin(), txs.end(), ctor_less));
            CHECK(block_of(txs).is_canonical_ordered());
        }
    }
}

TEST_CASE("BCHN CTOR - block::is_canonical_ordered rejects strictly descending order",
          "[bchn_non_vm][ctor]") {
    for (size_t n : {size_t(2), size_t(3), size_t(8), size_t(40)}) {
        DYNAMIC_SECTION(n << " transactions") {
            auto txs = many(n);
            std::sort(txs.begin(), txs.end(), [](auto const& a, auto const& b) { return ctor_less(b, a); });
            // strictly descending by the normative comparator, not just "not ascending"
            for (size_t i = 1; i < txs.size(); ++i) REQUIRE(ctor_less(txs[i], txs[i - 1]));
            CHECK( ! block_of(txs).is_canonical_ordered());
        }
    }
}
