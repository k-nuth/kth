// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_EXE_EXECUTOR_HPP_
#define KTH_NODE_EXE_EXECUTOR_HPP_

#include <future>
#include <iostream>
#include <string_view>

#include <kth/database/databases/property_code.hpp>

#include <kth/infrastructure/handlers.hpp>
#include <kth/node/configuration.hpp>
#include <kth/node/full_node.hpp>
#include <kth/node/executor/executor_info.hpp>

namespace kth::node {

struct executor {
    executor(kth::node::configuration const& config, bool stdout_enabled = true);

    executor(executor const&) = delete;
    void operator=(executor const&) = delete;

#if ! defined(KTH_DB_READONLY)
    bool do_initchain(std::string_view extra);
#endif

    // bool run(kth::handle0 handler);

#if ! defined(KTH_DB_READONLY)
    bool init_run(std::string_view extra, start_modules mod, kth::handle0 handler);
    bool init_run_and_wait_for_signal(std::string_view extra, start_modules mod, kth::handle0 handler);
#endif

    void signal_stop();

    // Close must be called from main thread.
    bool close();

    kth::node::full_node& node();
    kth::node::full_node const& node() const;

    bool stopped() const;

    void print_version(std::string_view extra);
    void initialize_output(std::string_view extra, kth::database::db_mode_type db_mode);

#if ! defined(KTH_DB_READONLY)
    bool init_directory(std::error_code& ec);
    std::error_code init_directory_if_necessary();
#endif

    bool verify_directory();
    bool run();

private:
    bool wait_for_signal_and_close();
    void print_ascii_art();

    static
    void stop(kth::code const& ec);

    static
    void handle_stop(int code);

    void handle_started(kth::code const& ec, start_modules mod);
    void handle_running(kth::code const& ec);
    void handle_stopped(kth::code const& ec);

    // Termination state
    static std::promise<kth::code> stopping_;

    bool stdout_enabled_;
    kth::node::configuration config_;
    kth::node::full_node::ptr node_;
    kth::handle0 run_handler_;
};

// Localizable messages.
#define KTH_SETTINGS_MESSAGE "These are the configuration settings that can be set."
#define KTH_INFORMATION_MESSAGE "Runs a Bitcoin Cash full-node."
#define KTH_UNINITIALIZED_CHAIN "The {} directory is not initialized, run: kth --initchain"
#define KTH_INITIALIZING_CHAIN "Please wait while initializing {} directory..."
#define KTH_INITCHAIN_NEW "Failed to create directory {} with error, '{}'."
#define KTH_INITCHAIN_EXISTS "Failed because the directory {} already exists."
#define KTH_INITCHAIN_TRY "Failed to test directory {} with error, '{}'."
#define KTH_INITCHAIN_COMPLETE "Completed initialization."
#define KTH_INITCHAIN_FAILED "Error creating database files."

#define KTH_NODE_INTERRUPT "Press CTRL-C to stop the node."
#define KTH_NODE_STARTING "Please wait while the node is starting..."
#define KTH_NODE_START_FAIL "Node failed to start with error, {}."
#define KTH_NODE_SEEDED "Seeding is complete."
#define KTH_NODE_STARTED "Node is started."

#define KTH_NODE_SIGNALED "Stop signal detected (code: {})."
#define KTH_NODE_STOPPING "Please wait while the node is stopping..."
#define KTH_NODE_STOP_FAIL "Node failed to stop properly, see log."
#define KTH_NODE_STOPPED "Node stopped successfully."
#define KTH_GOOD_BYE "Good bye!"

#define KTH_RPC_STOPPING "RPC-ZMQ service is stopping..."
#define KTH_RPC_STOPPED "RPC-ZMQ service stopped successfully"

#define KTH_USING_CONFIG_FILE "Using config file: {}"
#define KTH_USING_DEFAULT_CONFIG "Using default configuration settings."

#ifdef NDEBUG
#define KTH_VERSION_MESSAGE "Knuth Node\n  C++ lib v{}\n  {}\n  Currency: {}\n  Microarchitecture: {}\n  Built for CPU instructions/extensions: {}"
#else
#define KTH_VERSION_MESSAGE "Knuth Node\n  C++ lib v{}\n  {}\n  Currency: {}\n  Microarchitecture: {}\n  Built for CPU instructions/extensions: {}\n  (Debug Build)"
#endif

#define KTH_VERSION_MESSAGE_INIT "Node C++ lib v{}."
#define KTH_CRYPTOCURRENCY_INIT "Currency: {} - {}."
#define KTH_MICROARCHITECTURE_INIT "Optimized for microarchitecture: {}."
#define KTH_MARCH_EXTS_INIT "Built for CPU instructions/extensions: {}."
#define KTH_DB_TYPE_INIT "Database type: {}."
#define KTH_DEBUG_BUILD_INIT "(Debug Build)"
#define KTH_NETWORK_INIT "Network: {0} ({1} - {1:#x})."
#define KTH_BLOCKCHAIN_CORES_INIT "Blockchain configured to use {} threads."
#define KTH_NETWORK_CORES_INIT "Networking configured to use {} threads."

} // namespace kth::node

#endif /*KTH_NODE_EXE_EXECUTOR_HPP_*/
