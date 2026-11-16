// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_CAPNP_RPC_BRIDGE_HPP
#define KTH_NODE_CAPNP_RPC_BRIDGE_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stop_token>
#include <string>
#include <system_error>
#include <variant>

#include <asio/any_io_executor.hpp>
#include <asio/experimental/concurrent_channel.hpp>
#include <kj/async.h>

#include <kth/infrastructure/hash_define.hpp>
#include <kth/node/capnp_rpc/reader.hpp>
#include <kth/node/capnp_rpc/server.hpp>

// The seam between the two runtimes. Everything that crosses it is a value
// (moved, never shared) or one of these thread-safe primitives:
//
//   KJ thread -> owner strand : a bounded asio concurrent_channel `mailbox`.
//                               try_send only; the KJ loop never waits. The KJ
//                               thread's exit is posted to the strand instead,
//                               which cannot fail, so it is never lost.
//   owner strand -> KJ thread : a kj CrossThreadPromiseFulfiller per call.
//                               fulfill() is const and thread-safe, and a no-op
//                               once the call (or the whole KJ loop) is gone.
//   anyone -> KJ thread       : a std::stop_source. The KJ thread's
//                               stop_callback fulfills the loop's stop promise.
//   both                      : `in_flight`, the one shared count. A slot is
//                               taken on the KJ thread, where admission is
//                               decided before any message is sent, and given
//                               back on the strand, when the operation it covers
//                               has ended there; a message round trip either way
//                               would let admission run ahead of the work it
//                               bounds.
//
// KJ objects (promises, capabilities, streams) are created, used and destroyed
// on the KJ thread only. Chain objects are touched on the owner strand only.

namespace kth::node::capnp_rpc::detail {

enum class op_kind { chain_info, block, transaction };

// What the owner strand answers a call with. Plain data.
using reply = std::variant<failure, chain_info, block_data, transaction_data>;
using completion = kj::Own<const kj::CrossThreadPromiseFulfiller<reply>>;
using stop_signal = kj::Own<const kj::CrossThreadPromiseFulfiller<void>>;

struct request_msg {
    uint64_t id;
    op_kind kind;
    hash_digest hash;
    completion done;
};

struct cancel_msg {
    uint64_t id;
};

// The KJ loop is up and listening.
struct listening_msg {
    uint16_t port;
};

// Wakes the owner so it re-evaluates its state; carries nothing.
struct wake_msg {};

struct stop_msg {};

using command = std::variant<request_msg, cancel_msg, listening_msg, wake_msg, stop_msg>;
using mailbox_type = ::asio::experimental::concurrent_channel<void(std::error_code, command)>;

struct credentials {
    std::string expected;   // "user:password"
};

// State shared by both sides. Owned by the server; outlives the KJ thread.
struct bridge {
    bridge(::asio::any_io_executor owner, limits const& lim)
        : lim(lim)
        // Per admitted call at most: one request, one cancel; plus the one-offs.
        // A cancel for a call answered just before can still be in it: a full
        // mailbox refuses the next call (busy) rather than queue it.
        , mailbox(owner, 2 * lim.max_in_flight + 8) {}

    limits const lim;
    mailbox_type mailbox;
    // Admitted calls whose owner-side work has not finished: see the seam above.
    std::atomic<size_t> in_flight{0};
    // Asks the KJ loop (and the owner) to stop. Thread-safe, never blocks,
    // idempotent, and works before the loop exists.
    std::stop_source stop;
    // KJ thread only.
    uint64_t next_id{1};
    // KJ thread only: server_options::on_connection_released.
    std::function<void()> on_connection_released;

    [[nodiscard]] bool try_admit() {
        auto current = in_flight.load(std::memory_order_relaxed);
        while (current < lim.max_in_flight) {
            if (in_flight.compare_exchange_weak(current, current + 1,
                    std::memory_order_acq_rel, std::memory_order_relaxed)) {
                return true;
            }
        }
        return false;
    }

    void release() {
        in_flight.fetch_sub(1, std::memory_order_acq_rel);
    }

    // Never blocks. A full mailbox is not an error here: see callers.
    bool post(command&& msg) {
        return mailbox.try_send(std::error_code{}, std::move(msg));
    }
};

// How the KJ thread tells the owner it is finished with every KJ object: run on
// the owner strand, with the error that ended it (empty on a clean exit).
using exited_fn = std::function<void(std::string)>;

// The body of the KJ transport thread. Returns when stop is requested, the
// listener fails, or binding fails; all KJ objects are destroyed before it
// calls `exited`, last.
void run_kj_thread(bridge& shared, credentials creds, std::string bind, uint16_t port,
    exited_fn exited);

} // namespace kth::node::capnp_rpc::detail

#endif // KTH_NODE_CAPNP_RPC_BRIDGE_HPP
