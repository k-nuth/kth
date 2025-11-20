// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_PROTOCOL_HEADER_SYNC_HPP
#define KTH_NODE_PROTOCOL_HEADER_SYNC_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/node/configuration.hpp>
#include <kth/node/define.hpp>
#include <kth/node/utility/header_list.hpp>

namespace kth::node {

class full_node;

/// Headers sync protocol, thread safe.
class KND_API protocol_header_sync : public network::protocol_timer, public track<protocol_header_sync> {
public:
    using ptr = std::shared_ptr<protocol_header_sync>;

    /// Construct a header sync protocol instance.
    protocol_header_sync(full_node& network, network::channel::ptr channel, header_list::ptr headers, uint32_t minimum_rate);

    /// Start the protocol.
    virtual void start(event_handler handler);

private:
    void send_get_headers(event_handler complete);
    void handle_event(code const& ec, event_handler complete);
    void headers_complete(code const& ec, event_handler handler);
    bool handle_receive_headers(code const& ec, headers_const_ptr message, event_handler complete);

    // Thread safe and guarded by sequential header sync.
    header_list::ptr headers_;

    // This is guarded by protocol_timer/deadline contract (exactly one call).
    size_t current_second_;

    const uint32_t minimum_rate_;
    size_t const start_size_;
};

} // namespace kth::node

#endif
