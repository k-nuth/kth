// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_P2P_NODE_HPP
#define KTH_NETWORK_P2P_NODE_HPP

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>

#include <boost/unordered/unordered_flat_set.hpp>
#include <memory>
#include <string>
#include <vector>

#include <boost/unordered/concurrent_flat_map.hpp>
#include <boost/unordered/concurrent_flat_set.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <kth/domain.hpp>
#include <kth/infrastructure.hpp>

#include <kth/network/define.hpp>
#include <kth/network/peer_database.hpp>
#include <kth/network/peer_manager.hpp>
#include <kth/network/peer_session.hpp>
#include <kth/network/protocols_coro.hpp>
#include <kth/network/settings.hpp>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <asio/thread_pool.hpp>

namespace kth::node::detail { struct p2p_node_test_seam; }

namespace kth::node {
using namespace kth::network;

using kth::awaitable_expected;

// =============================================================================
// Message Handler Interface
// =============================================================================

/// Result of processing a message
enum class message_result {
    continue_processing,  // Keep processing messages
    disconnect,           // Disconnect the peer
    handled,              // Message was handled, continue
    not_handled           // Message was not handled by this handler
};

/// Handler for incoming raw messages
/// Return message_result to indicate what to do next
using message_handler_fn = std::function<::asio::awaitable<message_result>(
    peer_session& peer,
    raw_message const& msg
)>;

/// Handler for typed (already parsed) messages
/// Template parameter Message is the domain message type (e.g., domain::message::ping)
template <typename Message>
using typed_message_handler_fn = std::function<::asio::awaitable<message_result>(
    peer_session& peer,
    Message const& msg
)>;

/// Creates a raw message handler from a typed handler
/// Handles parsing boilerplate: byte_reader creation, from_data call, error handling
template <typename Message>
message_handler_fn make_handler(typed_message_handler_fn<Message> typed_handler) {
    return [typed_handler = std::move(typed_handler)](
        peer_session& peer,
        raw_message const& raw
    ) -> ::asio::awaitable<message_result> {
        byte_reader reader(raw.payload);
        auto result = Message::from_data(reader, peer.negotiated_version());

        if (!result) {
            spdlog::debug("[handler] Failed to parse {} from [{}]",
                Message::command, peer.authority());
            co_return message_result::continue_processing;
        }

        co_return co_await typed_handler(peer, *result);
    };
}

/// Overload for function pointers
template <typename Message>
message_handler_fn make_handler(
    ::asio::awaitable<message_result>(*handler)(peer_session&, Message const&)
) {
    return make_handler<Message>(typed_message_handler_fn<Message>(handler));
}

/// Dispatcher that routes messages to appropriate handlers
/// This is the mutable part - handlers can be added/removed
class KN_API message_dispatcher {
public:
    /// Process a message through registered handlers
    /// Returns true to continue, false to disconnect
    ::asio::awaitable<bool> dispatch(peer_session& peer, raw_message const& msg);

    /// Register a raw handler for a specific command
    void register_handler(std::string const& command, message_handler_fn handler);

    /// Register a typed handler for a specific message type
    /// The message type must have a static 'command' string member
    template <typename Message>
    void register_handler(typed_message_handler_fn<Message> handler) {
        handlers_[Message::command] = make_handler<Message>(std::move(handler));
    }

    /// Register a typed handler from a function pointer
    template <typename Message>
    void register_handler(::asio::awaitable<message_result>(*handler)(peer_session&, Message const&)) {
        handlers_[Message::command] = make_handler<Message>(handler);
    }

