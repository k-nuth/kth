// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/executor/executor.hpp>

#include <kth/node/detail/scope_action.hpp>

#include <csignal>
#include <exception>
#include <functional>
#include <type_traits>
#include <future>
#include <memory>
#include <mutex>
#include <print>
#include <thread>

#include <boost/core/null_deleter.hpp>

#include <kth/blockchain/utxo_builder.hpp>
#include <kth/domain/multi_crypto_support.hpp>
#include <kth/node.hpp>
#include <kth/node/parser.hpp>
#include <kth/domain/version.hpp>

#include <crypto/sha256.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/post.hpp>
#include <asio/signal_set.hpp>
#include <asio/steady_timer.hpp>
#include <asio/experimental/awaitable_operators.hpp>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace kth::node {

using namespace boost;
using namespace boost::system;
using namespace kd::chain;
using namespace kd::config;
using namespace kth::database;
using namespace ::asio::experimental::awaitable_operators;

#if ! defined(__EMSCRIPTEN__)
using namespace kth::network;
#endif

using namespace std::placeholders;
using boost::null_deleter;
using std::error_code;
using kth::database::data_base;
using std::placeholders::_1;

static constexpr int directory_exists = 0;
static constexpr int directory_not_found = 2;
static auto const mode = std::ofstream::out | std::ofstream::app;

// =============================================================================
// Construction / Destruction
// =============================================================================

executor::executor(kth::node::configuration const& config, bool stdout_enabled)
    : stdout_enabled_(stdout_enabled)
    , config_(config)
    , make_node_([](kth::node::configuration const& cfg) {
        return std::make_shared<kth::node::full_node>(cfg);
    })
    , run_completed_future_(run_completed_promise_.get_future())
{
#if ! defined(__EMSCRIPTEN__)
    auto const& network = config_.network;
    kth::log::initialize(network.debug_file.string(), network.error_file.string(), stdout_enabled, network.verbose);
#endif // ! defined(__EMSCRIPTEN__)
}

executor::~executor() noexcept {
    // One call, because there is one teardown. The old destructor asked whether
    // the service had reached `running` and used the answer to decide whether
    // this object still owned a thread; a start that failed owns one and never
    // reached `running`, so ~std::thread ran on a joinable thread and the process
    // aborted on a failure start() had already reported (#669).
    release();
}

// =============================================================================
// IO Thread Management
// =============================================================================

void executor::start_io_thread() {
    // Create work guard to keep io_context alive even when there's no work
    work_guard_.emplace(io_context_.get_executor());

    // Start io_context in background thread
    io_thread_ = std::thread([this]() {
        spdlog::info("[executor:io_thread] io_context_.run() starting (thread_id={})",
            std::hash<std::thread::id>{}(std::this_thread::get_id()));
        try {
            io_context_.run();
            // 2026-02-07: If we reach here without explicit stop(), something is wrong
            spdlog::warn("[executor:io_thread] io_context_.run() RETURNED - state={}, stopped={}",
                static_cast<int>(state_.load()), io_context_.stopped());
        } catch (std::exception const& e) {
            spdlog::error("[executor:io_thread] EXCEPTION in io_context_.run(): {}", e.what());
        } catch (...) {
            spdlog::error("[executor:io_thread] UNKNOWN EXCEPTION in io_context_.run()");
        }
        spdlog::warn("[executor:io_thread] Thread exiting!");
    });
}

namespace {

/// Join a thread this object owns, or say why it cannot be joined.
///
/// A thread cannot join itself, and both ways around that are worse than saying
/// so. detach() would let this object's members be destroyed under the handler
/// still running on them. Carrying on would leave a joinable thread for
/// ~std::thread to abort on — the very failure this file exists to remove,
/// re-entering by another door and reported as something other than what it is.
///
/// So it is a precondition rather than a case to handle, stated on the class:
/// an executor is stopped and destroyed by its owner, never from inside a
/// callback one of its own threads is running. Both of its threads can invoke
/// caller-supplied handlers — start_async()'s on the io thread, stop_async()'s
/// on the wind-down thread — so neither is theoretical, and naming which one it
/// was is the whole of what can be done about it from here.
void join_owned_thread(std::thread& thread, char const* which) {
    if ( ! thread.joinable()) {
        return;
    }
    if (thread.get_id() == std::this_thread::get_id()) {
        spdlog::critical("[executor] The {} cannot join itself. This executor was stopped or "
            "destroyed from inside a callback running on that thread, which its contract does "
            "not allow; there is no correct way to continue.", which);
        // Flushed, because terminate() follows immediately and spdlog flushes on
        // a configured level only — an unflushed sink loses the one line that
        // says what happened, which is the entire value of not just aborting.
        spdlog::default_logger()->flush();
        std::terminate();
    }
    thread.join();
}

} // namespace

void executor::stop_io_thread() {
    spdlog::debug("[executor] stop_io_thread() - releasing work guard...");
    // First, and the join below is why. run() does not return while this is held,
    // and after a start that failed the guard is the ONLY thing still holding it:
    // the startup coroutine has returned, the context has no work left, and the
    // thread sits in run() forever. Joining without dropping this does not abort —
    // it hangs, which is the other way to fail #669.
    work_guard_.reset();

    spdlog::debug("[executor] stop_io_thread() - stopping io_context...");
    // And this, so a context with handlers still queued returns too, rather than
    // draining them on the way out.
    io_context_.stop();

    spdlog::debug("[executor] stop_io_thread() - joining io thread...");
    join_owned_thread(io_thread_, "io thread");
    spdlog::debug("[executor] stop_io_thread() - done");
}

