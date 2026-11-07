// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_LOCAL_ADMISSION_TASKS_HPP
#define KTH_NODE_DETAIL_LOCAL_ADMISSION_TASKS_HPP

// Not installed. The one task the coordinator starts per locally submitted
// body, and nothing else: it decides nothing, it carries.
//
// The body goes to fast validation the way a download worker's chunk does — a
// suspended send on the same channel — and the chunk carries a slot of its own
// for the one terminal outcome of that body: refused by fast validation,
// dropped by storage as stale, failed to store, or stored. Whoever reaches that
// outcome puts it in the slot with a try_send that cannot find it full, and
// this task, which is waiting on the slot, carries it to the coordinator with a
// send that waits for room. Storage never waits on anything here, so a switch
// waiting for storage to park is not waiting on the coordinator.
//
// Armed with the network's stop token at both waits: cancelled before the
// send started, while it waits for room, or while it waits for the outcome,
// the task ends and the coordinator answers the submission itself at its exit.

#include <stop_token>

#include <asio/awaitable.hpp>

#include <kth/node/define.hpp>
#include <kth/node/sync/messages.hpp>

namespace kth::node::detail {

/// Hand `chunk` — one local body, carrying its `submission` and `local_result`
/// slot — to `fast_validation`, wait for its terminal outcome, and carry that
/// outcome to `events`. Ends on `stop`, on either channel closing, or once the
/// outcome is delivered.
KND_API
::asio::awaitable<void> local_body_handoff(
    sync::fast_validation_input_channel& fast_validation,
    sync::downloaded_chunk chunk,
    std::stop_token stop,
    sync::sync_coordinator_event_channel& events);

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_LOCAL_ADMISSION_TASKS_HPP
