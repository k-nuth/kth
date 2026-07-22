// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/pools/block_template.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <queue>
#include <unordered_map>
#include <vector>

#include <kth/domain.hpp>

namespace kth::blockchain {

using namespace kd::chain;

namespace {

// The tx hash is already a uniform digest; the low 8 bytes make a fine key hash.
struct digest_hasher {
    size_t operator()(hash_digest const& h) const {
        size_t out;
        std::memcpy(&out, h.data(), sizeof(out));
        return out;
    }
};

// Coinbase reserve, matching BCHN BlockAssembler::resetBlock (miner.cpp).
constexpr uint64_t coinbase_size_reserve = 1000;
constexpr uint64_t coinbase_sigchecks_reserve = 100;

// Give up adding once the block is nearly full and a run of candidates in a row
// have all failed the size/sigchecks test (BCHN's MAX_CONSECUTIVE_FAILURES).
constexpr int max_consecutive_failures = 1000;

// CTOR: ascending reverse-byte (little-endian display) lexicographic txid order,
// matching domain::chain::block::is_canonical_ordered.
bool canonical_less(transaction_const_ptr const& a, transaction_const_ptr const& b) {
    auto const& ha = a->hash();
    auto const& hb = b->hash();
    return std::lexicographical_compare(ha.rbegin(), ha.rend(), hb.rbegin(), hb.rend());
}

} // namespace

block_template build_block_template(mempool const& pool, block_template_context const& ctx) {
    // 1. Point-in-time snapshot of the pool. The mempool is lock-free, so we copy
    //    the entries out (cheap: shared_ptr + a few ints) and build the template
    //    from a self-consistent view instead of querying the live maps.
    std::vector<mempool_entry> entries;
    entries.reserve(pool.size());
    pool.for_each([&](mempool_entry const& e) { entries.push_back(e); });

    auto const n = entries.size();

    block_template result{};
    if (n == 0) {
        return result;
    }

    // 2. txid -> snapshot index.
    std::unordered_map<hash_digest, uint32_t, digest_hasher> index;
    index.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        index.emplace(entries[i].tx->hash(), i);
    }

    // 3. In-mempool parent / child adjacency, derived from the snapshot inputs
    //    (a prevout whose txid is in the pool is an unconfirmed parent). Parents
    //    are de-duplicated so a tx spending two outputs of one parent counts it
    //    once; children are the inverse edges.
    std::vector<std::vector<uint32_t>> parents(n);
    std::vector<std::vector<uint32_t>> children(n);
    for (uint32_t i = 0; i < n; ++i) {
        for (auto const& in : entries[i].tx->inputs()) {
            auto it = index.find(in.previous_output().hash());
            if (it == index.end()) {
                continue;
            }
            auto const parent = it->second;
            if (std::find(parents[i].begin(), parents[i].end(), parent) == parents[i].end()) {
                parents[i].push_back(parent);
                children[parent].push_back(i);
            }
        }
    }