bool executor::publish_start_outcome(code ec) noexcept {
    {
        std::lock_guard<std::mutex> lock(start_mutex_);
        if (start_finished_) {
            return false;       // an admitted start owes exactly one answer
        }
        start_result_ = ec;
        start_finished_ = true;
    }
    start_cv_.notify_all();
    return true;
}

void executor::invoke_start_handler(start_handler const& handler, code ec) noexcept {
    if ( ! handler) {
        return;
    }
    try {
        handler(ec);
    } catch (std::exception const& e) {
        spdlog::error("[executor] A start handler threw: {}", e.what());
    } catch (...) {
        spdlog::error("[executor] A start handler threw");
    }
}

void executor::satisfy_run_completed() noexcept {
    auto expected = false;
    if ( ! run_completed_satisfied_.compare_exchange_strong(expected, true)) {
        return;                 // a second set_value throws
    }
    try {
        run_completed_promise_.set_value();
    } catch (std::exception const& e) {
        spdlog::error("[executor] Could not signal run completion: {}", e.what());
    } catch (...) {
        spdlog::error("[executor] Could not signal run completion");
    }
}

void executor::await_start_outcome() {
    std::unique_lock<std::mutex> lock(start_mutex_);
    start_cv_.wait(lock, [this]() { return start_finished_; });
}

void executor::answer_failed_start(code ec, start_handler const& handler) noexcept {
    // Order, and all of it outside every lock.
    //
    // The outcome first, because it is what a release() waiting on this start is
    // waiting for. The promise second, because that release waits on it next.
    // The handler last, because it is caller code and may do anything — including
    // asking this object to stop, which needs both of the above already true.
    auto const ours = publish_start_outcome(ec);
    satisfy_run_completed();
    if (ours) {
        invoke_start_handler(handler, ec);
    }
}

void executor::wake_heartbeat() noexcept {
    // Posted, never cancelled in place: the wait runs on the io thread and every
    // caller of this is on another one. Cancelling a timer from a second thread
    // while it is being waited on is not safe, and measurably does not work —
    // the control for this reports that the wake simply never arrives.
    //
    // @par Why the handler cannot outlive this object
    // The only thread that can run it is the io thread, and release() joins that
    // thread in stop_io_thread() before returning; the destructor calls
    // release(). So the object is alive for as long as the handler can run.
    // heartbeat_timer_ is declared after io_context_ and before io_thread_, so it
    // is destroyed before the context it refers to.
    //
    // @par Why a stopped context is safe rather than merely likely
    // io_context::stop() does not discard queued work: it makes run() return, and
    // that work could still execute after a restart() and another run(). What
    // makes this safe is that this executor is SINGLE-USE — it never restarts
    // that context, and it destroys it after joining the thread — so a handler
    // that was still queued when the context stopped is destroyed with it and
    // never runs.
    //
    // `noexcept` because both callers are teardown paths that must not throw, and
    // one is reached from kth_node_signal_stop(), which is `extern "C"`. A post
    // that cannot be queued is not a reason to abandon a shutdown: the
    // heartbeat's own condition still ends it at the next expiry, which is the
    // behaviour this change improves on rather than depends on.
    try {
        ::asio::post(io_context_, [this] {
            // On the io thread now, so this cannot race the wait it cancels.
            // Cancelling a timer that is not armed does nothing, and it does not
            // need to: every caller has already asked the node to stop, so the
            // loop re-reads `node_->stopped()` before arming and ends there.
            //
            // Guarded because cancel() throws on failure — it calls
            // asio::detail::throw_error() — and an exception out of a handler
            // leaves io_context::run(), which is the io thread's whole body. That
            // would take the thread down in the middle of a shutdown, and the
            // teardown is waiting on work that runs there.
            try {
                heartbeat_timer_.cancel();
            } catch (...) {
                try {
                    spdlog::debug("[executor] The heartbeat timer could not be cancelled; "
                        "it ends on its own");
                } catch (...) {
                }
            }
        });
    } catch (...) {
        // Reporting is itself fallible — a logger can throw, and this function
        // promises not to — so the diagnostic gets its own guard rather than
        // being the thing that breaks the promise.
        try {
            spdlog::debug("[executor] The heartbeat could not be woken; it ends on its own");
        } catch (...) {
        }
    }
}

void executor::probe(lifecycle_probe_point point) const {
    if (lifecycle_probe_) {
        lifecycle_probe_(point);
    }
}

void executor::invoke_stop_handler(stop_handler const& handler) noexcept {
    if ( ! handler) {
        return;
    }
    try {
        handler();
    } catch (std::exception const& e) {
        spdlog::error("[executor] A stop handler threw: {}", e.what());
    } catch (...) {
        spdlog::error("[executor] A stop handler threw");
    }
}

