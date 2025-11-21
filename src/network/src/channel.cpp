// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/channel.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <kth/domain.hpp>
#include <kth/network/proxy.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

using namespace kd::message;
using namespace std::placeholders;

// Factory for deadline timer pointer construction.
static
deadline::ptr alarm(threadpool& pool, const asio::duration& duration) {
    return std::make_shared<deadline>(pool, pseudo_random_broken_do_not_use::duration(duration));
}

channel::channel(threadpool& pool, socket::ptr socket, settings const& settings)
    : proxy(pool, socket, settings)
    , notify_(false)
    , nonce_(0)
    , expiration_(alarm(pool, settings.channel_expiration()))
    , inactivity_(alarm(pool, settings.channel_inactivity()))
    , CONSTRUCT_TRACK(channel) {}

// Talk sequence.
// ----------------------------------------------------------------------------

// public:
void channel::start(result_handler handler) {
    proxy::start(std::bind(&channel::do_start, shared_from_base<channel>(), _1, handler));
}

// Don't start the timers until the socket is enabled.
void channel::do_start(code const& ec, result_handler handler) {
    start_expiration();
    start_inactivity();
    handler(error::success);
}

// Properties.
// ----------------------------------------------------------------------------

bool channel::notify() const {
    return notify_;
}

void channel::set_notify(bool value) {
    notify_ = value;
}

uint64_t channel::nonce() const {
    return nonce_;
}

void channel::set_nonce(uint64_t value) {
    nonce_.store(value);
}

version_const_ptr channel::peer_version() const {
    auto const version = peer_version_.load();
    KTH_ASSERT_MSG(version, "Read peer version before set.");
    return version;
}

void channel::set_peer_version(version_const_ptr value) {
    peer_version_.store(value);
}

// Proxy pure virtual protected and ordered handlers.
// ----------------------------------------------------------------------------

// It is possible that this may be called multiple times.
void channel::handle_stopping() {
    expiration_->stop();
    inactivity_->stop();
}

void channel::signal_activity() {
    start_inactivity();
}

bool channel::stopped(code const& ec) const {
    return proxy::stopped() || ec == error::channel_stopped || ec == error::service_stopped;
}

// Timers (these are inherent races, requiring stranding by stop only).
// ----------------------------------------------------------------------------

void channel::start_expiration() {
    if (proxy::stopped()) {
        return;
    }
    expiration_->start(std::bind(&channel::handle_expiration, shared_from_base<channel>(), _1));
}

void channel::handle_expiration(code const& ec) {
    if (stopped(ec)) {
        return;
    }

    spdlog::debug("[network] Channel lifetime expired [{}]", authority());

    stop(error::channel_timeout);
}

void channel::start_inactivity() {
    if (proxy::stopped()) {
        return;
    }

    inactivity_->start(std::bind(&channel::handle_inactivity, shared_from_base<channel>(), _1));
}

void channel::handle_inactivity(code const& ec) {
    if (stopped(ec)) {
        return;
    }

    spdlog::debug("[network] Channel inactivity timeout [{}]", authority());

    stop(error::channel_timeout);
}

} // namespace kth::network