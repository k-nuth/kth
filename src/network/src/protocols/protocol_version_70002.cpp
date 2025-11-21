// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/protocols/protocol_version_70002.hpp>

#include <cstdint>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/p2p.hpp>
#include <kth/network/protocols/protocol_version_31402.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

#define NAME "version"
#define CLASS protocol_version_70002

using namespace kd::message;
using namespace std::placeholders;

static const std::string insufficient_version = "insufficient-version";
static const std::string insufficient_services = "insufficient-services";

// TODO: set explicitly on inbound (none or new config) and self on outbound.
// Configured services was our but we found that most incoming connections are
// set to zero, so that is currently the default (see below).
protocol_version_70002::protocol_version_70002(p2p& network, channel::ptr channel)
    : protocol_version_70002(network, channel,
        network.network_settings().protocol_maximum,
        network.network_settings().services,
        network.network_settings().invalid_services,
        network.network_settings().protocol_minimum,
        version::service::none,
        network.network_settings().relay_transactions) {}

protocol_version_70002::protocol_version_70002(p2p& network, channel::ptr channel, uint32_t own_version, uint64_t own_services, uint64_t invalid_services, uint32_t minimum_version, uint64_t minimum_services, bool relay)
    : protocol_version_31402(network, channel, own_version, own_services, invalid_services, minimum_version, minimum_services)
    , relay_(relay)
    , CONSTRUCT_TRACK(protocol_version_70002) {}

// Start sequence.
// ----------------------------------------------------------------------------

void protocol_version_70002::start(event_handler handler) {
    protocol_version_31402::start(handler);
    SUBSCRIBE2(reject, handle_receive_reject, _1, _2);
}

domain::message::version protocol_version_70002::version_factory() const {
    auto version = protocol_version_31402::version_factory();

    // This is the only difference at protocol level 70001.
    version.set_relay(relay_);
    return version;
}

// Protocol.
// ----------------------------------------------------------------------------

bool protocol_version_70002::sufficient_peer(version_const_ptr message) {
    if (message->value() < minimum_version_) {
        const reject version_rejection {
            reject::reason_code::obsolete,
            version::command,
            insufficient_version
        };

        SEND2(version_rejection, handle_send, _1, reject::command);
    } else if ((message->services() & minimum_services_) != minimum_services_) {
        const reject obsolete_rejection {
            reject::reason_code::obsolete,
            version::command,
            insufficient_services
        };

        SEND2(obsolete_rejection, handle_send, _1, reject::command);
    }

    return protocol_version_31402::sufficient_peer(message);
}

bool protocol_version_70002::handle_receive_reject(code const& ec, reject_const_ptr reject) {
    if (stopped(ec)) {
        return false;
    }

    if (ec) {
        spdlog::debug("[network] Failure receiving reject from [{}] {}", authority(), ec.message());
        set_event(error::channel_stopped);
        return false;
    }

    auto const& message = reject->message();

    // Handle these in the reject protocol.
    if (message != version::command) {
        return true;
    }

    auto const code = reject->code();

    // Client is an obsolete, unsupported version.
    if (code == reject::reason_code::obsolete) {
        spdlog::debug("[network] Obsolete version reject from [{}] '{}'", authority(), reject->reason());
        set_event(error::channel_stopped);
        return false;
    }

    // Duplicate version message received.
    if (code == reject::reason_code::duplicate) {
        spdlog::debug("[network] Duplicate version reject from [{}] '{}'", authority(), reject->reason());
        set_event(error::channel_stopped);
        return false;
    }

    return true;
}

} // namespace kth::network