executor::teardown executor::release() noexcept {
    // Declared before the try and before every local below, so its destructor
    // runs LAST: after the try, after the catch, after every best-effort
    // statement, and after the node share and the thread object are gone. It was
    // constructed inside the try, where an exception ran its destructor during
    // the unwinding — publishing `release_done_` while this call still had the
    // catch and the whole best-effort cleanup ahead of it. A waiter woken there
    // returns, and its caller may destroy this object while this call is still
    // touching work_guard_, io_context_ and the two threads.
    //
    // A waiter that sees `release_done_` is seeing a call that will not touch
    // this object again.
    auto result = teardown::failed;
    detail::scope_action publish_completion([this, &result]() {
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex_);
            release_result_ = result;
            release_done_ = true;
        }
        lifecycle_cv_.notify_all();
    }, false);

    std::thread wind_down;
    kth::node::full_node::ptr node;

    try {
        bool admitted = false;
        {
            std::unique_lock<std::mutex> lock(lifecycle_mutex_);
            if (release_done_) {
                return release_result_;
            }
            if (release_claimed_) {
                // Somebody else owns this. stop() is documented to block until the
                // node is stopped, so waiting is the answer — and what comes back
                // is their result, not a clean finish this call did not witness.
                lifecycle_cv_.wait(lock, [this]() { return release_done_; });
                return release_result_;
            }
            release_claimed_ = true;
            // Armed only once this call owns the teardown: a waiter and a caller
            // arriving after it finished both leave above, and neither publishes.
            publish_completion.arm();
            admitted = start_admitted_;
            // NOT node_ and NOT cleanup_thread_. Taking them here takes whatever
            // exists at this instant, and at this instant an admitted start may
            // not have built the node yet: the copy is null, the stop below is
            // skipped, and the wait below never ends because nobody ever asked
            // the node to stop. They are taken after the wait, further down.
        }

        // -- an in-flight start finishes before anything is taken from it ---
        //
        // Until the start has published an outcome the startup coroutine is still
        // inside node_->start(), holding the node this is about to release.
        // Waiting is what makes the rest safe: releasing underneath it is a
        // use-after-free, and asking the node to stop underneath it does not even
        // work — p2p_node::start() clears its own stopped flag on entry, so the
        // stop is lost and run() goes on to run a node nobody wanted.
        if (admitted) {
            await_start_outcome();
        }

        // Only now, and under the lock, because only now is there anything
        // settled to take. The start has finished either building the node or
        // failing to, and either way what it left is what this teardown owns.
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex_);
            node = node_;                             // alive across the unlock
            wind_down = std::move(cleanup_thread_);   // nobody else joins it now
        }

        // `stopping` only from a state that was still going. A wind-down that a
        // stop request handed off has already published `stopped`, and walking
        // that back would show an observer true, then false, then true.
        mark_stopping();

        // One owner runs the wind-down, and only one.
        //
        // This used to call node->stop() first and THEN join a wind-down thread
        // that calls it too. p2p_node::stop() reads its flag and stores it
        // separately rather than exchanging it, so two threads both see `false`
        // and both go on to cancel the channels, stop_all() the manager and
        // save() the peer database — concurrently, over the same file.
        // Whether this call owns the cleanup, and it is not decided by whether a
        // thread exists. A wind-down that caught an exception did not stop the
        // node and did not join it, and taking its existence for completion is
        // how a full_node gets destroyed without ever having been joined.
        auto owns_cleanup = true;
        if (wind_down.joinable()) {
            join_owned_thread(wind_down, "wind-down thread");

            wind_down_outcome finished{};
            {
                std::lock_guard<std::mutex> lock(lifecycle_mutex_);
                finished = wind_down_outcome_;
            }
            owns_cleanup = (finished != wind_down_outcome::completed);
            if (owns_cleanup) {
                spdlog::error("[executor] The wind-down did not finish; this teardown is "
                    "completing what it left. The node has been joined by nobody so far.");
            }
        }

        if (owns_cleanup) {
            // Nobody else finished it, so this is the owner. The thread above is
            // joined by now, so nothing else can be inside these at the same
            // time — which is the property that matters, not the count.
            if (node) {
                probe(lifecycle_probe_point::before_node_stop);
                node->stop();
                // After the stop, so a wake that arrives before the timer is
                // armed is covered by the loop's own condition.
                wake_heartbeat();
            }
            // Gated on the start, not on the future being valid. The future is
            // built by the constructor now, so valid() is true from the moment
            // the object exists — and waiting on it with no start behind it is
            // waiting for a promise nobody will ever fulfil.
            if (admitted) {
                run_completed_future_.wait();
            }
            if (node) {
                node->join();
                spdlog::info("[node] Node stopped successfully.");
                spdlog::info("[node] Good bye!");
            }
        }

        // The node's components post to the context released below, so the node
        // goes first — and only now, with nothing left running that holds it.
        //
        // Moved out under the lock and dropped outside it. Resetting the member
        // in place runs ~full_node under lifecycle_mutex_ whenever no other share
        // exists, and node work does not run under that lock: it is unbounded,
        // and anything it called back into would find the lock already held.
        kth::node::full_node::ptr last;
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex_);
            last = std::move(node_);
        }
        node.reset();
        last.reset();

        // Empty everywhere but a control, and placed HERE on purpose: the node is
        // already stopped, joined and gone, so what a fault leaves unfinished is
        // only what the best-effort cleanup below redoes. Thrown any earlier it
        // would abandon a running node, which is a different broken teardown and
        // not the one worth pinning. See detail/executor_test_seam.hpp.
        probe(lifecycle_probe_point::after_node_cleanup);

        // Guard, context, join.
        stop_io_thread();

        // Published only now: `stopped` means the cycle's resources are gone, and
        // for as long as it meant anything less it was a value an observer could
        // not use and admission must never have read.
        state_ = state::stopped;

        result = teardown::succeeded;
        return result;      // published by the guard, on the way out
    } catch (std::exception const& e) {
        spdlog::error("[executor] The teardown failed: {}", e.what());
    } catch (...) {
        spdlog::error("[executor] The teardown failed for an unknown reason");
    }

    // Best effort, because the alternative is returning into ~std::thread. The
    // waiters have NOT been told yet: the guard runs after all of this.
    try {
        work_guard_.reset();
        io_context_.stop();
    } catch (...) {
    }
    for (auto* thread : {&wind_down, &cleanup_thread_, &io_thread_}) {
        if (thread->joinable() && thread->get_id() != std::this_thread::get_id()) {
            try {
                thread->join();
            } catch (...) {
            }
        }
    }
    if (wind_down.joinable() || cleanup_thread_.joinable() || io_thread_.joinable()) {
        // Returning would hand a joinable thread to ~std::thread, which is the
        // unexplained abort this whole change exists to remove. Ending here says
        // why instead.
        spdlog::critical("[executor] The teardown failed and could not give back every thread "
            "it owns. Returning would abort in ~std::thread with nothing said, so this ends "
            "here: a thread of this executor is still running and cannot be joined.");
        spdlog::default_logger()->flush();
        std::terminate();
    }
    return teardown::failed;
}

