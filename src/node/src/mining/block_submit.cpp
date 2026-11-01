// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/mining/block_submit.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

#include <kth/blockchain/interface/block_chain.hpp>

namespace kth::node::mining {

hash_list coinbase_merkle_branch(std::vector<transaction_const_ptr> const& job_txs) {
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
