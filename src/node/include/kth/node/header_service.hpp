// Copyright (c) 2026 Knuth Project developers.
// Distributed under the MIT software license, see LICENSE.

#ifndef KTH_NODE_HEADER_SERVICE_HPP
#define KTH_NODE_HEADER_SERVICE_HPP

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/network/peer_session.hpp>
#include <kth/node/define.hpp>

namespace kth::node {

// Serving headers depends on the chain, not on who opened the connection.
// The browser integration can register the same P2P handler against its chain.
KND_API ::asio::awaitable<code> serve_headers(blockchain::block_chain const& chain,
    network::peer_session& peer, domain::message::get_headers const& request, uint32_t magic);

}
#endif
