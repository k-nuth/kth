// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <crypto/sha256.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <kth/domain.hpp>
#include <kth/node/mining/block_submit.hpp>
using namespace kth;
namespace {
// Original scalar implementation; both paths include transaction hashing.
hash_list original_branch(std::vector<transaction_const_ptr> const& job_txs) {
    hash_list branch;
    hash_list level;
    level.reserve(job_txs.size() + 1);
    level.push_back(null_hash); // the coinbase's slot; only its siblings are needed
    for (auto const& tx : job_txs) {
        level.push_back(tx->hash());
    }
    while (level.size() > 1) {
        branch.push_back(level[1]);
        if (level.size() % 2 != 0) {
            level.push_back(level.back());
        }
        hash_list next;
        next.reserve(level.size() / 2);
        for (size_t i = 0; i < level.size(); i += 2) {
            std::array<uint8_t, 2 * hash_size> pair;
            std::copy(level[i].begin(), level[i].end(), pair.begin());
            std::copy(level[i + 1].begin(), level[i + 1].end(), pair.begin() + hash_size);
            next.push_back(bitcoin_hash(byte_span{pair.data(), pair.size()}));
        }
        level = std::move(next);
    }
    return branch;
}

}
int main() {
    std::cerr << "SHA256: " << kth::SHA256AutoDetect() << "\n";
    using clock = std::chrono::steady_clock;
    std::cout << "transactions,original_us,batched_us,speedup\n";
    for (auto const count : {0u, 1u, 16u, 256u, 4096u, 16384u}) {
        std::vector<transaction_const_ptr> job;
        for (uint32_t i = 0; i < count; ++i) {
            job.push_back(std::make_shared<domain::message::transaction>(domain::chain::transaction{1u, i, {}, {}}));
        }
        auto const expected = original_branch(job);
        if (expected != node::mining::coinbase_merkle_branch(job)) return 1;
        auto const iterations = std::max(10u, 65536u / std::max(1u, count));
        auto measure = [&](auto fn) {
            auto const start = clock::now();
            for (unsigned i = 0; i < iterations; ++i) {
                if (fn(job) != expected) std::abort();
            }
            return std::chrono::duration<double, std::micro>(clock::now() - start).count() / iterations;
        };
        measure(original_branch);
        measure(node::mining::coinbase_merkle_branch);
        std::array<double, 7> original_samples;
        std::array<double, 7> batched_samples;
        for (size_t round = 0; round < original_samples.size(); ++round) {
            if (round % 2 == 0) {
                original_samples[round] = measure(original_branch);
                batched_samples[round] = measure(node::mining::coinbase_merkle_branch);
            } else {
                batched_samples[round] = measure(node::mining::coinbase_merkle_branch);
                original_samples[round] = measure(original_branch);
            }
        }
        std::sort(original_samples.begin(), original_samples.end());
        std::sort(batched_samples.begin(), batched_samples.end());
        auto const original = original_samples[3];
        auto const batched = batched_samples[3];
        std::cout << count << ',' << original << ',' << batched << ',' << original / batched << '\n';
    }
}