// =============================================================================
// Async Lifecycle
// =============================================================================

code executor::refusal_code(admission refusal) {
    // Two refusals, two answers. "Already started" and "being stopped" are
    // different things for a caller to have been told, and reporting one code for
    // both threw away the only part it could act on.
    switch (refusal) {
        case admission::releasing:
            return error::service_stopped;
        case admission::already_started:
            return error::operation_failed;
        case admission::admitted:
            break;
    }
    return error::success;
}

executor::admission executor::admit_start() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);

    // Monotonic, both of them. Restart is refused because a start was admitted
    // once, not because a value happens to be the wrong one at this instant — and
    // `state::stopped` comes back round, which is exactly how a value came to
    // admit a start on top of a live thread.
    //
    // The teardown is asked about first because it is the dominant fact: an
    // object being given back is going away, and telling a caller "already
    // started" when what is true is "being stopped" hands them the one answer
    // they cannot act on.
    if (release_claimed_) {
        return admission::releasing;
    }
    if (start_admitted_) {
        return admission::already_started;
    }
    // Everything from here to the return must be incapable of throwing, and the
    // compiler is asked rather than trusted. The promise and its future used to
    // be built here, and neither can be built without the possibility of a throw:
    // a `std::promise` allocates its shared state and `get_future()` reports a
    // second retrieval by throwing. Either one leaving through here takes the
    // exception out of admit_start(), past the caller, with the debt below
    // already published and no guard yet in place to pay it — and a release then
    // waits for an outcome that will never come. They are built by the
    // constructor now, where a throw means there is no executor and nothing owed.
    static_assert(noexcept(start_admitted_ = true));
    static_assert(noexcept(state_ = state::starting));

    start_admitted_ = true;
    state_ = state::starting;
    return admission::admitted;
}

void executor::start_async(start_handler handler) {
    auto const refusal = admit_start();
    if (refusal != admission::admitted) {
        // Only this caller is told. Writing into start_result_ here is how a
        // refused start came to answer for the admitted one — waking a release()
        // that was waiting for the real outcome, and handing a caller of start()
        // somebody else's refusal (CR #5).
        // Through the helper, like every other handler call. This one is caller
        // code on the caller's own thread, and start_async() is what
        // kth_node_init_run() calls: an exception out of it here crosses
        // `extern "C"`.
        invoke_start_handler(handler, refusal_code(refusal));
        return;
    }
    begin_admitted_start(std::move(handler));
}

