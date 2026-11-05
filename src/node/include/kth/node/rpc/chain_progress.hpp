// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef KTH_NODE_RPC_CHAIN_PROGRESS_HPP
#define KTH_NODE_RPC_CHAIN_PROGRESS_HPP
#include <cstdint>
namespace kth::node::rpc {
// Progress of the response's block, under that block's own branch. At the
// activation boundary the block keeps its interval while its child changes.
struct chain_progress {
    uint64_t ticks;
    uint64_t target_time;
    uint64_t next_target_time;
};
} // namespace kth::node::rpc
#endif