    /// Register a default handler for unhandled messages
    void set_default_handler(message_handler_fn handler);

private:
    boost::unordered_flat_map<std::string, message_handler_fn> handlers_;
    message_handler_fn default_handler_;
};

// =============================================================================
// Connection result (combines peer_session with handshake info)
// =============================================================================

struct connection_result {
    peer_session::ptr session;
    handshake_result handshake;
};

// =============================================================================
// Peer event (for peer_supervisor channel)
// =============================================================================

enum class peer_direction {
    inbound,
    outbound
};

/// Result of handshake sent back to connect() caller
struct handshake_response {
    code result;  // error::success or error code
};

/// Channel type for handshake response (one-shot)
using handshake_response_channel = concurrent_channel<handshake_response>;

struct peer_event {
    peer_session::ptr peer;
    peer_direction direction;
    // Optional response channel for connect() to wait on handshake result
    // If nullptr, no response is expected (e.g., for seeding connections)
    std::shared_ptr<handshake_response_channel> response_channel;
};

/// Type of peer lifecycle event for sync layer notification
enum class peer_event_type { connected, disconnected };

/// Notification sent to sync layer when peer connects or disconnects
struct peer_notification {
    peer_session::ptr peer;
    peer_event_type event;
};

/// Woken when at least one block has been announced. Carries nothing: the
/// announcement itself is registered on the node before this is sent, so a wake
/// that is dropped because one is already queued costs nothing — whoever
/// answers the queued one drains everything registered by then.
struct blocks_announced {};

// =============================================================================
// P2P Node (main networking class)
// =============================================================================

class KN_API p2p_node {
public:
    using ptr = std::shared_ptr<p2p_node>;
    using address = domain::message::network_address;

    explicit p2p_node(settings const& settings);
    ~p2p_node();

    // Lifecycle
    // -------------------------------------------------------------------------

    /// Start the node (load hosts, seed if needed).
    ///
    /// @par Single-use, and single-use from the ADMISSION
    /// A p2p_node can be started ONCE. A second call returns
    /// `error::operation_failed`, including after a clean stop, and including
    /// when the first call went on to fail: what is consumed is the admission,
    /// not the success. An object whose start failed is finished, not ready to
    /// try again.
    ///
    /// This is an observable change. The previous behaviour admitted a second
    /// start whenever the service state said "not running", which it did after
    /// every stop, and then ran it over channels that stop() had closed and a
    /// thread pool that join() had ended. It returned success and did nothing;
    /// the refusal says so instead.
    ///
    /// Restarting would mean rebuilding the pool, the channels and the tasks.
    /// Until something does that, construct a new node.
    ::asio::awaitable<code> start();

    /// Run the node (start accepting connections and connecting to peers).
    ///
    /// Admitted once, like start(). A second call returns
    /// `error::operation_failed` rather than spawning the network tasks again --
    /// among them the status task, which would then be a second coroutine waiting
    /// on the one status_timer_, so a cancellation would end whichever of them
    /// happened to be waiting.
    ::asio::awaitable<code> run();

    /// Stop the node
    void stop();

    /// Block until all work is complete
    void join();

    // Properties
    // -------------------------------------------------------------------------

    [[nodiscard]]
    settings const& network_settings() const;

    [[nodiscard]]
    bool stopped() const;

    [[nodiscard]]
    size_t connection_count() const;

    [[nodiscard]]
    infrastructure::config::checkpoint top_block() const;

    void set_top_block(infrastructure::config::checkpoint const& top);
    void set_top_block(infrastructure::config::checkpoint&& top);

    /// Register a message handler injected by the node. This is how chain-backed
    /// replies (e.g. the BIP-35 `mempool` -> inv) are served: the node owns the
    /// bridge to the blockchain and supplies the handler, so p2p_node itself
    /// stays chain-agnostic (the network layer must not know about the chain).
    template <typename Message>
    void register_message_handler(typed_message_handler_fn<Message> handler) {
        dispatcher_.register_handler<Message>(std::move(handler));
    }

    // Manual connections
    // -------------------------------------------------------------------------

    /// Connect to a specific peer and perform handshake
    awaitable_expected<peer_session::ptr> connect(
        std::string const& host,
        uint16_t port);

    // Host management
    // -------------------------------------------------------------------------

    [[nodiscard]]
    size_t address_count() const;

    bool store(address const& addr);
    code fetch_address(address& out) const;
    bool remove(address const& addr);

    // Broadcasting
    // -------------------------------------------------------------------------

    /// Broadcast a message to all connected peers
    template <typename Message>
    ::asio::awaitable<size_t> broadcast(Message const& message) {
        co_return co_await manager_.broadcast(message);
    }

    // Peer access
    // -------------------------------------------------------------------------

    /// Get the peer manager (for advanced use)
    [[nodiscard]]
    peer_manager& peers();

    /// Get the channel that receives peer lifecycle events (CSP pattern)
    /// Use this to subscribe to peer connection/disconnection events.
    [[nodiscard]]
    concurrent_channel<peer_notification>& peer_events();

