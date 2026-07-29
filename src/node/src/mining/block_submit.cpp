// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/mining/block_submit.hpp>

#include <memory>
#include <utility>

#include <kth/blockchain/interface/block_chain.hpp>

namespace kth::node::mining {

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
