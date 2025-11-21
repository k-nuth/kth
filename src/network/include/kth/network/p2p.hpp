// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_P2P_HPP
#define KTH_NETWORK_P2P_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <kth/domain.hpp>

#include <kth/network/channel.hpp>
#include <kth/network/define.hpp>
#include <kth/network/hosts.hpp>
#include <kth/network/message_subscriber.hpp>
#include <kth/network/sessions/session_inbound.hpp>
#include <kth/network/sessions/session_manual.hpp>
#include <kth/network/sessions/session_outbound.hpp>
#include <kth/network/sessions/session_seed.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

/// Top level public networking interface, partly thread safe.
struct KN_API p2p : enable_shared_from_base<p2p>, noncopyable {
public:
    using ptr = std::shared_ptr<p2p>;
    using address = domain::message::network_address;
    using stop_handler = std::function<void()>;
    using truth_handler = std::function<void(bool)>;
    using count_handler = std::function<void(size_t)>;
    using result_handler = std::function<void(code const&)>;
    using address_handler = std::function<void(code const&, address const&)>;
    using channel_handler = std::function<void(code const&, channel::ptr)>;
    using connect_handler = std::function<bool(code const&, channel::ptr)>;
    using stop_subscriber = subscriber<code>;
    using channel_subscriber = resubscriber<code, channel::ptr>;

    // Templates (send/receive).
    // ------------------------------------------------------------------------

    /// Send message to all connections.
    template <typename Message>
    void broadcast(Message const& message, channel_handler handle_channel, result_handler handle_complete) {
        // Safely copy the channel collection.
        auto const channels = pending_close_.collection();

        // Invoke the completion handler after send complete on all channels.
        auto const join_handler = synchronize(handle_complete, channels.size(),
            "p2p_join", synchronizer_terminate::on_count);

        // No pre-serialize, channels may have different protocol versions.
        for (auto const channel: channels) {
            channel->send(message, std::bind(&p2p::handle_send, this, std::placeholders::_1, channel, handle_channel, join_handler));
        }
    }

    // Constructors.
    // ------------------------------------------------------------------------

    /// Construct an instance.
    p2p(settings const& settings);

    /// Ensure all threads are coalesced.
    virtual
    ~p2p();

    // Start/Run sequences.
    // ------------------------------------------------------------------------

    /// Invoke startup and seeding sequence, call from constructing thread.
    virtual
    void start(result_handler handler);

    virtual
    void start_fake(result_handler handler);

    /// Synchronize the blockchain and then begin long running sessions,
    /// call from start result handler. Call base method to skip sync.
    virtual
    void run(result_handler handler);

    virtual
    void run_chain(result_handler handler);

    // Shutdown.
    // ------------------------------------------------------------------------

    /// Idempotent call to signal work stop, start may be reinvoked after.
    /// Returns the result of file save operation.
    virtual
    bool stop();

    /// Blocking call to coalesce all work and then terminate all threads.
    /// Call from thread that constructed this class, or don't call at all.
    /// This calls stop, and start may be reinvoked after calling this.
    virtual
    bool close();

    // Properties.
    // ------------------------------------------------------------------------

    /// Network configuration settings.
    virtual
    settings const& network_settings() const;

    /// Return the current top block identity.
    infrastructure::config::checkpoint top_block() const;

    /// Set the current top block identity.
    void set_top_block(infrastructure::config::checkpoint&& top);

    /// Set the current top block identity.
    void set_top_block(infrastructure::config::checkpoint const& top);

    /// Determine if the network is stopped.
    virtual
    bool stopped() const;

    /// Return a reference to the network threadpool.
    virtual
    threadpool& thread_pool();

    // Subscriptions.
    // ------------------------------------------------------------------------

    /// Subscribe to connection creation events.
    virtual
    void subscribe_connection(connect_handler handler);

    /// Subscribe to service stop event.
    virtual
    void subscribe_stop(result_handler handler);

    // Manual connections.
    // ----------------------------------------------------------------------------

    /// Maintain a connection to hostname:port.
    virtual
    void connect(infrastructure::config::endpoint const& peer);

    /// Maintain a connection to hostname:port.
    virtual
    void connect(std::string const& hostname, uint16_t port);

    /// Maintain a connection to hostname:port.
    /// The callback is invoked by the first connection creation only.
    virtual
    void connect(std::string const& hostname, uint16_t port, channel_handler handler);

    // Hosts collection.
    // ------------------------------------------------------------------------

    /// Get the number of addresses.
    virtual
    size_t address_count() const;

    /// Store an address.
    virtual
    code store(address const& address);

    /// Store a collection of addresses (asynchronous).
    virtual
    void store(address::list const& addresses, result_handler handler);

    /// Get a randomly-selected address.
    virtual
    code fetch_address(address& out_address) const;

    /// Get a list of stored hosts
    virtual
    code fetch_addresses(address::list& out_addresses) const;

    /// Remove an address.
    virtual
    code remove(address const& address);

    // Pending connect collection.
    // ------------------------------------------------------------------------

    /// Store a pending connection reference.
    virtual
    code pend(connector::ptr connector);

    /// Free a pending connection reference.
    virtual
    void unpend(connector::ptr connector);

    // Pending handshake collection.
    // ------------------------------------------------------------------------

    /// Store a pending connection reference.
    virtual
    code pend(channel::ptr channel);

    /// Test for a pending connection reference.
    virtual
    bool pending(uint64_t version_nonce) const;

    /// Free a pending connection reference.
    virtual
    void unpend(channel::ptr channel);

    // Pending close collection (open connections).
    // ------------------------------------------------------------------------

    /// Get the number of connections.
    virtual
    size_t connection_count() const;

    /// Store a connection.
    virtual
    code store(channel::ptr channel);

    /// Determine if there exists a connection to the address.
    virtual
    bool connected(address const& address) const;

    /// Remove a connection.
    virtual
    void remove(channel::ptr channel);

protected:

    /// Attach a session to the network, caller must start the session.
    template <typename Session, typename... Args>
    typename Session::ptr attach(Args&&... args) {
        return std::make_shared<Session>(*this, std::forward<Args>(args)...);
    }

    /// Override to attach specialized sessions.
    virtual
    session_seed::ptr attach_seed_session();

    virtual
    session_manual::ptr attach_manual_session();

    virtual
    session_inbound::ptr attach_inbound_session();

    virtual
    session_outbound::ptr attach_outbound_session();

private:
    using pending_channels = kth::pending<channel>;
    using pending_connectors = kth::pending<connector>;

    void handle_manual_started(code const& ec, result_handler handler);
    void handle_inbound_started(code const& ec, result_handler handler);
    void handle_hosts_loaded(code const& ec, result_handler handler);
    void handle_hosts_saved(code const& ec, result_handler handler);
    void handle_send(code const& ec, channel::ptr channel, channel_handler handle_channel, result_handler handle_complete);

    void handle_started(code const& ec, result_handler handler);
    void handle_running(code const& ec, result_handler handler);

    // These are thread safe.
    settings const& settings_;
    std::atomic<bool> stopped_;
    kth::atomic<infrastructure::config::checkpoint> top_block_;
    kth::atomic<session_manual::ptr> manual_;
    threadpool threadpool_;
    hosts hosts_;
    pending_connectors pending_connect_;
    pending_channels pending_handshake_;
    pending_channels pending_close_;
    stop_subscriber::ptr stop_subscriber_;
    channel_subscriber::ptr channel_subscriber_;
};

} // namespace kth::network

#endif
