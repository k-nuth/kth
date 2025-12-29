// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_PARALLEL_SYNC_V2_HPP
#define KTH_NODE_PARALLEL_SYNC_V2_HPP

#include <asio/awaitable.hpp>

#include <kth/blockchain.hpp>
#include <kth/network.hpp>
#include <kth/node/block_download_coordinator_v2.hpp>
#include <kth/node/define.hpp>

namespace kth::node {

/// Result of parallel block sync v2
struct parallel_sync_result_v2 {
    code error{error::success};
    uint32_t blocks_downloaded{0};
    uint32_t blocks_validated{0};
    uint32_t final_height{0};
};

/// Parallel block sync using lock-free coordinator v2
/// Downloads blocks from multiple peers in parallel and validates in order
[[nodiscard]]
KND_API
::asio::awaitable<parallel_sync_result_v2> parallel_block_sync_v2(
    blockchain::block_chain& chain,
    blockchain::header_organizer& organizer,
    network::p2p_node& network,
    uint32_t start_height,
    uint32_t target_height,
    parallel_download_config_v2 const& config = {});

} // namespace kth::node

#endif // KTH_NODE_PARALLEL_SYNC_V2_HPP
