// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_CHUNK_RESULT_BRIDGE_HPP
#define KTH_NODE_DETAIL_CHUNK_RESULT_BRIDGE_HPP

// Not installed. Shared by the validation and storage result bridges.

#include <string_view>

#include <asio/awaitable.hpp>

#include <kth/node/define.hpp>
#include <kth/node/sync/messages.hpp>

namespace kth::node::detail {

/// Carries each chunk result to the coordinator, waiting for room. Ends when
/// a receive or send reports shutdown; pending sends need cancel() before
/// close(), as in the orchestrator's channel shutdown. `name` labels its logs.
KND_API ::asio::awaitable<void> chunk_result_bridge(
    sync::chunk_validated_channel& input,
    sync::sync_coordinator_event_channel& events,
    std::string_view name);

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_CHUNK_RESULT_BRIDGE_HPP
