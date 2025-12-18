// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_HPP
#define KTH_NETWORK_HPP

/**
 * API Users: Include only this header. Direct use of other headers is fragile
 * and unsupported as header organization is subject to change.
 *
 * Maintainers: Do not include this header internal to this library.
 */

#include <kth/domain.hpp>
#include <kth/network/define.hpp>
#include <kth/network/settings.hpp>

#include <kth/network/version.hpp>

// Modern coroutine-based networking (keep these)
#include <kth/network/peer_manager.hpp>
#include <kth/network/peer_session.hpp>
#include <kth/network/protocols_coro.hpp>
#include <kth/network/p2p_node.hpp>

// Legacy networking - COMMENTED OUT (coroutine migration cleanup)
// These are replaced by p2p_node + peer_session + peer_manager + protocols_coro
// #include <kth/network/acceptor.hpp>
// #include <kth/network/channel.hpp>
// #include <kth/network/connector.hpp>
#include <kth/network/hosts.hpp>                 // TODO: review - may need to keep or modernize
// #include <kth/network/message_subscriber.hpp>
// #include <kth/network/p2p.hpp>
// #include <kth/network/proxy.hpp>
// Legacy protocols - COMMENTED OUT, replaced by protocols_coro.hpp
// #include <kth/network/protocols/protocol.hpp>
// #include <kth/network/protocols/protocol_address_31402.hpp>
// #include <kth/network/protocols/protocol_events.hpp>
// #include <kth/network/protocols/protocol_ping_31402.hpp>
// #include <kth/network/protocols/protocol_ping_60001.hpp>
// #include <kth/network/protocols/protocol_reject_70002.hpp>
// #include <kth/network/protocols/protocol_seed_31402.hpp>
// #include <kth/network/protocols/protocol_timer.hpp>
// #include <kth/network/protocols/protocol_version_31402.hpp>
// #include <kth/network/protocols/protocol_version_70002.hpp>
// Legacy sessions - COMMENTED OUT, replaced by p2p_node + peer_session
// #include <kth/network/sessions/session.hpp>
// #include <kth/network/sessions/session_batch.hpp>
// #include <kth/network/sessions/session_inbound.hpp>
// #include <kth/network/sessions/session_manual.hpp>
// #include <kth/network/sessions/session_outbound.hpp>
// #include <kth/network/sessions/session_seed.hpp>

#endif
