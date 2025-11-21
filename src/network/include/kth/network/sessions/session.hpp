// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_SESSION_HPP
#define KTH_NETWORK_SESSION_HPP

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <kth/domain.hpp>
#include <kth/network/acceptor.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/connector.hpp>
#include <kth/network/define.hpp>
#include <kth/network/proxy.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

#define BOUND_SESSION(handler, args) \
    std::bind(std::forward<Handler>(handler), shared_from_base<Session>(), std::forward<Args>(args)...)

#define BOUND_SESSION_TYPE(handler, args) \
    std::bind(std::forward<Handler>(handler), std::shared_ptr<Session>(), std::forward<Args>(args)...)

class p2p;

/// Base class for maintaining the lifetime of a channel set, thread safe.
struct KN_API session : enable_shared_from_base<session>, noncopyable {
public:
    using authority = infrastructure::config::authority;
    using address = domain::message::network_address;
    using truth_handler = std::function<void(bool)>;
    using count_handler = std::function<void(size_t)>;
    using result_handler = std::function<void(code const&)>;
    using channel_handler = std::function<void(code const&, channel::ptr)>;
    using accept_handler = std::function<void(code const&, acceptor::ptr)>;
    using host_handler = std::function<void(code const&, authority const&)>;

    /// Start the session, invokes handler once stop is registered.
    virtual void start(result_handler handler);

    /// Subscribe to receive session stop notification.
    virtual void subscribe_stop(result_handler handler);

protected:

    /// Construct an instance.
    session(p2p& network, bool notify_on_connect);

    /// Validate session stopped.
    ~session();

    /// Template helpers.
    // ------------------------------------------------------------------------

    /// Attach a protocol to a channel, caller must start the channel.
    template <typename Protocol, typename... Args>
    typename Protocol::ptr attach(channel::ptr channel, Args&&... args) {
        return std::make_shared<Protocol>(network_, channel, std::forward<Args>(args)...);
    }

    /// Bind a method in the derived class.
    template <typename Session, typename Handler, typename... Args>
    auto bind(Handler&& handler, Args&&... args) ->
        decltype(BOUND_SESSION_TYPE(handler, args)) const {
        return BOUND_SESSION(handler, args);
    }

    /// Bind a concurrent delegate to a method in the derived class.
    template <typename Session, typename Handler, typename... Args>
    auto concurrent_delegate(Handler&& handler, Args&&... args) ->
        delegates::concurrent<decltype(BOUND_SESSION_TYPE(handler, args))> const {
        return dispatch_.concurrent_delegate(std::forward<Handler>(handler), shared_from_base<Session>(), std::forward<Args>(args)...);
    }

    /// Invoke a method in the derived class after the specified delay.
    inline
    void dispatch_delayed(const asio::duration& delay, dispatcher::delay_handler handler) const {
        dispatch_.delayed(delay, handler);
    }

    /// Delay timing for a tight failure loop, based on configured timeout.
    inline
    asio::duration cycle_delay(code const& ec) {
        return (ec == error::channel_timeout || ec == error::service_stopped ||
            ec == error::success) ? asio::seconds(0) :
            settings_.connect_timeout();
    }

    /// Properties.
    // ------------------------------------------------------------------------

    virtual size_t address_count() const;
    virtual size_t connection_count() const;
    virtual code fetch_address(address& out_address) const;
    virtual bool blacklisted(authority const& authority) const;
    virtual bool stopped() const;
    virtual bool stopped(code const& ec) const;

    /// Socket creators.
    // ------------------------------------------------------------------------

    virtual acceptor::ptr create_acceptor();
    virtual connector::ptr create_connector();

    // Pending connect.
    // ------------------------------------------------------------------------

    /// Store a pending connection reference.
    virtual code pend(connector::ptr connector);

    /// Free a pending connection reference.
    virtual void unpend(connector::ptr connector);

    // Pending handshake.
    // ------------------------------------------------------------------------

    /// Store a pending connection reference.
    virtual code pend(channel::ptr channel);

    /// Free a pending connection reference.
    virtual void unpend(channel::ptr channel);

    /// Test for a pending connection reference.
    virtual bool pending(uint64_t version_nonce) const;

    // Registration sequence.
    //-------------------------------------------------------------------------

    /// Register a new channel with the session and bind its handlers.
    virtual void register_channel(channel::ptr channel, result_handler handle_started, result_handler handle_stopped);

    /// Start the channel, override to perform pending registration.
    virtual void start_channel(channel::ptr channel, result_handler handle_started);

    /// Override to attach specialized handshake protocols upon session start.
    virtual void attach_handshake_protocols(channel::ptr channel, result_handler handle_started);

    /// The handshake is complete, override to perform loopback check.
    virtual void handshake_complete(channel::ptr channel, result_handler handle_started);

    // TODO: create session_timer base class.
    threadpool& pool_;
    settings const& settings_;

private:
    using connectors = kth::pending<connector>;

    void handle_stop(code const& ec);
    void handle_starting(code const& ec, channel::ptr channel, result_handler handle_started);
    void handle_handshake(code const& ec, channel::ptr channel, result_handler handle_started);
    void handle_start(code const& ec, channel::ptr channel, result_handler handle_started, result_handler handle_stopped);
    void handle_remove(code const& ec, channel::ptr channel, result_handler handle_stopped);

    // These are thread safe.
    std::atomic<bool> stopped_;
    bool const notify_on_connect_;
    p2p& network_;
    mutable dispatcher dispatch_;
};

#undef BOUND_SESSION
#undef BOUND_SESSION_TYPE

#define BIND1(method, p1) bind<CLASS>(&CLASS::method, p1)
#define BIND2(method, p1, p2) bind<CLASS>(&CLASS::method, p1, p2)
#define BIND3(method, p1, p2, p3) bind<CLASS>(&CLASS::method, p1, p2, p3)
#define BIND4(method, p1, p2, p3, p4) bind<CLASS>(&CLASS::method, p1, p2, p3, p4)
#define BIND5(method, p1, p2, p3, p4, p5) bind<CLASS>(&CLASS::method, p1, p2, p3, p4, p5)
#define BIND6(method, p1, p2, p3, p4, p5, p6) bind<CLASS>(&CLASS::method, p1, p2, p3, p4, p5, p6)
#define BIND7(method, p1, p2, p3, p4, p5, p6, p7) bind<CLASS>(&CLASS::method, p1, p2, p3, p4, p5, p6, p7)

#define CONCURRENT_DELEGATE2(method, p1, p2) concurrent_delegate<CLASS>(&CLASS::method, p1, p2)

} // namespace kth::network

#endif //KTH_NETWORK_SESSION_HPP
