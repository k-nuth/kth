// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_CHANNEL_HPP
#define KTH_NETWORK_CHANNEL_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <string>
#include <kth/domain.hpp>
#include <kth/network/define.hpp>
#include <kth/network/message_subscriber.hpp>
#include <kth/network/proxy.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

/// A concrete proxy with timers and state, mostly thread safe.
class KN_API channel
  : public proxy, track<channel>
{
public:
    using ptr = std::shared_ptr<channel>;

    /// Construct an instance.
    channel(threadpool& pool, socket::ptr socket, settings const& settings);

    void start(result_handler handler) override;

    // Properties.

    virtual bool notify() const;
    virtual void set_notify(bool value);

    virtual uint64_t nonce() const;
    virtual void set_nonce(uint64_t value);

    virtual version_const_ptr peer_version() const;
    virtual void set_peer_version(version_const_ptr value);

protected:
    virtual void signal_activity() override;
    virtual void handle_stopping() override;
    virtual bool stopped(code const& ec) const;

private:
    void do_start(code const& ec, result_handler handler);

    void start_expiration();
    void handle_expiration(code const& ec);

    void start_inactivity();
    void handle_inactivity(code const& ec);

    std::atomic<bool> notify_;
    std::atomic<uint64_t> nonce_;
    kth::atomic<version_const_ptr> peer_version_;
    deadline::ptr expiration_;
    deadline::ptr inactivity_;
};

} // namespace kth::network

#endif
