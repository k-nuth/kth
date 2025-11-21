// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/protocols/protocol_version_31402.hpp>

#include <cstdint>
#include <functional>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/define.hpp>
#include <kth/network/p2p.hpp>
#include <kth/network/protocols/protocol_timer.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

#define NAME "version"
#define CLASS protocol_version_31402

using namespace kd::message;
using namespace std::placeholders;

// TODO: set explicitly on inbound (none or new config) and self on outbound.
// Require the configured minimum and services by default.
// Configured min version is our own but we may require higer for some stuff.
// Configured services was our but we found that most incoming connections are
// set to zero, so that is currently the default (see below).
protocol_version_31402::protocol_version_31402(p2p& network, channel::ptr channel)
    : protocol_version_31402(network, channel
        , network.network_settings().protocol_maximum
        , network.network_settings().services
        , network.network_settings().invalid_services
        , network.network_settings().protocol_minimum
        , version::service::none)
{}

protocol_version_31402::protocol_version_31402(p2p& network, channel::ptr channel, uint32_t own_version, uint64_t own_services, uint64_t invalid_services, uint32_t minimum_version, uint64_t minimum_services)
    : protocol_timer(network, channel, false, NAME)
    , network_(network)
    , user_agent_(network.network_settings().user_agent)
    , own_version_(own_version)
    , own_services_(own_services)
    , invalid_services_(invalid_services)
    , minimum_version_(minimum_version)
    , minimum_services_(minimum_services)
    , CONSTRUCT_TRACK(protocol_version_31402)
{}

// Start sequence.
// ----------------------------------------------------------------------------

void protocol_version_31402::start(event_handler handler) {
    auto const period = network_.network_settings().channel_handshake();

    auto const join_handler = synchronize(handler, 2, NAME, synchronizer_terminate::on_error);

    // The handler is invoked in the context of the last message receipt.
    protocol_timer::start(period, join_handler);

    SUBSCRIBE2(domain::message::version, handle_receive_version, _1, _2);
    SUBSCRIBE2(verack, handle_receive_verack, _1, _2);
    SEND2(version_factory(), handle_send, _1, version::command);
}

domain::message::version protocol_version_31402::version_factory() const {
    auto const& settings = network_.network_settings();
    auto const height = network_.top_block().height();
    KTH_ASSERT_MSG(height <= max_uint32, "Time to upgrade the protocol.");

    domain::message::version version;
    version.set_value(own_version_);
    version.set_services(own_services_);
    version.set_timestamp(static_cast<uint64_t>(zulu_time()));
    version.set_address_receiver(authority().to_network_address());
    version.set_address_sender(settings.self.to_network_address());
    version.set_nonce(nonce());
    // version.set_user_agent(get_user_agent());
    version.set_user_agent(user_agent_);
    version.set_start_height(static_cast<uint32_t>(height));

    // The peer's services cannot be reflected, so zero it.
    version.address_receiver().set_services(version::service::none);

    // We always match the services declared in our version.services.
    version.address_sender().set_services(own_services_);
    return version;
}

// Protocol.
// ----------------------------------------------------------------------------

bool protocol_version_31402::handle_receive_version(code const& ec, version_const_ptr message) {
    if (stopped(ec)) {
        return false;
    }

    if (ec) {
        spdlog::debug("[network] Failure receiving version from [{}] {}", authority(), ec.message());
        set_event(ec);
        return false;
    }

    auto const& settings = network_.network_settings();

    auto blacklisted = std::find_if(begin(settings.user_agent_blacklist), end(settings.user_agent_blacklist), [&message](auto const& x){
        if (x.size() <= message->user_agent().size()) {
            return std::equal(begin(x), end(x), begin(message->user_agent()));
        }
        return std::equal(begin(message->user_agent()), end(message->user_agent()), begin(x));
    });

    if (blacklisted != end(settings.user_agent_blacklist)) {
        spdlog::debug("[network] Invalid user agent (blacklisted) for peer [{}] user agent: {}", authority(), message->user_agent());
        set_event(error::channel_stopped);
        return false;
    }

    spdlog::debug("[network] Peer [{}] protocol version ({}) user agent: {}", authority(), message->value(), message->user_agent());

    // TODO: move these three checks to initialization.
    //-------------------------------------------------------------------------

    if (settings.protocol_minimum < version::level::minimum) {
        spdlog::error("[network] Invalid protocol version configuration, minimum below ({}).", uint32_t(version::level::minimum));
        set_event(error::channel_stopped);
        return false;
    }

    if (settings.protocol_maximum > version::level::maximum) {
        spdlog::error("[network] Invalid protocol version configuration, maximum above ({}).", uint32_t(version::level::maximum));
        set_event(error::channel_stopped);
        return false;
    }

    if (settings.protocol_minimum > settings.protocol_maximum) {
        spdlog::error("[network] Invalid protocol version configuration, minimum exceeds maximum.");
        set_event(error::channel_stopped);
        return false;
    }

    //-------------------------------------------------------------------------

    if ( ! sufficient_peer(message)) {
        set_event(error::channel_stopped);
        return false;
    }

    auto const version = std::min(message->value(), own_version_);
    set_negotiated_version(version);
    set_peer_version(message);

    spdlog::debug("[network] Negotiated protocol version ({}) for [{}]", version, authority());

    SEND2(verack(), handle_send, _1, verack::command);

    // 1 of 2
    set_event(error::success);
    return false;
}

bool protocol_version_31402::sufficient_peer(version_const_ptr message) {
    if ((message->services() & invalid_services_) != 0) {
        spdlog::debug("[network] Invalid peer network services ({}) for [{}]", message->services(), authority());
        return false;
    }

    if ((message->services() & minimum_services_) != minimum_services_) {
        spdlog::debug("[network] Insufficient peer network services ({}) for [{}]", message->services(), authority());
        return false;
    }

    if (message->value() < minimum_version_) {
        spdlog::debug("[network] Insufficient peer protocol version ({}) for [{}]", message->value(), authority());
        return false;
    }

    return true;
}

bool protocol_version_31402::handle_receive_verack(code const& ec, verack_const_ptr) {
    if (stopped(ec)) {
        return false;
    }

    if (ec) {
        spdlog::debug("[network] Failure receiving verack from [{}] {}", authority(), ec.message());
        set_event(ec);
        return false;
    }

    // 2 of 2
    set_event(error::success);
    return false;
}

} // namespace kth::network
