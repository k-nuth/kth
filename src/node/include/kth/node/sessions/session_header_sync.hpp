// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SESSION_HEADER_SYNC_HPP
#define KTH_NODE_SESSION_HEADER_SYNC_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <kth/blockchain.hpp>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/node/define.hpp>

#if ! defined(__EMSCRIPTEN__)
#include <kth/node/sessions/session.hpp>
#endif

#include <kth/node/settings.hpp>
#include <kth/node/utility/check_list.hpp>
#include <kth/node/utility/header_list.hpp>

namespace kth::node {

class full_node;

/// Class to manage initial header download connection, thread safe.
class KND_API session_header_sync : public session<network::session_outbound>, track<session_header_sync> {
public:
    using ptr = std::shared_ptr<session_header_sync>;

    session_header_sync(full_node& network, check_list& hashes, blockchain::fast_chain& blockchain, infrastructure::config::checkpoint::list const& checkpoints);

    virtual void start(result_handler handler) override;

protected:
    /// Overridden to attach and start specialized handshake.
    void attach_handshake_protocols(network::channel::ptr channel, result_handler handle_started) override;

    /// Override to attach and start specialized protocols after handshake.
    virtual void attach_protocols(network::channel::ptr channel, header_list::ptr headers, result_handler handler);

private:
    using headers_table = std::vector<header_list::ptr>;

    bool initialize();
    void handle_started(code const& ec, result_handler handler);
    void new_connection(header_list::ptr row, result_handler handler);
    void start_syncing(code const& ec, infrastructure::config::authority const& host, result_handler handler);

    void handle_connect(code const& ec, network::channel::ptr channel, header_list::ptr row, result_handler handler);
    void handle_complete(code const& ec, header_list::ptr row, result_handler handler);
    void handle_channel_start(code const& ec, network::channel::ptr channel, header_list::ptr row, result_handler handler);
    void handle_channel_stop(code const& ec, header_list::ptr row);

    // Thread safe.
    check_list& hashes_;

    // These do not require guard because they are not used concurrently.
    headers_table headers_;
    uint32_t minimum_rate_;
    blockchain::fast_chain& chain_;
    infrastructure::config::checkpoint::list const checkpoints_;
};

} // namespace kth::node

#endif

