// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SESSION_INBOUND_HPP
#define KTH_NODE_SESSION_INBOUND_HPP

#include <memory>
#include <kth/blockchain.hpp>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/node/define.hpp>

#if ! defined(__EMSCRIPTEN__)
#include <kth/node/sessions/session.hpp>
#endif

namespace kth::node {

class full_node;

/// Inbound connections session, thread safe.
struct KND_API session_inbound : session<network::session_inbound>, track<session_inbound> {
public:
    using ptr = std::shared_ptr<session_inbound>;

    /// Construct an instance.
    session_inbound(full_node& network, blockchain::safe_chain& chain);

protected:
    /// Overridden to attach blockchain protocols.
    void attach_protocols(network::channel::ptr channel) override;

    blockchain::safe_chain& chain_;
};

} // namespace kth::node

#endif // KTH_NODE_SESSION_INBOUND_HPP
