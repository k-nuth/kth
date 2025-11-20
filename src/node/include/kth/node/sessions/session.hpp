// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SESSION_HPP
#define KTH_NODE_SESSION_HPP

#include <utility>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/node/define.hpp>

namespace kth::node {

class full_node;

/// Intermediate session base class template.
/// This avoids having to make network::session into a template.
template <class Session>
class KND_API session : public Session {
protected:
    /// Construct an instance.
    session(full_node& node, bool notify_on_connect)
        : Session(node, notify_on_connect), node_(node) {}

    /// Attach a protocol to a channel, caller must start the channel.
    template <class Protocol, typename... Args>
    typename Protocol::ptr attach(network::channel::ptr channel, Args&&... args) {
        return std::make_shared<Protocol>(node_, channel, std::forward<Args>(args)...);
    }

private:
    // This is thread safe.
    full_node& node_;
};

} // namespace kth::node

#endif
