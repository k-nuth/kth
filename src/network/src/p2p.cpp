// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/p2p.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <kth/domain.hpp>

#include <kth/network/channel.hpp>
#include <kth/network/define.hpp>
#include <kth/network/hosts.hpp>
#include <kth/network/protocols/protocol_address_31402.hpp>
#include <kth/network/protocols/protocol_ping_31402.hpp>
#include <kth/network/protocols/protocol_ping_60001.hpp>
#include <kth/network/protocols/protocol_seed_31402.hpp>
#include <kth/network/protocols/protocol_version_31402.hpp>
#include <kth/network/protocols/protocol_version_70002.hpp>
#include <kth/network/sessions/session_inbound.hpp>
#include <kth/network/sessions/session_manual.hpp>
#include <kth/network/sessions/session_outbound.hpp>
#include <kth/network/sessions/session_seed.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

#define NAME "p2p"

using namespace kth::config;
using namespace std::placeholders;

// This can be exceeded due to manual connection calls and race conditions.
inline
size_t nominal_connecting(settings const& settings) {
    return settings.peers.size() + settings.connect_batch_size * settings.outbound_connections;
}

// This can be exceeded due to manual connection calls and race conditions.
inline
size_t nominal_connected(settings const& settings) {
    return settings.peers.size() + settings.outbound_connections + settings.inbound_connections;
}

p2p::p2p(settings const& settings)
    : settings_(settings)
    , stopped_(true)
    , top_block_({ null_hash, 0 })
    , hosts_(settings_)
    , pending_connect_(nominal_connecting(settings_))
    , pending_handshake_(nominal_connected(settings_))
    , pending_close_(nominal_connected(settings_))
    , threadpool_("network")
    , stop_subscriber_(std::make_shared<stop_subscriber>(threadpool_, NAME "_stop_sub"))
    , channel_subscriber_(std::make_shared<channel_subscriber>(threadpool_, NAME "_sub"))
{}

// This allows for shutdown based on destruct without need to call stop.
p2p::~p2p() {
    p2p::close();
}

// Start sequence.
// ----------------------------------------------------------------------------

void p2p::start(result_handler handler) {
    if ( ! stopped()) {
        handler(error::operation_failed);
        return;
    }

    threadpool_.join();
    threadpool_.spawn(thread_default(settings_.threads), thread_priority::normal);
    stopped_ = false;

    stop_subscriber_->start();
    channel_subscriber_->start();

    // This instance is retained by stop handler and member reference.
    manual_.store(attach_manual_session());

    // This is invoked on a new thread.
    manual_.load()->start([this, handler](code const& ec){
        handle_manual_started(ec, handler);
    });
}

void p2p::start_fake(result_handler handler) {
    if ( ! stopped()) {
        handler(error::operation_failed);
        return;
    }

    stopped_ = false;
    handler(error::success);
}

void p2p::handle_manual_started(code const& ec, result_handler handler) {
    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    if (ec) {
        spdlog::error("[network] Error starting manual session: {}", ec.message());
        handler(ec);
        return;
    }

    handle_hosts_loaded(hosts_.start(), handler);
}

void p2p::handle_hosts_loaded(code const& ec, result_handler handler) {
    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    if (ec) {
        spdlog::error("[network] Error loading host addresses: {}", ec.message());
        handler(ec);
        return;
    }

    // The instance is retained by the stop handler (until shutdown).
    auto const seed = attach_seed_session();

    // This is invoked on a new thread.
    seed->start([this, handler](code const& ec){
        handle_started(ec, handler);
    });
}

void p2p::handle_started(code const& ec, result_handler handler) {
    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    if (ec) {
        spdlog::error("[network] Error seeding host addresses: {}", ec.message());
        handler(ec);
        return;
    }

    // There is no way to guarantee subscription before handler execution.
    // So currently subscription for seed node connections is not supported.
    // Subscription after this return will capture connections established via
    // subsequent "run" and "connect" calls, and will clear on close/destruct.

    // This is the end of the start sequence.
    handler(error::success);
}

// Run sequence.
// ----------------------------------------------------------------------------

void p2p::run(result_handler handler) {
    // Start node.peer persistent connections.
    for (auto const& peer: settings_.peers)
        connect(peer);

    // The instance is retained by the stop handler (until shutdown).
    auto const inbound = attach_inbound_session();

    // This is invoked on a new thread.
    inbound->start([this, handler](code const& ec){
        handle_inbound_started(ec, handler);
    });
}

void p2p::run_chain(result_handler handler) {
    // do nothing
}

void p2p::handle_inbound_started(code const& ec, result_handler handler) {
    if (ec) {
        spdlog::error("[network] Error starting inbound session: {}", ec.message());
        handler(ec);
        return;
    }

    // The instance is retained by the stop handler (until shutdown).
    auto const outbound = attach_outbound_session();

    // This is invoked on a new thread.
    outbound->start([this, handler](code const& ec){
        handle_running(ec, handler);
    });
}

void p2p::handle_running(code const& ec, result_handler handler) {
    if (ec) {
        spdlog::error("[network] Error starting outbound session: {}", ec.message());
        handler(ec);
        return;
    }

    // This is the end of the run sequence.
    handler(error::success);
}

// Specializations.
// ----------------------------------------------------------------------------
// Create derived sessions and override these to inject from derived p2p class.

session_seed::ptr p2p::attach_seed_session() {
    return attach<session_seed>();
}

session_manual::ptr p2p::attach_manual_session() {
    return attach<session_manual>(true);
}

session_inbound::ptr p2p::attach_inbound_session() {
    return attach<session_inbound>(true);
}

