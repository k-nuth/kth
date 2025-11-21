// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_PROTOCOL_HPP
#define KTH_NETWORK_PROTOCOL_HPP

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/define.hpp>

namespace kth::network {

#define BOUND_PROTOCOL(handler, args) \
    std::bind_front(std::forward<Handler>(handler), shared_from_base<Protocol>(), std::forward<Args>(args)...)

#define BOUND_PROTOCOL_TYPE(handler, args) \
    std::bind_front(std::forward<Handler>(handler), std::shared_ptr<Protocol>(), std::forward<Args>(args)...)

class p2p;

/// Virtual base class for protocol implementation, mostly thread safe.
struct KN_API protocol : enable_shared_from_base<protocol>, noncopyable {
protected:
    using completion_handler = std::function<void()>;
    using event_handler = std::function<void(code const&)>;
    using count_handler = std::function<void(code const&, size_t)>;

    /// Construct an instance.
    protocol(p2p& network, channel::ptr channel, std::string const& name);

    /// Bind a method in the derived class.
    template <typename Protocol, typename Handler, typename... Args>
    auto bind(Handler&& handler, Args&&... args) ->
        decltype(BOUND_PROTOCOL_TYPE(handler, args)) const {
        return BOUND_PROTOCOL(handler, args);
    }

    template <typename Protocol, typename Handler, typename... Args>
    void dispatch_concurrent(Handler&& handler, Args&&... args) {
        dispatch_.concurrent(BOUND_PROTOCOL(handler, args));
    }

    /// Send a message on the channel and handle the result.
    template <typename Protocol, typename Message, typename Handler, typename... Args>
    void send(Message const& packet, Handler&& handler, Args&&... args) {
        channel_->send(packet, BOUND_PROTOCOL(handler, args));
    }

    /// Subscribe to all channel messages, blocking until subscribed.
    template <typename Protocol, typename Message, typename Handler, typename... Args>
    void subscribe(Handler&& handler, Args&&... args) {
        channel_->template subscribe<Message>(BOUND_PROTOCOL(handler, args));
    }

    /// Subscribe to the channel stop, blocking until subscribed.
    template <typename Protocol, typename Handler, typename... Args>
    void subscribe_stop(Handler&& handler, Args&&... args) {
        channel_->subscribe_stop(BOUND_PROTOCOL(handler, args));
    }

    /// Get the address of the channel.
    virtual infrastructure::config::authority authority() const;

    /// Get the protocol name, for logging purposes.
    virtual std::string const& name() const;

    /// Get the channel nonce.
    virtual uint64_t nonce() const;

    /// Get the peer version message.
    virtual version_const_ptr peer_version() const;

    /// Set the peer version message.
    virtual void set_peer_version(version_const_ptr value);

    /// Get the negotiated protocol version.
    virtual uint32_t negotiated_version() const;

    /// Set the negotiated protocol version.
    virtual void set_negotiated_version(uint32_t value);

    /// Get the threadpool.
    virtual threadpool& pool();

    /// Stop the channel (and the protocol).
    virtual void stop(code const& ec);

protected:
    void handle_send(code const& ec, std::string const& command);

private:
    threadpool& pool_;
    dispatcher dispatch_;
    channel::ptr channel_;
    const std::string name_;
};

#undef BOUND_PROTOCOL
#undef BOUND_PROTOCOL_TYPE

#define SEND1(message, method, p1) \
    send<CLASS>(message, &CLASS::method, p1)
#define SEND2(message, method, p1, p2) \
    send<CLASS>(message, &CLASS::method, p1, p2)
#define SEND3(message, method, p1, p2, p3) \
    send<CLASS>(message, &CLASS::method, p1, p2, p3)

#define SUBSCRIBE2(message, method, p1, p2) \
    subscribe<CLASS, message>(&CLASS::method, p1, p2)
#define SUBSCRIBE3(message, method, p1, p2, p3) \
    subscribe<CLASS, message>(&CLASS::method, p1, p2, p3)

#define SUBSCRIBE_STOP1(method, p1) \
    subscribe_stop<CLASS>(&CLASS::method, p1)

#define DISPATCH_CONCURRENT1(method, p1) \
    dispatch_concurrent<CLASS>(&CLASS::method, p1)

} // namespace kth::network

#endif