void executor::begin_admitted_start(start_handler handler) {
    // Armed here, disarmed only once co_spawn has installed the coroutine that
    // takes the debt over. Everything between can throw — the log setup, the
    // node's construction, start_io_thread(), co_spawn itself — and every one of
    // those exits through here, leaving a start that answered rather than a
    // release waiting forever for one (CR #3).
    code failure = error::operation_failed;
    // The FIRST statement of the only function an admitted start reaches, and its
    // construction cannot throw — the callable is stored by value and the handler
    // arrived here by a move that cannot throw either. So there is no step
    // between owing an answer and being able to give one.
    static_assert(std::is_nothrow_move_constructible_v<start_handler>,
        "the handler must reach this function without a throw, or the guard below "
        "is armed one step too late");
    detail::scope_action answer_on_any_exit([this, &failure, &handler]() {
        answer_failed_start(failure, handler);
    }, true);

    try {

    // Initialize output and directory
    initialize_output("", config_.database.db_mode);

    spdlog::info("[node] Press CTRL-C to stop the node.");
    spdlog::info("[node] Please wait while the node is starting...");

    // Probe the system CSPRNG before anything can need it. Whether it works is
    // a constant for the life of the process, so this is the one point where an
    // unusable one is both detectable and actionable: past here, every draw is
    // infallible and no caller carries an error path for it.
    if (auto const ec = pseudo_random::check_available()) {
        spdlog::error("[node] System CSPRNG is unavailable: '{}'. "
            "The node cannot generate keys, nonces or salts without it.", ec.message());
        failure = ec;
        return;             // the guard answers, with this code
    }

#if ! defined(KTH_DB_READONLY)
    if (auto const ec = init_directory_if_necessary(); ec != error::success) {
        auto const& directory = config_.database.directory;
        spdlog::error("[node] Failed to create directory {} with error, '{}'.", directory.string(), ec.message());
        failure = ec;
        return;             // as above (#673)
    }
#endif

    // Create the node
    auto node = make_node_(config_);
    if ( ! node) {
        // A factory is allowed to fail; it is not allowed to hand back nothing
        // and let the next line find out. This is an ordinary start failure and
        // leaves by the ordinary path.
        spdlog::error("[node] The node could not be created.");
        failure = error::operation_failed;
        return;
    }

    // The node reports conditions it cannot go on from (see full_node::notify_fatal)
    // from inside its own tasks. It stops itself; ending the process is this
    // executor's, and it is the same wind-down a stop request takes — the node
    // has no second, partial copy of it.
    node->set_fatal_handler([this](std::string const& reason) {
        spdlog::critical("[node] Shutting down: {}", reason);
        stop_async({});
    });

    // Published under the lock, because a teardown reads it under the same one.
    // Without this the read is unsynchronised with the write no matter which lock
    // the reader holds, and the two race.
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        node_ = std::move(node);
    }

    // Start IO thread
    start_io_thread();

    // Spawn the startup coroutine
    ::asio::co_spawn(io_context_, [this, handler]() -> ::asio::awaitable<void> {
        // Start the node
        auto start_ec = co_await node_->start();
        if (start_ec != error::success) {
            spdlog::error("[node] Node failed to start with error: {}.", start_ec.message());
            // The promise first, then the outcome, then the handler: a release()
            // woken by the outcome waits on the promise next, and the handler is
            // caller code that may ask this object to stop.
            satisfy_run_completed();
            if (publish_start_outcome(start_ec)) {
                invoke_start_handler(handler, start_ec);
            }
            co_return;
        }

        spdlog::info("[node] Seeding is complete.");

        // Mark as running BEFORE calling run() so start() can return
        // run() blocks until the node is stopped, so we must notify first
        state_ = state::running;

        // And the outcome before the handler, so a release() racing this start is
        // already past its wait by the time caller code runs. The handler is
        // called by whoever recorded the outcome, and only then: everything below
        // this point can throw, and an exception here becomes the exception_ptr
        // the completion handler reads — which would otherwise call this same
        // handler a second time, with `operation_failed` after a `success`.
        auto const ours = publish_start_outcome(error::success);

        // Notify handler
        if (ours) {
            invoke_start_handler(handler, error::success);
        }

        spdlog::info("[node] Node is started.");

        // 2026-02-07: Diagnostic heartbeat to verify io_context is still processing
        // This helps identify if the io_context stops processing timers
        // Returns code to match node_->run() return type for && operator
        auto heartbeat = [this]() -> ::asio::awaitable<code> {
            uint64_t heartbeat_count = 0;
            // Also stops when the node stopped on its own: a fatal condition
            // reported from inside it ends run(), and a heartbeat that only
            // watched this executor's state would keep the `&&` below from ever
            // completing — no join, no shutdown, a process with nothing to do.
            //
            // The condition is re-read before each wait AND the wait can be
            // ended from outside, which are two different things and both are
            // needed. Re-reading alone is what made stopping this node take up
            // to ten seconds: the teardown waits for this coroutine, and this
            // coroutine was waiting on a timer nobody could reach.
            while (state_.load() == state::running && node_ && ! node_->stopped()) {
                heartbeat_timer_.expires_after(std::chrono::seconds(10));
                probe(lifecycle_probe_point::heartbeat_armed);
                auto [ec] = co_await heartbeat_timer_.async_wait(
                    ::asio::as_tuple(::asio::use_awaitable));

                if (ec == ::asio::error::operation_aborted) {
                    // A stop asked for this, and it is the ONLY reason this wait
                    // ends early. Not a tick: nothing is alive to report on, and
                    // counting it would say the io_context was serving timers at
                    // a moment when it was being torn down.
                    probe(lifecycle_probe_point::heartbeat_woken);
                    spdlog::debug("[executor:heartbeat] Woken by a stop; ending");
                    break;
                }
                if (ec) {
                    spdlog::debug("[executor:heartbeat] Timer failed: {}", ec.message());
                    break;
                }

                ++heartbeat_count;
                probe(lifecycle_probe_point::heartbeat_beat);
                // 2026-02-07: Log more info to help diagnose io_context issues
                spdlog::debug("[executor:heartbeat] io_context alive, beat #{}, io_stopped={}, state={}",
                    heartbeat_count, io_context_.stopped(), static_cast<int>(state_.load()));
            }
            spdlog::debug("[executor:heartbeat] Exiting (state={})", static_cast<int>(state_.load()));
            co_return error::success;
        };

        // Run the node (starts P2P, sync, etc.) AND the diagnostic heartbeat
        // This blocks until the node is stopped (via stop())
        auto [run_ec, heartbeat_ec] = co_await (node_->run() && heartbeat());
        (void)heartbeat_ec;  // Unused, just for diagnostics
        if (run_ec != error::success && run_ec != error::service_stopped) {
            spdlog::error("[node] Node run ended with error: {}.", run_ec.message());
        }

        spdlog::debug("[node] Node run() completed, signaling run_completed_promise.");

        // Signal that run() has completed - stop() waits for this before destroying the node
        satisfy_run_completed();

    }, [this, handler](std::exception_ptr ep) {
        if (ep) {
            try {
                std::rethrow_exception(ep);
            } catch (std::exception const& e) {
                spdlog::error("[node] Startup exception: {}", e.what());
            } catch (...) {
                spdlog::error("[node] Startup exception");
            }
            // Same order and the same reasons as the failure branch above, and
            // the handler only if this is the call that recorded the outcome. The
            // coroutine may have answered already and then thrown — a handler
            // that throws is exactly how it gets here — and the start owes one
            // answer, not one per exception on the way out.
            satisfy_run_completed();
            if (publish_start_outcome(error::operation_failed)) {
                invoke_start_handler(handler, error::operation_failed);
            }
        }
    });

    // Installed. The debt is the coroutine's now.
    answer_on_any_exit.disarm();

    } catch (std::exception const& e) {
        // Reported, not propagated. start() returns a code and start_async()
        // takes a handler, and the callers of both include an `extern "C"`
        // boundary that an exception must not cross. The guard above turns this
        // into the answer that start was owed; logging first is what puts the
        // reason ahead of the handler that acts on it.
        spdlog::error("[node] The node could not be started: {}.", e.what());
        answer_on_any_exit.run();
    } catch (...) {
        spdlog::error("[node] The node could not be started.");
        answer_on_any_exit.run();
    }
}

