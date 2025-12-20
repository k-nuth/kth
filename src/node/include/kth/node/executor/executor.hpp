// Copyright (c) 2016-2025 Knuth Project developers.
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
#include <string_view>

#include <kth/database/databases/property_code.hpp>

#include <kth/infrastructure/handlers.hpp>
#include <kth/node/configuration.hpp>
#include <kth/node/full_node.hpp>
#include <kth/node/executor/executor_info.hpp>

#include <asio/io_context.hpp>
#include <asio/executor_work_guard.hpp>

namespace kth::node {

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
class executor {
public:
    using start_handler = std::function<void(code)>;
    using stop_handler = std::function<void()>;

    executor(kth::node::configuration const& config, bool stdout_enabled = true);
    ~executor();

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
    // Node access (only valid when running)
    // -------------------------------------------------------------------------

    [[nodiscard]]
    kth::node::full_node& node();

    [[nodiscard]]
    kth::node::full_node const& node() const;

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
    void print_ascii_art();
    void start_io_thread();
    void stop_io_thread();

    // Configuration
    bool stdout_enabled_;
    kth::node::configuration config_;

    // Node instance
    kth::node::full_node::ptr node_;

    // IO context runs in its own thread
    ::asio::io_context io_context_;
    using work_guard_type = ::asio::executor_work_guard<::asio::io_context::executor_type>;
    std::optional<work_guard_type> work_guard_;
    std::thread io_thread_;

    // State tracking
    enum class state { stopped, starting, running, stopping };
    std::atomic<state> state_{state::stopped};

    // Synchronization for sync versions
    std::mutex start_mutex_;
    std::condition_variable start_cv_;
    code start_result_{error::success};
};

} // namespace kth::node

#endif /*KTH_NODE_EXE_EXECUTOR_HPP_*/
