// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SESSION_MANUAL_HPP
#define KTH_NODE_SESSION_MANUAL_HPP

#include <memory>
#include <kth/blockchain.hpp>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/node/define.hpp>
#include <kth/node/sessions/session.hpp>

namespace kth::node {

class full_node;

/// Manual connections session, thread safe.
class KND_API session_manual : public session<network::session_manual>, track<session_manual> {
public:
    using ptr = std::shared_ptr<session_manual>;

    /// Construct an instance.
    session_manual(full_node& network, blockchain::safe_chain& chain);

protected:
    /// Overridden to attach blockchain protocols.
    void attach_protocols(network::channel::ptr channel) override;

    blockchain::safe_chain& chain_;
};

} // namespace kth::node

#endif // KTH_NODE_SESSION_MANUAL_HPP
