// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_COMPACT_BLOCK_RELAY_HPP
#define KTH_NODE_COMPACT_BLOCK_RELAY_HPP

#include <chrono>
#include <memory>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/node/define.hpp>
#include <kth/node/p2p_node.hpp>

namespace kth::node {

// Reconstructs cmpctblock announcements once the node is caught up, and hands
// a complete block to block_chain::organize — the same entry a mined block
// uses. Anything that cannot be rebuilt (a missing transaction that never
// arrives, a short-id collision, a body that does not match the header) is
// given back to the header ingest, which downloads the block the ordinary way.
//
// The read loop only records the message. Waiting for blocktxn, and waiting
// for the block to connect, happen on the network pool.
class KND_API compact_block_relay {
public:
    static constexpr std::chrono::milliseconds default_blocktxn_timeout{10000};

    compact_block_relay(blockchain::block_chain& chain, p2p_node& network,
        std::chrono::milliseconds blocktxn_timeout = default_blocktxn_timeout);
    ~compact_block_relay();

    compact_block_relay(compact_block_relay const&) = delete;
    compact_block_relay& operator=(compact_block_relay const&) = delete;

    // Install before peers run. The chain and network must outlive the network
    // pool's join(), which drains reconstructions as well as peer tasks.
    void attach();

private:
    struct state;
    std::shared_ptr<state> state_;
    p2p_node& network_;
};

} // namespace kth::node

#endif
