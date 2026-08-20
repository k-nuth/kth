// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_EXE_EXECUTOR_HPP_
#define KTH_NODE_EXE_EXECUTOR_HPP_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>

#include <kth/database/databases/property_code.hpp>

#include <kth/infrastructure/handlers.hpp>
#include <kth/node/configuration.hpp>
#include <kth/node/full_node.hpp>
#include <kth/node/executor/executor_info.hpp>

#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/executor_work_guard.hpp>

namespace kth::node {

namespace detail {
/// Declared, never defined here. Its definition lives in a header the install
/// rules exclude, so nothing a consumer can reach has a way to use the friendship
/// below. See detail/executor_test_seam.hpp.
struct executor_test_seam;
} // namespace detail

/// Executor - manages the lifecycle of a full node
///
/// The executor owns the io_context and runs it internally in its own thread.
/// This design allows the node to be used as a library from any language
/// (via C API) without the caller needing to manage async execution.
///
/// Usage:
///   executor exec(config);
///   auto ec = exec.start();  // blocks until node is ready
///   // ... use node ...
///   exec.stop();  // blocks until node is stopped
///
/// THE LIFECYCLE RULE
///
/// One rule, because the alternative was several that did not agree.
///
///   Every lifecycle transition — start, stop, release — decides under
///   `lifecycle_mutex_`, publishes the decision there, and moves out the members
///   that decision owns. Every join, every future, every `node->stop()`, every
///   `node->join()` and every caller handler happens OUTSIDE that lock.
///
/// What admits a start and what claims a teardown are monotonic facts of their
/// own — `start_admitted_`, `release_claimed_` — not values of the state enum.
/// Each becomes true once and never goes back. The enum describes the service to
/// an observer and admits nothing:
///
///   * a start is admitted at most once per object, so restart is refused by
///     construction rather than by a value that happens to have come back round.
///     A stop callback cannot re-enable it, and neither can anything else;
///   * `state::stopped` means the SERVICE ended: the node was stopped and
///     joined. It does not mean the resources came back — that is
///     `release_done_`, which stays internal. The wind-down thread publishes it,
///     and that is safe precisely because admission no longer reads it: a value
///     that comes round again is not a licence to start on top of a live thread.
///     Withholding it until a release made kth_node_stopped() a circular wait,
///     since the loop the C API documents is signal_stop, poll stopped(), close.
///
/// ANSWERS ARE OWED, AND OWED ONCE
///
/// An admitted start owes exactly one outcome, and owns it: a start that is
/// refused tells its own caller and writes nothing into the slot the admitted one
/// still owes. Between admission and the coroutine being installed, that debt is
/// held by a guard (see detail/scope_action.hpp) which, on any exit at all —
/// including an exception out of the log setup, the node's construction,
/// `start_io_thread()` or `co_spawn` itself — publishes the outcome once,
/// satisfies the run-completed promise once, and calls the handler outside every
/// lock. Ownership passes to the coroutine only after `co_spawn` has installed
/// it. Without that, a start that threw left a release waiting for an answer
/// nobody would ever give.
///
/// A teardown likewise always publishes that it finished its attempt, and
/// whether it succeeded. A waiter is never left on the condition variable, and
/// never told a failed teardown was a clean one. If a teardown fails and cannot
/// leave the threads unjoinable, it terminates with a diagnosis rather than
/// returning into `~std::thread`.
///
/// THE ONE PRECONDITION
///
/// An executor is stopped and destroyed from outside its own threads, never from
/// inside a handler one of them is running. It owns two, and both invoke caller
/// handlers: `start_async()`'s on the io thread, `stop_async()`'s on the wind-down
/// thread. A thread cannot join itself, and the two ways around that are both
/// worse than refusing: `detach()` would let these members be destroyed under the
/// handler still running on them, and carrying on would leave a joinable thread
/// for `~std::thread` to abort on. So it is detected on both threads and reported
/// by name — see `join_owned_thread()` — and pinned by a control, not only
/// written down here.
///
/// A handler that wants the node down asks for it (`stop_async`,
/// `kth_node_signal_stop`) and destroys from wherever it owns the object.
class executor {
public:
    using start_handler = std::function<void(code)>;
    using stop_handler = std::function<void()>;

    executor(kth::node::configuration const& config, bool stdout_enabled = true);
    ~executor() noexcept;

