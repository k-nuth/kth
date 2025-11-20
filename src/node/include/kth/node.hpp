// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_HPP
#define KTH_NODE_HPP

/**
 * API Users: Include only this header. Direct use of other headers is fragile
 * and unsupported as header organization is subject to change.
 *
 * Maintainers: Do not include this header internal to this library.
 */

#include <kth/blockchain.hpp>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/node/configuration.hpp>
#include <kth/node/define.hpp>
#include <kth/node/full_node.hpp>
#include <kth/node/parser.hpp>
#include <kth/node/settings.hpp>
#include <kth/node/version.hpp>
#include <kth/node/executor/executor.hpp>

#if ! defined(__EMSCRIPTEN__)
#include <kth/node/protocols/protocol_block_in.hpp>
#include <kth/node/protocols/protocol_block_out.hpp>
#include <kth/node/protocols/protocol_block_sync.hpp>
#include <kth/node/protocols/protocol_double_spend_proof_in.hpp>
#include <kth/node/protocols/protocol_double_spend_proof_out.hpp>
#include <kth/node/protocols/protocol_header_sync.hpp>
#include <kth/node/protocols/protocol_transaction_in.hpp>
#include <kth/node/protocols/protocol_transaction_out.hpp>

#include <kth/node/sessions/session.hpp>
#include <kth/node/sessions/session_block_sync.hpp>
#include <kth/node/sessions/session_header_sync.hpp>
#include <kth/node/sessions/session_inbound.hpp>
#include <kth/node/sessions/session_manual.hpp>
#include <kth/node/sessions/session_outbound.hpp>
#endif

#include <kth/node/utility/check_list.hpp>
#include <kth/node/utility/header_list.hpp>
#include <kth/node/utility/performance.hpp>
#include <kth/node/utility/reservation.hpp>
#include <kth/node/utility/reservations.hpp>

#endif
