// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_CAPNP_RPC_BRIDGE_HPP
#define KTH_NODE_CAPNP_RPC_BRIDGE_HPP

#include <atomic>
#include <mutex>
#include <cstddef>
#include <cstdint>
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
// (moved, never shared) or one of two thread-safe primitives:
//
//   KJ thread -> owner strand : a bounded asio concurrent_channel `mailbox`.
//                               try_send only; the KJ loop never waits.
//   owner strand -> KJ thread : a kj CrossThreadPromiseFulfiller per call, plus
//                               one for stop. fulfill() is const and
//                               thread-safe, and a no-op once the call (or the
//                               whole KJ loop) is gone.
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
    std::uint64_t id;
    op_kind kind;
    hash_digest hash;
    completion done;
};

struct cancel_msg {
    std::uint64_t id;
};

// The KJ loop is up and listening.
struct listening_msg {
    std::uint16_t port;
};

// The KJ thread is finished with all KJ objects (or never got going).
struct exited_msg {
    std::string error;   // empty on a clean exit
};

// Wakes the owner so it re-evaluates its state; carries nothing.
struct wake_msg {};

struct stop_msg {};

using command = std::variant<request_msg, cancel_msg, listening_msg, exited_msg, wake_msg, stop_msg>;
using mailbox_type = ::asio::experimental::concurrent_channel<void(std::error_code, command)>;

struct credentials {
    std::string expected;   // "user:password"
};

// State shared by both sides. Owned by the server; outlives the KJ thread.
struct bridge {
    bridge(::asio::any_io_executor owner, limits const& lim)
        : lim(lim)
        // Per admitted call at most: one request, one cancel; plus the one-offs.
        , mailbox(owner, 2 * lim.max_in_flight + 8) {}

    limits const lim;
    mailbox_type mailbox;
    // Admitted calls whose owner-side work has not finished. Incremented by the
    // KJ thread on admission, decremented by the owner when the work is done.
    std::atomic<std::size_t> in_flight{0};
    std::atomic<std::uint64_t> next_id{1};
    // Set by the KJ thread (release) before its final wake; read by the owner.
    std::atomic<bool> kj_exited{false};
    std::string kj_error;   // written before kj_exited, read after

private:
    // Held only to swap or use the handle; fulfill() does not wait for the KJ
    // loop, so the lock is never held across anything slow.
    std::mutex stop_mutex;
    stop_signal stop_handle;
    bool stop_requested = false;

public:
    // Ends the KJ loop. Thread-safe, never blocks, idempotent, and works before
    // the loop exists: the KJ thread honors a request made earlier. This is the
    // only way the owner (or the destructor) tells the KJ thread to stop.
    void request_stop() {
        std::lock_guard guard(stop_mutex);
        stop_requested = true;
        if (stop_handle) stop_handle->fulfill();
    }

    // KJ thread: publish the handle, then honor an earlier request.
    void publish_stop(stop_signal handle) {
        std::lock_guard guard(stop_mutex);
        stop_handle = std::move(handle);
        if (stop_requested) stop_handle->fulfill();
    }

    void retract_stop() {
        std::lock_guard guard(stop_mutex);
        stop_handle = nullptr;
    }

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

// The body of the KJ transport thread. Returns when `stop` is fulfilled, the
// listener fails, or binding fails; all KJ objects are destroyed before it
// returns. Sets bridge.kj_exited and posts exited_msg last.
void run_kj_thread(bridge& shared, credentials creds, std::string bind, std::uint16_t port);

} // namespace kth::node::capnp_rpc::detail

#endif // KTH_NODE_CAPNP_RPC_BRIDGE_HPP
