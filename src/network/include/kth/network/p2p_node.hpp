// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_P2P_NODE_HPP
#define KTH_NETWORK_P2P_NODE_HPP

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <boost/unordered/concurrent_flat_set.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <kth/domain.hpp>
#include <kth/infrastructure.hpp>

#include <kth/network/banlist.hpp>
#include <kth/network/define.hpp>
#include <kth/network/hosts.hpp>
#include <kth/network/peer_manager.hpp>
#include <kth/network/peer_session.hpp>
#include <kth/network/protocols_coro.hpp>
#include <kth/network/settings.hpp>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>
#include <asio/thread_pool.hpp>

namespace kth::network {

using kth::awaitable_expected;

// =============================================================================
// Message Handler Interface
// =============================================================================

/// Result of processing a message
enum class message_result {
    continue_processing,  // Keep processing messages
    disconnect,           // Disconnect the peer
    handled,              // Message was handled, continue
    not_handled           // Message was not handled by this handler
};

/// Handler for incoming raw messages
/// Return message_result to indicate what to do next
using message_handler_fn = std::function<::asio::awaitable<message_result>(
    peer_session& peer,
    raw_message const& msg
)>;

/// Handler for typed (already parsed) messages
/// Template parameter Message is the domain message type (e.g., domain::message::ping)
template <typename Message>
using typed_message_handler_fn = std::function<::asio::awaitable<message_result>(
    peer_session& peer,
    Message const& msg
)>;

/// Creates a raw message handler from a typed handler
/// Handles parsing boilerplate: byte_reader creation, from_data call, error handling
template <typename Message>
message_handler_fn make_handler(typed_message_handler_fn<Message> typed_handler) {
    return [typed_handler = std::move(typed_handler)](
        peer_session& peer,
        raw_message const& raw
    ) -> ::asio::awaitable<message_result> {
        byte_reader reader(raw.payload);
        auto result = Message::from_data(reader, peer.negotiated_version());

        if (!result) {
            spdlog::debug("[handler] Failed to parse {} from [{}]",
                Message::command, peer.authority());
            co_return message_result::continue_processing;
        }

        co_return co_await typed_handler(peer, *result);
    };
}

/// Overload for function pointers
template <typename Message>
message_handler_fn make_handler(
    ::asio::awaitable<message_result>(*handler)(peer_session&, Message const&)
) {
    return make_handler<Message>(typed_message_handler_fn<Message>(handler));
}

/// Dispatcher that routes messages to appropriate handlers
/// This is the mutable part - handlers can be added/removed
class KN_API message_dispatcher {
public:
    /// Process a message through registered handlers
    /// Returns true to continue, false to disconnect
    ::asio::awaitable<bool> dispatch(peer_session& peer, raw_message const& msg);

    /// Register a raw handler for a specific command
    void register_handler(std::string const& command, message_handler_fn handler);

    /// Register a typed handler for a specific message type
    /// The message type must have a static 'command' string member
    template <typename Message>
    void register_handler(typed_message_handler_fn<Message> handler) {
        handlers_[Message::command] = make_handler<Message>(std::move(handler));
    }

    /// Register a typed handler from a function pointer
    template <typename Message>
    void register_handler(::asio::awaitable<message_result>(*handler)(peer_session&, Message const&)) {
        handlers_[Message::command] = make_handler<Message>(handler);
    }

    /// Register a default handler for unhandled messages
    void set_default_handler(message_handler_fn handler);

private:
    boost::unordered_flat_map<std::string, message_handler_fn> handlers_;
    message_handler_fn default_handler_;
};

// =============================================================================
// Connection result (combines peer_session with handshake info)
// =============================================================================

struct connection_result {
    peer_session::ptr session;
    handshake_result handshake;
};

// =============================================================================
// Peer event (for peer_supervisor channel)
// =============================================================================

enum class peer_direction {
    inbound,
    outbound
};

/// Result of handshake sent back to connect() caller
struct handshake_response {
    code result;  // error::success or error code
};

/// Channel type for handshake response (one-shot)
using handshake_response_channel = concurrent_channel<handshake_response>;

struct peer_event {
    peer_session::ptr peer;
    peer_direction direction;
    // Optional response channel for connect() to wait on handshake result
    // If nullptr, no response is expected (e.g., for seeding connections)
    std::shared_ptr<handshake_response_channel> response_channel;
};

// =============================================================================
// P2P Node (main networking class)
// =============================================================================

class KN_API p2p_node {
public:
    using ptr = std::shared_ptr<p2p_node>;
    using address = domain::message::network_address;

    explicit p2p_node(settings const& settings);
    ~p2p_node();

    // Lifecycle
    // -------------------------------------------------------------------------

    /// Start the node (load hosts, seed if needed)
    ::asio::awaitable<code> start();