void executor::mark_stopping() noexcept {
    // From whichever live state it is in, and from no other. A wind-down that a
    // stop request handed off publishes `stopped` when it is done, and walking
    // that back would show an observer true, then false, then true.
    auto running = state::running;
    if (state_.compare_exchange_strong(running, state::stopping)) {
        return;
    }
    auto starting = state::starting;
    state_.compare_exchange_strong(starting, state::stopping);
}

void executor::wind_down_node(stop_handler const& handler) noexcept {
    auto wound_down = false;
    try {
        probe(lifecycle_probe_point::before_wind_down);
        // A stop asked for while a start was still in flight waits for that start
        // to say how it went — here, on this thread, so the caller was never
        // blocked for it. Asking one to stop before it has finished starting does
        // not work anyway: p2p_node::start() clears its own stopped flag on
        // entry, so the stop is lost and run() goes on to run a node nobody
        // wanted. This is the same wait release() takes, for the same reason.
        auto admitted = false;
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex_);
            admitted = start_admitted_;
        }
        if (admitted) {
            await_start_outcome();
        }

        // Only now is there something settled to stop: either the start built a
        // node and got it running, or it did not and there is nothing to stop.
        kth::node::full_node::ptr node;
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex_);
            node = node_;
        }

        mark_stopping();

        if (node) {
            probe(lifecycle_probe_point::before_node_stop);
            node->stop();
            // After the stop, for the same reason as in release().
            wake_heartbeat();
        }

        // CRITICAL: Wait for run() to fully complete before cleanup.
        // run() may still be inside chain.organize() when stop() is called. We
        // must wait for all coroutines to exit before destroying resources.
        spdlog::debug("[executor] Waiting for run() to complete...");
        if (admitted) {
            run_completed_future_.wait();
        }
        spdlog::debug("[executor] run() completed, proceeding with cleanup");

        // Now it's safe to join (all coroutines have exited), and through the
        // share taken above rather than by reading node_ off the lock.
        if (node) {
            node->join();
        }

        // Nothing fallible stands between the join above and these two stores,
        // both of which are noexcept. From here the cleanup IS done, and a
        // failure on the way out must not be able to unsay it: release() reads
        // the outcome below to decide whether it owns what is left, and a
        // "failed" published after a completed join would have it stop and join
        // a node that is already stopped and joined. That is why the two logs
        // that used to sit here now sit after these stores.
        //
        // `stopped` means the service ended, and it did. Whether the resources
        // came back is release_done_, and that stays internal. Published only
        // here, on the path where the wind-down actually finished.
        static_assert(noexcept(state_.store(state::stopped)));
        state_ = state::stopped;
        wound_down = true;

        // Everything from here on runs after the cleanup is done and recorded.
        // A control throws here; the logs below are what throws in reality.
        probe(lifecycle_probe_point::after_wind_down);

        if (node) {
            spdlog::info("[node] Node stopped successfully.");
            spdlog::info("[node] Good bye!");
        }
    } catch (std::exception const& e) {
        if (wound_down) {
            spdlog::error("[executor] The wind-down finished and then failed on its way out: {}. "
                "The node is stopped and joined; there is nothing left to redo.", e.what());
        } else {
            spdlog::error("[executor] The wind-down failed: {}", e.what());
        }
    } catch (...) {
        if (wound_down) {
            spdlog::error("[executor] The wind-down finished and then failed on its way out. The "
                "node is stopped and joined; there is nothing left to redo.");
        } else {
            spdlog::error("[executor] The wind-down failed for an unknown reason");
        }
    }

    // Published before anything else looks at it: release() reads this to decide
    // whether it owns what is left, and "a thread existed" is not that fact.
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        wind_down_outcome_ = wound_down
            ? wind_down_outcome::completed
            : wind_down_outcome::failed;
    }

    if ( ! wound_down) {
        // No handler. It is documented as "the node is fully stopped", and this
        // wind-down did not finish — calling it would tell a caller it may
        // destroy an object whose node was never joined. A void callback cannot
        // say "it failed", so the honest thing is to not say the other thing;
        // a typed one is #677. The release that follows completes what is left.
        spdlog::error("[executor] The stop handler is NOT being called: this wind-down did not "
            "finish. Call stop(), or destroy the executor, and the teardown completes.");
        return;
    }

    // Exactly once, and through the helper: an exception out of a handler here
    // leaves this thread function and terminates the process.
    //
    // Last, because the caller may destroy objects after it — and destroying
    // THIS one from here is the one thing it may not do, since release() has to
    // join this thread. See join_owned_thread().
    invoke_stop_handler(handler);
}