    executor(executor const&) = delete;
    executor& operator=(executor const&) = delete;

    // -------------------------------------------------------------------------
    // Lifecycle - Async versions (callback-based, for C API and non-blocking use)
    // -------------------------------------------------------------------------

    /// Start the node asynchronously
    /// @param handler Called when node is ready (or failed to start)
    void start_async(start_handler handler);

    /// Stop the node asynchronously
    /// @param handler Called when node is fully stopped (optional)
    void stop_async(stop_handler handler = nullptr);

    // -------------------------------------------------------------------------
    // Lifecycle - Sync versions (blocking, for simple use cases)
    // -------------------------------------------------------------------------

    /// Start the node and block until ready
    /// @return Error code (success if node started successfully)
    [[nodiscard]]
    code start();

    /// Stop the node and block until fully stopped
    void stop();

    /// Wait for stop signal (SIGINT/SIGTERM)
    /// Blocks until signal received
    void wait_for_stop_signal();

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------

    /// Check if node is currently running (started and not stopped)
    [[nodiscard]]
    bool running() const;

    /// Check if node has been started (may still be starting up)
    [[nodiscard]]
    bool started() const;

    /// Check if node is stopped
    [[nodiscard]]
    bool stopped() const;

    // -------------------------------------------------------------------------
    // Node access
    // -------------------------------------------------------------------------

    /// The node this executor is running, or nothing.
    ///
    /// A share, not a reference, and the difference is the whole of it. A
    /// reference into `node_` is not kept alive by anything: a teardown running
    /// on another thread resets that member, and the reference the caller was
    /// handed refers to a destroyed node — a lock around the return would not
    /// change that, because the reference outlives the lock by definition. The
    /// snapshot is taken under `lifecycle_mutex_` and the caller decides how long
    /// to hold it.
    ///
    /// Empty before a start and after a release, where dereferencing `node_` was
    /// undefined and had no caller checking for it.
    [[nodiscard]]
    kth::node::full_node::ptr node() const;

    // -------------------------------------------------------------------------
    // Initialization helpers
    // -------------------------------------------------------------------------

#if ! defined(KTH_DB_READONLY)
    bool do_initchain(std::string_view extra);
    bool init_directory(std::error_code& ec);
    std::error_code init_directory_if_necessary();
#endif

    bool verify_directory();
    void print_version(std::string_view extra);
    void initialize_output(std::string_view extra, kth::database::db_mode_type db_mode);

private:
    /// The only thing that may replace how the node is built, and it is not
    /// installed.
    ///
    /// A seam exists at all because the guarantee that matters most here — that a
    /// start which throws before reaching its coroutine still answers, and that
    /// one handed nothing back fails normally — cannot be tested unless node
    /// construction can be made to fail. It is private, and reachable only
    /// through a type whose definition ships with the tests, so it adds nothing a
    /// consumer can call.
    friend struct detail::executor_test_seam;

    /// How the node is built. Never empty.
    using node_factory = std::function<kth::node::full_node::ptr(
        kth::node::configuration const&)>;

    void print_ascii_art();
    void start_io_thread();
    void stop_io_thread();

    /// Whether this caller's start was admitted, and if not, why not.
    ///
    /// An answer per call. A refused start reports this to its own caller and
    /// touches nothing the admitted start owes.
    enum class admission { admitted, already_started, releasing };

    /// How a teardown ended. A waiter is told which; it is never told that a
    /// teardown which failed was a clean one.
    enum class teardown { succeeded, failed };

    /// What a refused caller is told. Two refusals, two answers: `service_stopped`
    /// when a teardown owns the object, `operation_failed` when a start already
    /// did. Reporting one code for both threw away the only part a caller could
    /// act on.
    [[nodiscard]] static code refusal_code(admission refusal);

    /// Decide, once, whether this caller owns the start.
    [[nodiscard]] admission admit_start();

    /// Everything an admitted start does. Only ever reached by the caller that
    /// was admitted.
    void begin_admitted_start(start_handler handler);

    /// The one teardown. stop() is this, and so is the destructor.
    ///
    /// Claimed once: a second caller waits for the first rather than repeating
    /// it, and one arriving afterwards returns at once with the first's result.
    /// noexcept, because a destructor calls it.
    teardown release() noexcept;

