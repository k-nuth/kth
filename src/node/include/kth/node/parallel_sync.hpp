// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_PARALLEL_SYNC_HPP
#define KTH_NODE_PARALLEL_SYNC_HPP

#include <kth/blockchain.hpp>
#include <kth/network.hpp>
#include <kth/node/define.hpp>
#include <kth/node/block_download_coordinator.hpp>
#include <kth/node/sync_session.hpp>

#include <asio/awaitable.hpp>

namespace kth::node {

/// Result of parallel block sync
struct parallel_sync_result {
    code error;
    uint32_t blocks_downloaded{0};
    uint32_t blocks_validated{0};
    uint32_t final_height{0};
};

/// Download and validate blocks in parallel from multiple peers
/// @param chain Blockchain for validation
/// @param organizer Header organizer (contains header index)
/// @param network P2P network for peer access
/// @param start_height First block to download
/// @param target_height Last block to download
/// @param config Download configuration
[[nodiscard]]
KND_API ::asio::awaitable<parallel_sync_result> parallel_block_sync(
    blockchain::block_chain& chain,
    blockchain::header_organizer& organizer,
    network::p2p_node& network,
    uint32_t start_height,
    uint32_t target_height,
    parallel_download_config const& config = {});

} // namespace kth::node

#endif // KTH_NODE_PARALLEL_SYNC_HPP
