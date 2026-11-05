// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/mining/block_submit.hpp>

#include <crypto/sha256.h>

#include <algorithm>
#include <memory>
#include <utility>

#include <kth/blockchain/interface/block_chain.hpp>

namespace kth::node::mining {

hash_list coinbase_merkle_branch(std::vector<transaction_const_ptr> const& job_txs) {
    hash_list branch;
    hash_list level;
    level.reserve(job_txs.size() + 1);
    for (auto const& tx : job_txs) {
        level.push_back(tx->hash());
    }
    data_chunk input;
    data_chunk output;
    // The omitted coinbase path is the leftmost node at every level.
    // Emit its sibling and hash only the remaining subtrees.
    while ( ! level.empty()) {
        branch.push_back(level.front());
        if (level.size() == 1) {
            break;
        }
        if (level.size() % 2 == 0) {
            level.push_back(level.back());
        }
        auto const pairs = (level.size() - 1) / 2;
        input.resize(pairs * 2 * hash_size);
        output.resize(pairs * hash_size);
        for (size_t i = 0; i < pairs * 2; ++i) {
            std::copy(level[i + 1].begin(), level[i + 1].end(), input.begin() + i * hash_size);
        }
        SHA256D64(output.data(), input.data(), pairs);
        level.resize(pairs);
        for (size_t i = 0; i < pairs; ++i) {
            std::copy_n(output.begin() + i * hash_size, hash_size, level[i].begin());
        }
    }
    return branch;
}

domain::message::block assemble_block(
    domain::chain::header const& header,
    domain::chain::transaction const& coinbase,
    std::vector<transaction_const_ptr> const& job_txs) {

    domain::chain::transaction::list txs;
    txs.reserve(1 + job_txs.size());
    txs.push_back(coinbase);
    for (auto const& tx : job_txs) {
        txs.push_back(*tx); // slice message::transaction -> chain::transaction
    }
    return domain::message::block(header, std::move(txs));
}

::asio::awaitable<code> submit_block_light(
    blockchain::block_chain& chain,
    domain::chain::header const& header,
    domain::chain::transaction const& coinbase,
    std::vector<transaction_const_ptr> const& job_txs) {

    auto const block = std::make_shared<domain::message::block>(
        assemble_block(header, coinbase, job_txs));
    co_return co_await chain.organize(block);
}

} // namespace kth::node::mining