    // 4. Order candidates by individual fee-rate (fee/size), descending. Fee-rate
    //    ordering is mining policy, not consensus, so a double key is fine; ties
    //    break on txid for a deterministic template.
    std::vector<double> feerate(n);
    for (uint32_t i = 0; i < n; ++i) {
        feerate[i] = entries[i].size != 0
            ? static_cast<double>(entries[i].fee) / static_cast<double>(entries[i].size)
            : 0.0;
    }
    std::vector<uint32_t> order(n);
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        if (feerate[a] != feerate[b]) {
            return feerate[a] > feerate[b];
        }
        return canonical_less(entries[a].tx, entries[b].tx);
    });

    // 5. Greedy selection with the BCHN skip/backlog scheme: a child is only added
    //    after all its in-mempool parents, so the selected set is dependency-
    //    complete even though the order is by individual fee-rate.
    auto const max_size = ctx.max_block_size;
    auto const max_sigchecks = ctx.max_block_sigchecks;
    auto const height = ctx.height;
    auto const mtp = ctx.median_time_past;

    std::vector<uint32_t> missing(n);   // in-mempool parents not yet added
    for (uint32_t i = 0; i < n; ++i) {
        missing[i] = static_cast<uint32_t>(parents[i].size());
    }
    std::vector<char> included(n, 0);
    std::vector<char> skipped(n, 0);
    std::queue<uint32_t> backlog;

    uint64_t block_size = coinbase_size_reserve;
    uint64_t block_sigchecks = coinbase_sigchecks_reserve;
    int consecutive_failed = 0;

    size_t cursor = 0;
    while ( ! backlog.empty() || cursor < n) {
        uint32_t i;
        bool from_backlog = false;
        if ( ! backlog.empty()) {
            i = backlog.front();
            backlog.pop();
            from_backlog = true;
        } else {
            i = order[cursor++];
        }

        if (included[i]) {
            continue;
        }

        // Defer a child until all its parents are in the block (backlog items are
        // enqueued only once that holds).
        if ( ! from_backlog && missing[i] != 0) {
            skipped[i] = 1;
            continue;
        }

        auto const& e = entries[i];

        // Block resource limits (BCHN TestTx uses >=).
        if (block_size + e.size >= max_size || block_sigchecks + e.sigchecks >= max_sigchecks) {
            if (++consecutive_failed > max_consecutive_failures && block_size > max_size - coinbase_size_reserve) {
                break;
            }
            continue;
        }

        // Absolute finality at the template height (BIP113 uses the tip's MTP).
        if ( ! e.tx->is_final(height, mtp)) {
            continue;
        }

        consecutive_failed = 0;
        included[i] = 1;
        block_size += e.size;
        block_sigchecks += e.sigchecks;
        result.total_fees += e.fee;
        result.total_size += e.size;
        result.total_sigchecks += e.sigchecks;

        // A child becomes a candidate once its last parent is added; if it was
        // already passed over by the cursor, re-queue it via the backlog.
        for (auto const child : children[i]) {
            if (--missing[child] == 0 && skipped[child]) {
                backlog.push(child);
            }
        }
    }

    // 6. Emit the selected txs in CTOR order (coinbase is prepended by the caller).
    result.txs.reserve(result.total_size != 0 ? n : 0);
    for (uint32_t i = 0; i < n; ++i) {
        if (included[i]) {
            result.txs.push_back(entries[i].tx);
        }
    }
    std::sort(result.txs.begin(), result.txs.end(), canonical_less);

    return result;
}

mining_template make_mining_template(
    uint32_t version, hash_digest const& previous_block_hash, size_t height,
    uint32_t bits, uint32_t median_time_past, uint32_t now,
    uint64_t size_limit, uint64_t sigchecks_limit, block_template selection) {

    auto const min_time = median_time_past + 1;
    auto const coinbase_value = selection.total_fees + block::subsidy(height, true);

    return mining_template{
        version,
        previous_block_hash,
        height,
        bits,
        min_time,
        std::max(min_time, now),
        coinbase_value,
        size_limit,
        sigchecks_limit,
        std::move(selection)};
}

double difficulty_from_bits(uint32_t bits) {
    auto const mantissa = bits & 0x00ffffffu;
    if (mantissa == 0) {
        return 0.0;
    }

    // Standard Bitcoin difficulty: the ratio of the difficulty-1 target
    // (0x1d00ffff) to this target, scaled by the exponent difference.
    int shift = static_cast<int>((bits >> 24) & 0xffu);
    double difficulty = static_cast<double>(0x0000ffff) /
        static_cast<double>(mantissa);
    while (shift < 29) {
        difficulty *= 256.0;
        ++shift;
    }
    while (shift > 29) {
        difficulty /= 256.0;
        --shift;
    }
    return difficulty;
}

} // namespace kth::blockchain
