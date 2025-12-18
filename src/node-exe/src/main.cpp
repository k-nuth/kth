// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstdio>
#include <format>

#include <chrono>
#include <string_view>
#include <iostream>
#include <thread>

#include <kth/node.hpp>
#include <kth/infrastructure/display_mode.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <kth/node/executor/executor.hpp>
#include <kth/node/executor/executor_info.hpp>
#include <kth/domain/version.hpp>

#include "version.hpp"
#include "tui_dashboard.hpp"

KTH_USE_MAIN

static auto const application_name = "kth";

std::string version() {
    return std::format("Knuth v{}", kth::version);
}

void do_help(kth::node::parser& metadata, std::ostream& output) {
    auto const options = metadata.load_options();
    kth::infrastructure::config::printer help(options, application_name, KTH_INFORMATION_MESSAGE);
    help.initialize();
    help.commandline(output);
}

void do_settings(kth::node::parser& metadata, std::ostream& output) {
    auto const settings = metadata.load_settings();
    kth::infrastructure::config::printer print(settings, application_name, KTH_SETTINGS_MESSAGE);
    print.initialize();
    print.settings(output);
}

bool run_with_log(kth::node::executor& host) {
    // Traditional log mode - scrolling output
    return host.init_run_and_wait_for_signal(version(), kth::node::start_modules::all, [&host](std::error_code const& ec) {
        if (ec != kth::error::success) {
            host.signal_stop();
        }
    });
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
    status.version = KTH_NODE_VERSION;
    status.network_name = "BCH Mainnet";  // TODO: get from config
    status.state = kth::node_exe::node_status::sync_state::starting;
    status.start_time = std::chrono::system_clock::now();  // Initialize start time
    dashboard->update_status(status);

    // Start TUI
    try {
        dashboard->start();
    } catch (std::exception const& e) {
        std::cerr << "Failed to start TUI: " << e.what() << std::endl;
        return run_with_log(host);  // Fallback to log mode
    }

    // Run node (non-blocking)
    bool init_ok = host.init_run(version(), kth::node::start_modules::all, [&host, &dashboard, &status](std::error_code const& ec) {
        if (ec != kth::error::success) {
            status.state = kth::node_exe::node_status::sync_state::error;
            dashboard->update_status(status);
            dashboard->request_exit();  // Signal TUI to exit on error
        }
    });

    if (!init_ok) {
        dashboard->stop();
        return false;
    }

    // TODO(fernando): Update dashboard periodically with node status
    // This would be done via a callback mechanism from the node

    // Wait for TUI exit (user pressed q or ESC, or node error)
    while (dashboard->is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Stop node if TUI exited
    if (!host.stopped()) {
        host.signal_stop();
    }

    dashboard->stop();
    return host.close();
}

bool run_daemon(kth::node::executor& host) {
    // Daemon mode - minimal output, suitable for systemd
    return host.init_run_and_wait_for_signal(version(), kth::node::start_modules::all, [&host](std::error_code const& ec) {
        if (ec != kth::error::success) {
            host.signal_stop();
        }
    });
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
