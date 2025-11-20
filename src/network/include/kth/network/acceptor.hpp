// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_ACCEPTOR_HPP
#define KTH_NETWORK_ACCEPTOR_HPP

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/define.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

/// Create inbound socket connections.
/// This class is thread safe against stop.
/// This class is not safe for concurrent listening attempts.
class KN_API acceptor
  : public enable_shared_from_base<acceptor>, noncopyable, track<acceptor>
{
public:
    using ptr = std::shared_ptr<acceptor>;
    using accept_handler = std::function<void(code const&, channel::ptr)>;

    /// Construct an instance.
    acceptor(threadpool& pool, settings const& settings);

    /// Validate acceptor stopped.
    ~acceptor();

    /// Start the listener on the specified port.
    virtual code listen(uint16_t port);

    /// Accept the next connection available, until canceled.
    virtual void accept(accept_handler handler);

    /// Cancel outstanding accept attempt.
    virtual void stop(code const& ec);

private:
    virtual bool stopped() const;

    void handle_accept(boost_code const& ec, socket::ptr socket, accept_handler handler);

    // These are thread safe.
    std::atomic<bool> stopped_;
    threadpool& pool_;
    settings const& settings_;
    mutable dispatcher dispatch_;

    // These are protected by mutex.
    asio::acceptor acceptor_;
    mutable shared_mutex mutex_;
};

} // namespace kth::network

#endif