    // -------------------------------------------------------------------------
    // Block announcements
    // -------------------------------------------------------------------------
    //
    // Three wire messages mean the same thing — a block exists that you may not
    // have — and they arrive as `headers` nobody asked for, as `cmpctblock`, or
    // as an `inv` naming blocks. They collapse into one registration and one
    // wake, because the action they produce does not depend on which of them
    // arrived: ask for headers from the tip and take everything above it.
    //
    // The hashes are registered here, under a mutex, because they arrive on
    // several peer strands at once. The channel is only a doorbell.

    /// What was announced since the last take. `overflowed` says the registry
    /// refused hashes for want of room, so the list is not the whole story and
    /// something unaccounted for may be missing — a consumer that cannot verify
    /// must assume it is.
    struct announced_blocks {
        hash_list hashes;
        bool overflowed{false};
    };

    /// Register announced block hashes and ring the doorbell.
    void announce_blocks(hash_list const& hashes);

    /// Take everything registered since the last call.
    [[nodiscard]]
    announced_blocks take_announced_blocks();

    /// The doorbell. Woken by announce_blocks(), never carries the hashes.
    [[nodiscard]]
    concurrent_channel<blocks_announced>& block_announcements();

    /// Get the thread pool
    [[nodiscard]]
    threadpool& thread_pool();

    // Message handling
    // -------------------------------------------------------------------------

    /// Get the message dispatcher for registering handlers
    [[nodiscard]]
    message_dispatcher& dispatcher();

    // Ban management
    // -------------------------------------------------------------------------

    /// Ban a peer (convenience method)
    void ban_peer(
        peer_session::ptr const& peer,
        std::chrono::seconds duration = std::chrono::hours{24},
        ban_reason reason = ban_reason::node_misbehaving);

    /// Check if an address is banned
    [[nodiscard]]
    bool is_banned(infrastructure::config::authority const& authority) const;

    /// Check if a peer is slow based on download performance
    /// @param authority The peer's address
    /// @param threshold_ms Threshold in ms/block (default 500ms)
    [[nodiscard]]
    bool is_slow_peer(infrastructure::config::authority const& authority,
                      double threshold_ms = 500.0) const;

    /// Get peer's average download speed (ms per block), 0.0 if unknown
    [[nodiscard]]
    double get_peer_speed(infrastructure::config::authority const& authority) const;

    // Peer database (unified peer storage with reputation and ban management)
    // -------------------------------------------------------------------------

    /// Get the peer database
    [[nodiscard]]
    peer_database& peer_db();

    [[nodiscard]]
    peer_database const& peer_db() const;

    /// Report misbehavior from a peer, returns true if peer should be banned
    /// @param peer The peer that misbehaved
    /// @param score Misbehavior score to add (default thresholds: 10=minor, 50=major, 100=ban)
    /// @param reason Human-readable reason for logging
    /// @return true if peer exceeded ban threshold and was banned
    bool report_misbehavior(
        peer_session::ptr const& peer,
        int score,
        std::string_view reason = {});

    /// Report misbehavior by authority
    bool report_misbehavior(
        infrastructure::config::authority const& authority,
        int score,
        std::string_view reason = {});

    /// Record block download performance for a peer
    void record_peer_performance(
        peer_session::ptr const& peer,
        uint32_t blocks,
        uint32_t time_ms);

    /// Record block download performance by authority
    void record_peer_performance(
        infrastructure::config::authority const& authority,
        uint32_t blocks,
        uint32_t time_ms);

private:
    // Internal coroutines
    ::asio::awaitable<void> run_seeding();
    ::asio::awaitable<void> run_outbound();
    ::asio::awaitable<void> run_inbound();
    ::asio::awaitable<void> run_peer_protocols(peer_session::ptr peer);
    ::asio::awaitable<void> maintain_outbound_connections();

    // Peer supervisor - manages all peer lifecycles (structured concurrency)
    ::asio::awaitable<void> peer_supervisor();

    // Helper for seeding - takes params by value to avoid lambda capture issues
    ::asio::awaitable<void> connect_to_seed(
        std::string seed_host,
        uint16_t seed_port,
        std::shared_ptr<std::atomic<size_t>> seeds_completed);

    uint64_t generate_nonce();

    /// Draw a ping nonce. Never zero -- zero is peer_session's "no ping in
    /// flight" sentinel.
    static
    uint64_t generate_ping_nonce();