session_outbound::ptr p2p::attach_outbound_session() {
    return attach<session_outbound>(true);
}

// Shutdown.
// ----------------------------------------------------------------------------
// All shutdown actions must be queued by the end of the stop call.
// IOW queued shutdown operations must not enqueue additional work.

// This is not short-circuited by a stopped test because we need to ensure it
// completes at least once before returning. This requires a unique lock be
// taken around the entire section, which poses a deadlock risk. Instead this
// is thread safe and idempotent, allowing it to be unguarded.
bool p2p::stop() {
    // This is the only stop operation that can fail.
    auto const result = (hosts_.stop() == error::success);

    // Signal all current work to stop and free manual session.
    stopped_ = true;
    manual_.store({});

    // Prevent subscription after stop.
    stop_subscriber_->stop();
    stop_subscriber_->invoke(error::service_stopped);

    // Prevent subscription after stop.
    channel_subscriber_->stop();
    channel_subscriber_->invoke(error::service_stopped, {});

    // Stop creating new channels and stop those that exist (self-clearing).
    pending_connect_.stop(error::service_stopped);
    pending_handshake_.stop(error::service_stopped);
    pending_close_.stop(error::service_stopped);

    // Signal threadpool to stop accepting work now that subscribers are clear.
    threadpool_.shutdown();

    return result;
}

// This must be called from the thread that constructed this class (see join).
bool p2p::close() {
    // Signal current work to stop and threadpool to stop accepting new work.
    auto const result = p2p::stop();

    // Block on join of all threads in the threadpool.
    threadpool_.join();

    return result;
}

// Properties.
// ----------------------------------------------------------------------------

settings const& p2p::network_settings() const {
    return settings_;
}

infrastructure::config::checkpoint p2p::top_block() const {
    return top_block_.load();
}

void p2p::set_top_block(infrastructure::config::checkpoint&& top) {
    top_block_.store(std::move(top));
}

void p2p::set_top_block(infrastructure::config::checkpoint const& top) {
    top_block_.store(top);
}

bool p2p::stopped() const {
    return stopped_;
}

threadpool& p2p::thread_pool() {
    return threadpool_;
}

// Send.
// ----------------------------------------------------------------------------

// private
void p2p::handle_send(code const& ec, channel::ptr channel,
    channel_handler handle_channel, result_handler handle_complete) {
    handle_channel(ec, channel);
    handle_complete(ec);
}

// Subscriptions.
// ----------------------------------------------------------------------------

void p2p::subscribe_connection(connect_handler handler) {
    channel_subscriber_->subscribe(handler, error::service_stopped, {});
}

void p2p::subscribe_stop(result_handler handler) {
    stop_subscriber_->subscribe(handler, error::service_stopped);
}

// Manual connections.
// ----------------------------------------------------------------------------

void p2p::connect(infrastructure::config::endpoint const& peer) {
    connect(peer.host(), peer.port());
}

void p2p::connect(std::string const& hostname, uint16_t port) {
    if (stopped())
        return;

    auto manual = manual_.load();

    // Connect is invoked on a new thread.
    if (manual)
        manual->connect(hostname, port);
}

void p2p::connect(std::string const& hostname, uint16_t port,
    channel_handler handler) {
    if (stopped()) {
        handler(error::service_stopped, {});
        return;
    }

    auto manual = manual_.load();

    if (manual)
        manual->connect(hostname, port, handler);
}

// Hosts collection.
// ----------------------------------------------------------------------------

size_t p2p::address_count() const {
    return hosts_.count();
}

code p2p::store(address const& address) {
    return hosts_.store(address);
}

void p2p::store(address::list const& addresses, result_handler handler) {
    // Store is invoked on a new thread.
    hosts_.store(addresses, handler);
}

code p2p::fetch_address(address& out_address) const {
    return hosts_.fetch(out_address);
}

code p2p::fetch_addresses(address::list& out_addresses) const {
    return hosts_.fetch(out_addresses);
}

code p2p::remove(address const& address) {
    return hosts_.remove(address);
}

// Pending connect collection.
// ----------------------------------------------------------------------------

code p2p::pend(connector::ptr connector) {
    return pending_connect_.store(connector);
}

void p2p::unpend(connector::ptr connector) {
    connector->stop(error::success);
    pending_connect_.remove(connector);
}

// Pending handshake collection.
// ----------------------------------------------------------------------------

code p2p::pend(channel::ptr channel) {
    return pending_handshake_.store(channel);
}

void p2p::unpend(channel::ptr channel) {
    pending_handshake_.remove(channel);
}

bool p2p::pending(uint64_t version_nonce) const {
    auto const match = [version_nonce](const channel::ptr& element) {
        return element->nonce() == version_nonce;
    };

    return pending_handshake_.exists(match);
}

// Pending close collection (open connections).
// ----------------------------------------------------------------------------

size_t p2p::connection_count() const {
    return pending_close_.size();
}

bool p2p::connected(address const& address) const {
    auto const match = [&address](const channel::ptr& element) {
        return element->authority() == address;
    };

    return pending_close_.exists(match);
}

code p2p::store(channel::ptr channel) {
    auto const address = channel->authority();
    auto const match = [&address](const channel::ptr& element) {
        return element->authority() == address;
    };

    // May return error::address_in_use.
    auto const ec = pending_close_.store(channel, match);

    if ( ! ec && channel->notify())
        channel_subscriber_->relay(error::success, channel);

    return ec;
}

void p2p::remove(channel::ptr channel) {
    pending_close_.remove(channel);
}

} // namespace kth::network