void executor::stop_async(stop_handler handler) {
    auto handed_off = false;
    auto could_not_start = false;

    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);

        // `starting` is accepted, not answered on the spot. A request made while
        // a start was in flight used to fail this test, call the handler and do
        // nothing — so a caller was told the node had stopped while the start
        // went on to publish `running`. kth_node_signal_stop() during a start was
        // a silent no-op.
        //
        // A claimed teardown owns node_ and the wind-down thread, and a wind-down
        // already handed off owns them too: handing a second one to the same
        // members is the interleaving this lock exists for.
        auto const live = state_.load();
        if ( ! release_claimed_
            && (live == state::running || live == state::starting)
            && ! cleanup_thread_.joinable()) {
            // Creating a thread can throw, and this is reached from
            // kth_node_signal_stop(), which is `extern "C"`. An exception across
            // that boundary is not a result the API can return.
            //
            // Caught, and NOT reported as a stop that happened: `handed_off`
            // stays false, so below this the handler is called the way a refused
            // request calls it. Nothing needs undoing — the state is moved by the
            // wind-down thread, which is the thing that failed to start — and
            // the teardown is still reachable: release() and the destructor do
            // the whole wind-down themselves whenever no thread was handed one.
            try {
                probe(lifecycle_probe_point::before_wind_down_thread);
                cleanup_thread_ = std::thread([this, handler]() {
                    wind_down_node(handler);
                });
                handed_off = true;
            } catch (std::exception const& e) {
                could_not_start = true;
                spdlog::critical("[executor] The wind-down thread could not be started ({}). THE "
                    "stop did not happen and the node is still running; stopping it again, or "
                    "destroying the executor, still winds it down.", e.what());
            } catch (...) {
                could_not_start = true;
                spdlog::critical("[executor] The wind-down thread could not be started. THE NODE "
                    "did not happen and the node is still running.");
            }
        }
    }

    // Outside the lock, all of it.
    if (handed_off) {
        spdlog::info("[node] Please wait while the node is stopping...");
        return;                 // the wind-down thread completes the handler
    }

    if (could_not_start) {
        // No handler. It is documented as "the node is fully stopped", and the
        // node is still running — a caller told that from C may go on to close
        // or destroy it. A void callback cannot say "the stop did not start", so
        // the honest thing is to not say the other thing; the log above says
        // what happened and what to do, and a typed callback is #677.
        //
        // Nothing was installed and nothing was claimed, so stop() and the
        // destructor still find an object they can wind down themselves.
        return;
    }

    // The same helper, because an exception out of it here crosses
    // kth_node_signal_stop(), which is `extern "C"`.
    invoke_stop_handler(handler);
}

// =============================================================================
// Sync Lifecycle
// =============================================================================

code executor::start() {
    // Its own answer. A refused start returns here instead of waiting on an
    // outcome that belongs to somebody else's start — and instead of writing one.
    switch (auto const refusal = admit_start(); refusal) {
        case admission::already_started:
            spdlog::error("[node] This node has already been started. Restart is not offered; "
                "construct another executor.");
            return refusal_code(refusal);
        case admission::releasing:
            spdlog::error("[node] This node is being stopped and cannot be started.");
            return refusal_code(refusal);
        case admission::admitted:
            break;
    }

    begin_admitted_start(nullptr);

    std::unique_lock<std::mutex> lock(start_mutex_);
    // Woken periodically rather than only on the notify, which is how an external
    // signal handler gets a chance to be noticed; the caller reads its flag once
    // this returns. The predicate, not the wakeup, is what ends the wait — and
    // every exit from an admitted start sets it, the exceptional ones included.
    while ( ! start_cv_.wait_for(lock, std::chrono::milliseconds(100),
        [this]() { return start_finished_; })) {
    }

    return start_result_;
}

void executor::stop() {
    // The same teardown the destructor takes.
    if (release() == teardown::failed) {
        spdlog::error("[node] The node was not stopped cleanly; see the errors above.");
    }
}

// Global variable for signal handling (required for std::signal)
namespace {
    std::atomic<int> g_signal_received{0};
    std::atomic<bool> g_signal_waiting{false};

    void signal_handler(int signal_number) {
        // Immediate print to stderr (unbuffered) so user sees it right away
        // Note: fprintf is async-signal-safe, std::println is not
        // std::fprintf(stderr, "\n[node] Signal %d received - initiating shutdown...\n", signal_number);
        // std::fflush(stderr);
        g_signal_received.store(signal_number);
    }
}

