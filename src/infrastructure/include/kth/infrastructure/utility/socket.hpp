// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_SOCKET_HPP
#define KTH_INFRASTRUCTURE_SOCKET_HPP

#include <memory>

#include <kth/infrastructure/config/authority.hpp>
#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/asio.hpp>
#include <kth/infrastructure/utility/noncopyable.hpp>
#include <kth/infrastructure/utility/thread.hpp>
#include <kth/infrastructure/utility/threadpool.hpp>

namespace kth {

/// This class is thread safe but the socket may not be used concurrently.
class KI_API socket
    : noncopyable
    /*, public track<socket>*/
{
public:
    using ptr = std::shared_ptr<socket>;

    /// Construct an instance.
    explicit
    socket(threadpool& thread);

    /// Obtain the authority of the remote endpoint.
    infrastructure::config::authority authority() const;

    /// The underlying socket.
    asio::socket& get();

    /// Signal cancel of all outstanding work on the socket.
    void stop();

private:
    // This is thread safe.
    threadpool& thread_;

    // This is protected by mutex.
    asio::socket socket_;
    mutable shared_mutex mutex_;
};

} // namespace kth

#endif