    /// Record how a start ended — once, for everyone waiting on it.
    ///
    /// Returns whether THIS call is the one that recorded it. That answer is what
    /// decides who calls the handler: an admitted start owes one call, and the
    /// coroutine and the completion handler that follows it can both arrive at
    /// the question. Answering it twice hands a caller `success` and then
    /// `operation_failed` for the same start.
    [[nodiscard]] bool publish_start_outcome(code ec) noexcept;

    /// Call a start handler. Same reasons as invoke_stop_handler(): it is caller
    /// code on a thread of ours, and an exception out of it here becomes the
    /// exception_ptr that makes the completion handler call it a second time.
    static void invoke_start_handler(start_handler const& handler, code ec) noexcept;

    /// Satisfy the run-completed promise — once. Both the start guard and the
    /// coroutine can be the one that gets there, and the second `set_value`
    /// throws.
    void satisfy_run_completed() noexcept;

    /// Wait for the start to have ended, whichever way it ended.
    void await_start_outcome();

    /// Publish the outcome, satisfy the promise, then call the handler — in that
    /// order, outside every lock. This is what the start guard runs.
    void answer_failed_start(code ec, start_handler const& handler) noexcept;

    /// Everything a stop request does, on the thread that owns it.
    ///
    /// One body for both states a stop can be asked from. A request made while a
    /// start is still in flight used to be answered on the spot and do nothing —
    /// the caller was told the node had stopped while the start went on to
    /// publish `running`. It waits for the start to say how it went, stops what
    /// the start left, and only then completes the handler. Exactly once, on
    /// either outcome.
    void wind_down_node(stop_handler const& handler) noexcept;

    /// Move the state to `stopping`, from whichever live state it is in.
    void mark_stopping() noexcept;

    /// Call a stop handler, wherever it is being called from.
    ///
    /// One helper for both places a stop handler runs — the wind-down thread and
    /// the caller's own thread — because an exception out of either is
    /// unrecoverable in a different way and neither was caught. Out of the
    /// wind-down thread it leaves the thread function and terminates the process;
    /// out of the caller's it crosses `kth_node_signal_stop`, which is
    /// `extern "C"`. Reported here instead, the way a start handler already was.
    static void invoke_stop_handler(stop_handler const& handler) noexcept;

    // Configuration
    bool stdout_enabled_;
    kth::node::configuration config_;

    // Node instance
    kth::node::full_node::ptr node_;

    // IO context runs in its own thread
    ::asio::io_context io_context_;
    using work_guard_type = ::asio::executor_work_guard<::asio::io_context::executor_type>;
    std::optional<work_guard_type> work_guard_;

    /// What the heartbeat waits on, held here so a stop can reach it.
    ///
    /// It used to be a local of the heartbeat coroutine, which is why stopping
    /// this node could take ten seconds with nothing left to do: the wait was
    /// only re-evaluated when the timer expired on its own, and the teardown
    /// waits for that coroutine — release() blocks on run_completed_future_,
    /// which is satisfied only after `node_->run() && heartbeat()` completes.
    /// io_context_.stop(), which would end it, runs after that wait.
    ///
    /// Declared AFTER io_context_ so it is destroyed before it, and touched only
    /// from the io thread: wake_heartbeat() posts, it does not cancel in place.
    ::asio::steady_timer heartbeat_timer_{io_context_};

    std::thread io_thread_;

    /// Ask the heartbeat to look again, now.
    ///
    /// Posted rather than cancelled in place: the wait runs on the io thread and
    /// the stop does not, and a timer is not safe to cancel from another thread
    /// while it is being waited on.
    ///
    /// A cancellation that arrives before the timer is armed is not lost. Every
    /// caller has already asked the node to stop, so the loop's own condition —
    /// re-read before it arms — sees `node_->stopped()` and ends. And one that
    /// arrives after the timer has already expired changes nothing: asio runs
    /// that handler exactly once with the result it already had, and the loop
    /// re-reads the same condition at the top.
    void wake_heartbeat() noexcept;

    /// The wind-down stop_async() hands off, so it can return without blocking the
    /// io thread it may itself be running on.
    ///
    /// Owned rather than detached. It captures this object and outlives the call
    /// that spawned it, so a stop nobody waits for — stop_async() with no handler,
    /// which is how the node's own fatal path and kth_node_signal_stop() ask for
    /// one — could otherwise still be inside node_->join() while this object is
    /// being destroyed. release() moves it out under the lock and joins it.
    std::thread cleanup_thread_;

