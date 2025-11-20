// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_CONNECTOR_HPP
#define KTH_NETWORK_CONNECTOR_HPP

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/define.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

/// Create outbound socket connections.
/// This class is thread safe against stop.
/// This class is not safe for concurrent connection attempts.
class KN_API connector
  : public enable_shared_from_base<connector>, noncopyable, track<connector>
{
public:
    using ptr = std::shared_ptr<connector>;
    using connect_handler = std::function<void(code const& ec, channel::ptr)>;

    /// Construct an instance.
    connector(threadpool& pool, settings const& settings);

    /// Validate connector stopped.
    ~connector();

    /// Try to connect to the endpoint.
    virtual void connect(infrastructure::config::endpoint const& endpoint,
        connect_handler handler);

    /// Try to connect to the authority.
    virtual void connect(const infrastructure::config::authority& authority,
        connect_handler handler);

    /// Try to connect to host:port.
    virtual void connect(std::string const& hostname, uint16_t port,
        connect_handler handler);

    /// Cancel outstanding connection attempt.
    void stop(code const& ec);

private:
    using query_ptr = std::shared_ptr<asio::query>;

    bool stopped() const;

    void handle_resolve(boost_code const& ec, asio::iterator iterator, connect_handler handler);
    void handle_connect(boost_code const& ec, asio::iterator iterator, socket::ptr socket, connect_handler handler);
    void handle_timer(code const& ec, socket::ptr socket, connect_handler handler);

    // These are thread safe
    std::atomic<bool> stopped_;
    threadpool& pool_;
    settings const& settings_;
    mutable dispatcher dispatch_;

    // These are protected by mutex.
    query_ptr query_;
    deadline::ptr timer_;
    asio::resolver resolver_;
    mutable upgrade_mutex mutex_;
};

} // namespace kth::network

#endif
