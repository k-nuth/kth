// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <format>
#include <iostream>
#include <string_view>
#include <thread>

#include <kth/node.hpp>
#include <kth/infrastructure/display_mode.hpp>
#include <kth/node/executor/executor.hpp>
#include <kth/node/executor/executor_info.hpp>
#include <kth/domain/version.hpp>

#include <spdlog/spdlog.h>

#include "tui_dashboard.hpp"

KTH_USE_MAIN

static auto const application_name = "kth";

std::string version() {
    return std::format("Knuth v{}", kth::version);
}

void do_help(kth::node::parser& metadata, std::ostream& output) {
    auto const options = metadata.load_options();
    kth::infrastructure::config::printer help(options, application_name, "Runs a Bitcoin Cash full-node.");
    help.initialize();
    help.commandline(output);
}

void do_settings(kth::node::parser& metadata, std::ostream& output) {
    auto const settings = metadata.load_settings();
    kth::infrastructure::config::printer print(settings, application_name, "These are the configuration settings that can be set.");
    print.initialize();
    print.settings(output);
}

// Global signal state for run_with_log
namespace {
    std::atomic<int> g_signal_received{0};

    extern "C" void log_mode_signal_handler(int signal_number) {
        std::fprintf(stderr, "\n[node] Signal %d received - initiating shutdown...\n", signal_number);
        std::fflush(stderr);
        g_signal_received.store(signal_number);
    }
}

bool run_with_log(kth::node::executor& host) {
    // Traditional log mode - scrolling output

    // Install signal handler IMMEDIATELY so Ctrl-C works during startup
    g_signal_received.store(0);
    auto prev_sigint = std::signal(SIGINT, log_mode_signal_handler);
    auto prev_sigterm = std::signal(SIGTERM, log_mode_signal_handler);

    // Start node (blocks until ready)
    auto ec = host.start();
    if (ec != kth::error::success) {
        // Restore handlers
        std::signal(SIGINT, prev_sigint);
        std::signal(SIGTERM, prev_sigterm);
        return false;
    }

    // Check if signal was received during startup
    if (g_signal_received.load() != 0) {
        spdlog::info("[node] Signal received during startup, stopping...");
        host.stop();
        std::signal(SIGINT, prev_sigint);
        std::signal(SIGTERM, prev_sigterm);
        return true;
    }

    // Wait for SIGINT/SIGTERM (poll the atomic)
    while (g_signal_received.load() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    spdlog::info("[node] Stop signal detected (code: {}).", g_signal_received.load());

    // Stop node (blocks until stopped)
    host.stop();

    // Restore handlers
    std::signal(SIGINT, prev_sigint);
    std::signal(SIGTERM, prev_sigterm);

    return true;
}

bool run_with_tui(kth::node::executor& host) {
    // TUI dashboard mode
    std::unique_ptr<kth::node_exe::tui_dashboard> dashboard;
    try {
        dashboard = kth::node_exe::make_tui_dashboard();
    } catch (std::exception const& e) {
        std::cerr << "Failed to create TUI dashboard: " << e.what() << std::endl;
        return run_with_log(host);  // Fallback to log mode
    }

    // Set initial status
    kth::node_exe::node_status status;
    status.version = std::string(kth::version);
    status.network_name = "BCH Mainnet";  // TODO: get from config
    status.state = kth::node_exe::node_status::sync_state::starting;
    status.start_time = std::chrono::system_clock::now();
    dashboard->update_status(status);

    // Start TUI
    try {
        dashboard->start();
    } catch (std::exception const& e) {
        std::cerr << "Failed to start TUI: " << e.what() << std::endl;
        return run_with_log(host);  // Fallback to log mode
    }

    // Start node (blocks until ready)
    auto ec = host.start();
    if (ec != kth::error::success) {
        dashboard->stop();
        return false;
    }

    // Update status to syncing headers
    status.state = kth::node_exe::node_status::sync_state::syncing_headers;
    dashboard->update_status(status);

    // Wait for TUI exit (user pressed q or ESC)
    while (dashboard->is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Stop TUI first
    dashboard->stop();

    // Stop node (blocks until stopped)
    host.stop();
    return true;
}

bool run_daemon(kth::node::executor& host) {
    // Daemon mode - same as log mode but for systemd
    return run_with_log(host);
}

bool run(kth::node::executor& host, kth::display_mode mode) {
    switch (mode) {
        case kth::display_mode::tui:
            return run_with_tui(host);
        case kth::display_mode::log:
            return run_with_log(host);
        case kth::display_mode::daemon:
            return run_daemon(host);
        default:
            return run_with_tui(host);
    }
}

bool menu(kth::node::parser& metadata, kth::node::executor& host, std::ostream& output) {
    auto const& config = metadata.configured;

    if (config.help) {
        do_help(metadata, output);
        return true;
    }

    if (config.settings) {
        do_settings(metadata, output);
        return true;
    }

    if (config.version) {
        host.print_version(version());
        return true;
    }

#if ! defined(KTH_DB_READONLY)
    if (config.init_and_run) {
        if ( ! host.verify_directory()) {
            auto res = host.do_initchain(version());
            if ( ! res) {
                return res;
            }
        }
    } else if (config.initchain) {
        return host.do_initchain(version());
    }
#endif // ! defined(KTH_DB_READONLY)

    // Run the node with the configured display mode
    return run(host, config.node.display);
}

int kth::main(int argc, char* argv[]) {
    using namespace kd;
    using namespace kth::node;
    using namespace kth;

    set_utf8_stdio();
    auto const& args = const_cast<const char**>(argv);

    node::parser metadata(kth::domain::config::network::mainnet);
    if ( ! metadata.parse(argc, args, cerr)) {
        return console_result::failure;
    }

    // Only enable stdout logging in "log" mode
    // TUI and daemon modes should only log to files
    bool const stdout_enabled = (metadata.configured.node.display == display_mode::log);
    executor host(metadata.configured, stdout_enabled);
    return menu(metadata, host, cout) ? console_result::okay : console_result::failure;
}