    node_factory make_node_;

    /// Where the lifecycle can be watched, and made to fail, from a test.
    ///
    /// Named, not spelled: a mistyped string point is a probe that silently never
    /// fires, and a control that never fires is a control that passes for the
    /// wrong reason. A mistyped enumerator does not compile.
    enum class lifecycle_probe_point {
        before_node_stop,
        before_wind_down,
        // Fires once the wind-down has really finished: node stopped, run()
        // waited on, node joined. It is what a failure "after the cleanup was
        // done" looks like, and the only way to check that such a failure does
        // not get the wind-down classified as failed and redone.
        after_wind_down,
        before_wind_down_thread,
        after_node_cleanup,
        /// The heartbeat is about to wait. A control that needs the wait to be
        /// in progress observes this rather than assuming it: under a sanitizer
        /// the stop can arrive first, and then the loop ends on its own
        /// condition — correct, but not what such a control set out to measure.
        heartbeat_armed,
        /// The heartbeat completed a real wait and is about to report. A tick.
        heartbeat_beat,
        /// The heartbeat's wait was ended by a stop. NOT a tick: nothing is left
        /// alive to report on, and counting it would say the io_context was
        /// serving timers at the moment it was being torn down.
        heartbeat_woken
    };

    /// Called at the points above. Empty in every build but a test's.
    ///
    /// One member for all four. Each property they exist for is invisible from
    /// outside — p2p_node::stop() logs only when it fails, a std::thread that
    /// cannot be created is not something a caller can arrange, and a teardown
    /// that fails once it owns the object has no step a caller can reach — and
    /// one callable covers them all: a test's lambda counts, or throws, or both,
    /// and keeps no permanent counting state of its own. Installed only through
    /// detail/executor_test_seam.hpp, which is not installed.
    std::function<void(lifecycle_probe_point point)> lifecycle_probe_;

    /// Run the probe, if a test installed one. May throw: that is the point.
    void probe(lifecycle_probe_point point) const;

    /// What a wind-down thread left behind, published under lifecycle_mutex_.
    ///
    /// `release()` skips the stop and the join when a wind-down already did
    /// them, and it may only do that when one actually did. A wind-down that
    /// caught an exception did not, and a release that took its existence for
    /// completion would destroy a full_node that was never joined.
    ///
    /// Deliberately not `state::stopped`: that describes the service, not who
    /// owns the cleanup or whether it finished.
    enum class wind_down_outcome { none, completed, failed };
    wind_down_outcome wind_down_outcome_{wind_down_outcome::none};

    /// Guards the lifecycle decision and the handoff of what that decision owns.
    ///
    /// Never held across a join, a future, a condition variable, a node call or a
    /// caller handler — so a thread this object owns can always reach it. The
    /// node's fatal handler calls stop_async() from the io thread, and release()
    /// joins that same thread.
    mutable std::mutex lifecycle_mutex_;
    std::condition_variable lifecycle_cv_;

    /// Monotonic. True once, never false again, and what admission and teardown
    /// actually consult — the state enum consults nothing.
    bool start_admitted_{false};
    bool release_claimed_{false};
    bool release_done_{false};
    teardown release_result_{teardown::failed};

    // State tracking. Answers questions about the service; never about which
    // resources this object still owns.
    enum class state { stopped, starting, running, stopping };
    std::atomic<state> state_{state::stopped};

    // Synchronization for sync versions
    std::mutex start_mutex_;
    std::condition_variable start_cv_;
    code start_result_{error::success};
    bool start_finished_{false};

    // Tracks when run() coroutine completes (for safe shutdown).
    //
    // Built by the constructor, not by the admission. Constructing a promise
    // allocates and get_future() can throw, and doing either after
    // `start_admitted_ = true` puts a throwing step between the debt and the
    // guard that pays it: the exception leaves admit_start(), no outcome is ever
    // published, and a release waits for one forever. Here a throw means no
    // executor was constructed, so nothing is owed. The object is single-use, so
    // one pair is all it will ever need.
    std::promise<void> run_completed_promise_;
    std::future<void> run_completed_future_;
    std::atomic<bool> run_completed_satisfied_{false};
};

} // namespace kth::node

#endif /*KTH_NODE_EXE_EXECUTOR_HPP_*/