    /// Run the node (start accepting connections and connecting to peers)
    ::asio::awaitable<code> run();

    /// Stop the node
    void stop();

    /// Block until all work is complete
    void join();

    // Properties
    // -------------------------------------------------------------------------

    [[nodiscard]]
    settings const& network_settings() const;

    [[nodiscard]]
    bool stopped() const;

    [[nodiscard]]
    size_t connection_count() const;

    [[nodiscard]]
    infrastructure::config::checkpoint top_block() const;

    void set_top_block(infrastructure::config::checkpoint const& top);
    void set_top_block(infrastructure::config::checkpoint&& top);

    // Manual connections
    // -------------------------------------------------------------------------

    /// Connect to a specific peer and perform handshake
    awaitable_expected<peer_session::ptr> connect(
        std::string const& host,
        uint16_t port);

    // Host management
    // -------------------------------------------------------------------------

    [[nodiscard]]
    size_t address_count() const;

    bool store(address const& addr);
    code fetch_address(address& out) const;
    bool remove(address const& addr);

    // Broadcasting
    // -------------------------------------------------------------------------

    /// Broadcast a message to all connected peers
    template <typename Message>
    ::asio::awaitable<size_t> broadcast(Message const& message) {
        co_return co_await manager_.broadcast(message);
    }

    // Peer access
    // -------------------------------------------------------------------------

    /// Get the peer manager (for advanced use)
    [[nodiscard]]
    peer_manager& peers();

    /// Get a copy of all connected peer sessions
    [[nodiscard]]
    std::vector<peer_session::ptr> get_peers() const;

    /// Get the channel that receives newly connected peers (CSP pattern)
    /// Use this to subscribe to peer connection events without polling.
    [[nodiscard]]
    concurrent_channel<peer_session::ptr>& connected_peers();

    /// Get the thread pool
    [[nodiscard]]
    threadpool& thread_pool();

    // Message handling
    // -------------------------------------------------------------------------

    /// Get the message dispatcher for registering handlers
    [[nodiscard]]
    message_dispatcher& dispatcher();

    // Banlist management
    // -------------------------------------------------------------------------

    /// Get the banlist
    [[nodiscard]]
    banlist& bans();

    [[nodiscard]]
    banlist const& bans() const;

    /// Ban a peer (convenience method)
    void ban_peer(
        peer_session::ptr const& peer,
        std::chrono::seconds duration = banlist::default_ban_duration,
        ban_reason reason = ban_reason::node_misbehaving);

    /// Check if an address is banned
    [[nodiscard]]
    bool is_banned(infrastructure::config::authority const& authority) const;

private:
    // Internal coroutines
    ::asio::awaitable<void> run_seeding();
    ::asio::awaitable<void> run_outbound();
    ::asio::awaitable<void> run_inbound();
    ::asio::awaitable<void> run_peer_protocols(peer_session::ptr peer);
    ::asio::awaitable<void> maintain_outbound_connections();

    // Peer supervisor - manages all peer lifecycles (structured concurrency)
    ::asio::awaitable<void> peer_supervisor();

    // Helper for seeding - takes params by value to avoid lambda capture issues
    ::asio::awaitable<void> connect_to_seed(
        std::string seed_host,
        uint16_t seed_port,
        std::shared_ptr<std::atomic<size_t>> seeds_completed);

    uint64_t generate_nonce();

    settings const& settings_;
    threadpool pool_;
    peer_manager manager_;
    hosts hosts_;
    banlist banlist_;

    // Track IPs currently being connected to (for deduplication)
    // Prevents multiple simultaneous connection attempts to the same IP
    boost::concurrent_flat_set<::asio::ip::address, salted_ip_hasher> pending_connections_;

    // Acceptor for inbound connections
    std::unique_ptr<::asio::ip::tcp::acceptor> acceptor_;

    std::atomic<bool> stopped_{true};
    std::atomic<bool> seeded_{false};
    std::atomic<bool> supervisor_ready_{false};  // Signals that peer_supervisor is ready
    kth::atomic<infrastructure::config::checkpoint> top_block_;

    // Message dispatcher for routing messages to handlers
    message_dispatcher dispatcher_;

    // Channels for structured concurrency (peer_supervisor pattern)
    // New peers are sent here by run_inbound/run_outbound, processed by peer_supervisor
    std::unique_ptr<concurrent_channel<peer_event>> new_peer_channel_;

    // Signal to stop the peer_supervisor gracefully
    std::unique_ptr<concurrent_event_channel> stop_signal_;

    // Channel for notifying sync layer of newly connected peers (CSP pattern)
    // Peers are sent here after successful handshake and addition to manager
    std::unique_ptr<concurrent_channel<peer_session::ptr>> connected_peer_channel_;
};

} // namespace kth::network

#endif // KTH_NETWORK_P2P_NODE_HPP