    settings const& settings_;
    threadpool pool_;
    peer_manager manager_;
    peer_database peer_db_;  // Unified peer storage (hosts, bans, and reputation)

    // Track IPs currently being connected to (for deduplication)
    // Prevents multiple simultaneous connection attempts to the same IP
    boost::concurrent_flat_set<::asio::ip::address, salted_ip_hasher> pending_connections_;

    // Track recently failed connection IPs with cooldown timestamp
    // Prevents rapid reconnection to failing peers
    boost::concurrent_flat_map<
        ::asio::ip::address,
        std::chrono::steady_clock::time_point,
        salted_ip_hasher
    > failed_connections_;

    // Acceptor for inbound connections
    std::unique_ptr<::asio::ip::tcp::acceptor> acceptor_;

    /// The whole lifecycle, in one atomic word.
    ///
    /// @par Why one value and not several flags
    /// The admission and the active state used to be two atomics — `start()` took
    /// an admission and then published the active state as a separate step — and
    /// two atomics are not one transition: a `stop()` landing between them read
    /// the INITIAL "stopped", concluded there was nothing to stop, and returned,
    /// leaving a node running after the stop that was meant to end it had already
    /// come back.
    ///
    /// A mutex around both steps would fix that by serialising it. One value
    /// removes it instead: there is nothing to serialise when the admission and
    /// the state ARE the same word. start() is one compare-exchange and stop() is
    /// one exchange, and neither needs to say what it holds across what.
    ///
    /// @par The transitions, all of them
    /// `fresh → active` is the only admission, so a start is admitted once and
    /// the compare-exchange says so. Anything `→ stopped` is a stop, and the value
    /// it replaces says what that stop has to do: `active` means there is work,
    /// `fresh` means nothing was ever running but the claim still counts — a stop
    /// that arrives before a start refuses that start rather than being forgotten
    /// — and `stopped` means someone got there first.
    ///
    /// Nothing goes back. `stopped → active` would be a restart, and restarting
    /// would mean rebuilding the pool and the channels, not changing this value.
    ///
    /// @par `active`, not `running`
    /// It is published BEFORE the database load and the seeding finish, so it
    /// says "the lifecycle has been consumed and not stopped", not "the start
    /// succeeded". A stop arriving during that work still finds `active` and
    /// tears down, which is the point.
    ///
    /// @par What this does NOT do
    /// It makes the lifecycle DECISION one step. It does not serialize the rest
    /// of start()'s work against a teardown running beside it — that concurrency
    /// predates this and is not what this change addresses. What it closes is a
    /// stop being lost in the transition itself.
    enum class lifecycle_state : uint8_t { fresh, active, stopped };
    std::atomic<lifecycle_state> lifecycle_{lifecycle_state::fresh};

    /// Whether run() has already spawned the network tasks.
    ///
    /// Monotonic and never cleared, not even when run() returns: the whole object
    /// is single-use, so a node that has run has run.
    ///
    /// @par When it is consumed
    /// Only by a call that gets past the stopped check, which is to say only by a
    /// call on an active node. A run() on a node that was never started answers
    /// `service_stopped` and spends nothing, so the legitimate run() that follows
    /// its start is still admitted.
    ///
    /// @par Deliberately NOT a fourth lifecycle state
    /// That word exists because an admission and an active state had to be
    /// published together. This one is published against nothing: no stop reads
    /// it, and no decision pairs with it. Folding it in would make the transition
    /// table describe two unrelated things.
    ///
    /// @par What it does not do
    /// It stops the tasks from being spawned twice. It does not address a first
    /// run() racing a stop(), which predates this.
    std::atomic<bool> run_admitted_{false};

    /// Serializes everything that touches status_timer_.
    ///
    /// A strand, not the pool's executor. `pool_` is an asio::thread_pool with
    /// several threads, so posting to its executor says nothing about ordering:
    /// the status coroutine can be resuming into expires_after() on one thread
    /// while a cancellation runs on another. Same executor is not serialized
    /// execution, and a clean TSan run would not make it one.
    ///
    /// Declared after pool_ so it is destroyed before the executor it wraps.
    ::asio::strand<::asio::thread_pool::executor_type> status_strand_{
        ::asio::make_strand(pool_.get_executor())};