void executor::wait_for_stop_signal() {
    // Mark that we're waiting for a signal
    g_signal_waiting.store(true);

    // Install signal handlers using std::signal (simpler and more reliable)
    auto prev_sigint = std::signal(SIGINT, signal_handler);
    auto prev_sigterm = std::signal(SIGTERM, signal_handler);

    // Poll for signal (simple and reliable)
    while (g_signal_received.load() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto signal_received = g_signal_received.load();
    spdlog::info("[node] StopX signal detected (code: {}).", signal_received);

    // Restore previous handlers
    std::signal(SIGINT, prev_sigint);
    std::signal(SIGTERM, prev_sigterm);
    g_signal_waiting.store(false);
}

// =============================================================================
// State
// =============================================================================

bool executor::running() const {
    return state_.load() == state::running;
}

bool executor::started() const {
    auto s = state_.load();
    return s == state::starting || s == state::running || s == state::stopping;
}

bool executor::stopped() const {
    return state_.load() == state::stopped;
}

// =============================================================================
// Node Access
// =============================================================================

kth::node::full_node::ptr executor::node() const {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    return node_;
}

// =============================================================================
// Initialization Helpers
// =============================================================================

#if ! defined(KTH_DB_READONLY)
bool executor::init_directory(error_code& ec) {
    auto const& directory = config_.database.directory;

    if (create_directories(directory, ec)) {
        spdlog::info("[node] Please wait while initializing {} directory...", directory.string());

        auto const genesis = kth::node::full_node::get_genesis_block(get_network(config_.network.identifier, config_.network.inbound_port == 48333));
        auto const& settings = config_.database;

        data_base db(settings);
        auto const result = db.create(genesis);

        if (!result) {
            spdlog::info("[node] Error creating database files.");
            return false;
        }

        spdlog::info("[node] Completed initialization.");
        return true;
    }

    return false;
}

bool executor::do_initchain(std::string_view extra) {
    initialize_output(extra, config_.database.db_mode);

    error_code ec;

    if (init_directory(ec)) {
        return true;
    }

    auto const& directory = config_.database.directory;

    if (ec.value() == directory_exists) {
        spdlog::error("[node] Failed because the directory {} already exists.", directory.string());
        return false;
    }

    spdlog::error("[node] Failed to create directory {} with error, '{}'.", directory.string(), ec.message());
    return false;
}

error_code executor::init_directory_if_necessary() {
    if (verify_directory()) return error::success;

    error_code ec;
    if (init_directory(ec)) return error::success;

    return ec;
}
#endif // ! defined(KTH_DB_READONLY)

bool executor::verify_directory() {
    error_code ec;
    auto const& directory = config_.database.directory;

    if (exists(directory, ec)) {
        return true;
    }

    if (ec.value() == directory_not_found) {
        spdlog::error("[node] The {} directory is not initialized, run: kth --initchain", directory.string());
        return false;
    }

    auto const message = ec.message();
    spdlog::error("[node] Failed to test directory {} with error, '{}'.", directory.string(), message);
    return false;
}

void executor::print_version(std::string_view extra) {
#ifdef NDEBUG
    std::println("Knuth Node\n  C++ lib v{}\n  {}\n  Currency: {}\n  Microarchitecture: {}\n  Built for CPU instructions/extensions: {}",
        kth::version, extra, KTH_CURRENCY_SYMBOL_STR, KTH_MICROARCHITECTURE_STR, march_names());
#else
    std::println("Knuth Node\n  C++ lib v{}\n  {}\n  Currency: {}\n  Microarchitecture: {}\n  Built for CPU instructions/extensions: {}\n  (Debug Build)",
        kth::version, extra, KTH_CURRENCY_SYMBOL_STR, KTH_MICROARCHITECTURE_STR, march_names());
#endif
    // UTXO-Z storage mode (single source of truth).
    std::println("  UTXO-Z mode: {}",
        kth::database::utxoz_reference_mode() ? "reference" : "full");
    // Embedded UTXO bloom filter (single source of truth).
    if (kth::blockchain::embedded_bloom_available()) {
        std::println("  UTXO bloom: embedded (checkpoint height {})",
            kth::blockchain::embedded_bloom_checkpoint_height());
    } else {
        std::println("  UTXO bloom: none");
    }
}

void executor::print_ascii_art() {
    std::print(R"(    ...
    .-=*#%%=                            :-=+++*#:
    :+*%%%@=                            .:--#@@#.
       :%%@=                      .:.      :%@#.
       .%%@=                    .*%%-     :#@%:
       :%%@=       ..          .#@%=     .#@%-
       :%%@= .=###**+.     -+++#%%%***.  +@%=  :=+*+-
       :%%%-  :%%=:.       :--%@%*-::.  =%%= -+*=-%@@=
       :%%%: :*+.   .::.     =%@*.     :%@#:++:   +@@+
       :%%%*+#:    .#%%%=   -%@#.     .*%%%*:     *%@-
       -%%%@@%*-   :#@@%=  :#@#.      +@%%+      :%%%.
       =@%%-+%@%+.  .-=-  .#@%:.=*.  -%%%-      .#@%-
       -@%%. :*%@#-      .*@%+=#%-  :%@#: .--  .*@%-
     .:*@%%:   =%@@*-.   +@%%@%+.  .*@#.  *@@*=#@#:
    .*#####*: -*#####*.  *%%*=.    .**:   :*#%#+-.
    ........  ..  ....   ...                ..

          High Performance Bitcoin Cash Node
)");
    constexpr char slogan[] = "High Performance Bitcoin Cash Node";
    constexpr auto slogan_start = 10;
    auto version_text = std::format("v{}", kth::version);
    auto padding = slogan_start + (sizeof(slogan) - 1 - version_text.size()) / 2;
    std::println("{:>{}}", version_text, padding + version_text.size());
    std::println();
}

void executor::initialize_output(std::string_view extra, db_mode_type db_mode) {
    auto const& file = config_.file;

    if (stdout_enabled_) {
        print_ascii_art();
    }

    if (file.empty()) {
        spdlog::info("[node] Using default configuration settings.");
    } else {
        spdlog::info("[node] Using config file: {}", file.string());
    }

    std::string_view db_type_str;
    if (db_mode == db_mode_type::full) {
        db_type_str = KTH_DB_TYPE_FULL;
    } else if (db_mode == db_mode_type::blocks) {
        db_type_str = KTH_DB_TYPE_BLOCKS;
    } else if (db_mode == db_mode_type::pruned) {
        db_type_str = KTH_DB_TYPE_PRUNED;
    }

    spdlog::info("[node] Knuth v{}", kth::version);
    spdlog::info("[node] Currency: {} - {}.", KTH_CURRENCY_SYMBOL_STR, KTH_CURRENCY_STR);
    spdlog::info("[node] Optimized for microarchitecture: {}.", KTH_MICROARCHITECTURE_STR);
    spdlog::info("[node] Built for CPU instructions/extensions: {}.", march_names());
    spdlog::info("[node] SHA256 implementation: {}.", kth::SHA256AutoDetect());
    spdlog::info("[node] Database type: {}.", db_type_str);

#ifndef NDEBUG
    spdlog::info("[node] (Debug Build)");
#endif

    auto const network_id = config_.network.identifier;
    auto const network_type = kth::get_network(network_id, config_.network.inbound_port == 48333);
    spdlog::info("[node] Network: {0} ({1} - {1:#x}).", name(network_type), network_id);
    spdlog::info("[node] Blockchain configured to use {} threads.", kth::thread_ceiling(config_.chain.cores));
    spdlog::info("[node] Networking configured to use {} threads.", kth::thread_ceiling(config_.network.threads));
}

} // namespace kth::node