    /// What the status task waits on, held here so a stop can reach it.
    ///
    /// It used to be a local of that task's coroutine, which is why stopping this
    /// node could take ten seconds with nothing left to do: the wait was only
    /// re-evaluated when the timer expired on its own, the task is detached, and
    /// join() waits for the pool it keeps alive.
    ///
    /// Built on the strand above, and touched only from it.
    ::asio::steady_timer status_timer_{status_strand_};

    /// Ask the status task to look again, now.
    ///
    /// Posted rather than cancelled in place: the wait runs on the pool and stop()
    /// runs on whatever thread asked, and a timer is not safe to cancel from
    /// another thread while it is being waited on.
    ///
    /// A cancellation that arrives before the timer is armed is not lost: stop()
    /// publishes the stopped state first, and the loop re-reads it -- through
    /// stopped() -- before arming.
    ///
    /// @par Why no generation stamp
    /// A wake cannot arrive for a run that is not the current one, because there
    /// is no second run: start() admits ONCE and refuses every call after it,
    /// including after a clean stop. A second admission is structurally
    /// forbidden, so a wake cannot belong to a run other than the current one and
    /// needs no generation stamp to say so.
    ///
    /// The refusal is not decoration. Restarting would mean rebuilding the pool
    /// and the channels — stop() closes new_peer_channel_, stop_signal_ and
    /// peer_notification_channel_, which are built once in the constructor's init
    /// list, and join() on an asio::thread_pool is terminal — so a second run
    /// that was merely admitted would do nothing and say it worked.
    void wake_status_task() noexcept;

    /// Where the status task can be watched. Nothing here is reachable from
    /// outside: the task is detached, logs at info and reports to no one, so
    /// whether a stop cut its wait short or the wait simply expired is
    /// indistinguishable without this.
    enum class status_probe_point { before_arm, armed, woken, reported };

    /// Empty in every build but a control's. Installed only through
    /// detail/p2p_node_test_seam.hpp, which is not installed.
    std::function<void(status_probe_point)> status_probe_;

    void probe_status_task(status_probe_point point) const;

    friend struct detail::p2p_node_test_seam;

    std::atomic<bool> seeded_{false};
    std::atomic<bool> supervisor_ready_{false};  // Signals that peer_supervisor is ready
    std::atomic<int> seed_task_counter_{0};  // For unique seed task names in logging
    std::atomic<int> conn_task_counter_{0};  // For unique connection task names in logging
    std::atomic<int> peer_task_counter_{0};  // For unique peer task names in logging
    kth::atomic<infrastructure::config::checkpoint> top_block_;

    // Message dispatcher for routing messages to handlers
    message_dispatcher dispatcher_;

    // Channels for structured concurrency (peer_supervisor pattern)
    // New peers are sent here by run_inbound/run_outbound, processed by peer_supervisor
    std::unique_ptr<concurrent_channel<peer_event>> new_peer_channel_;

    // Signal to stop the peer_supervisor gracefully
    std::unique_ptr<concurrent_event_channel> stop_signal_;

    // Channel for notifying sync layer of peer lifecycle events (CSP pattern)
    // Peers are sent here on connection (after handshake) and disconnection
    std::unique_ptr<concurrent_channel<peer_notification>> peer_notification_channel_;
    std::unique_ptr<concurrent_channel<blocks_announced>> block_announcement_channel_;

    /// The three forms an announcement arrives in, each turned into the same
    /// registration. Every one of them states what it does with the message;
    /// none accepts a message and discards it without saying so.
    void announce_from_headers(peer_session& peer, raw_message const& raw);
    void announce_from_compact_block(peer_session& peer, raw_message const& raw);
    void announce_from_inventory(peer_session& peer, raw_message const& raw);

    // Peers choose what goes in here, so it is bounded and its membership test
    // is constant time. Beyond the bound new hashes are refused and the refusal
    // is recorded: the coordinator asks once when it cannot tell, which is the
    // same single request any announcement produces.
    static constexpr size_t max_announced_blocks = 1024;

    mutable std::mutex announced_blocks_mutex_;
    boost::unordered_flat_set<hash_digest> announced_blocks_;
    bool announced_blocks_overflowed_{false};
};

} // namespace kth::node

#endif // KTH_NETWORK_P2P_NODE_HPP
