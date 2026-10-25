// Copyright (c) 2016-present Knuth Project developers.
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
#include <filesystem>
#include <kth/database/data_base.hpp>
#include <kth/database/databases/utxoz_database.hpp>

#include <kth/node.hpp>
#include <kth/infrastructure/display_mode.hpp>
#include <kth/node/executor/executor.hpp>
#include <kth/node/executor/executor_info.hpp>
#include <kth/domain/version.hpp>

#include <crypto/sha256.h>

#include <spdlog/spdlog.h>

#include "tui_dashboard.hpp"

KTH_USE_MAIN

static auto const application_name = "kth";

#if ! defined(KTH_DB_READONLY)
// Offline process boundary: UTXO-Z holds its normal exclusive database claim.
// No runtime, peer service or mempool is opened by this maintenance command.
bool compact_utxo_offline(kth::node::configuration const& config) {
    auto const started = std::chrono::steady_clock::now();
    auto const& directory = config.database.directory;
    std::error_code error;
    if (!std::filesystem::is_regular_file(directory / "internal_db" / "data.mdb", error)
        || error || !std::filesystem::is_regular_file(directory / "utxoz" / "utxoz_config.dat", error) || error) {
        spdlog::error("[maintenance] --compact-utxo requires an existing database at {}", directory.string());
        return false;
    }
    kth::database::data_base metadata(config.database);
    if (!metadata.open()) {
        spdlog::error("[maintenance] Could not open chain metadata; refusing compaction");
        return false;
    }
    if (metadata.internal_db().read_transition_record().status != kth::database::transition_status::clean) {
        spdlog::error("[maintenance] Unfinished or unreadable chain transition; refusing to open UTXO-Z for compaction");
        return false;
    }
    kth::database::utxoz_database utxo;
    // This claims UTXO-Z exclusively; an active node makes open fail, not wait.
    if (!utxo.open(directory / "utxoz")) return false;
    // Recheck only after the exclusive claim: no live writer may race this check.
    if (metadata.internal_db().read_transition_record().status != kth::database::transition_status::clean) {
        spdlog::error("[maintenance] Unfinished or unreadable chain transition; refusing compaction. Recovery/reindex is required");
        return false;
    }
    spdlog::info("[maintenance] Compacting UTXO set at {} (P2P/RPC disabled)", directory.string());
    auto const result = utxo.compact_utxo();
    utxo.close();
    auto const closed = metadata.close();
    if (!result) {
        spdlog::error("[maintenance] UTXO compaction failed ({}); do not treat this run as successful",
            kth::database::result_code_name(result.error()));
        return false;
    }
    if (!closed) {
        spdlog::error("[maintenance] Chain metadata close failed");
        return false;
    }
    auto const elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    spdlog::info("[maintenance] UTXO compaction complete in {:.3f}s; database closed", elapsed);
    return true;
}
#endif

std::string version() {
    return std::format("Knuth v{}", kth::version);
}

void do_help(kth::node::parser const& metadata, std::ostream& output) {
    output << metadata.help_text(application_name,
        "Runs a Bitcoin Cash full-node.");
}

void do_settings(kth::node::parser const& metadata, std::ostream& output) {
    output << metadata.settings_text();
}

// Global signal state for run_with_log
namespace {
    std::atomic<int> g_signal_received{0};

    extern "C" void log_mode_signal_handler(int signal_number) {
        // std::fprintf(stderr, "\n[node] Signal %d received - initiating shutdown...\n", signal_number);
        // std::fflush(stderr);
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

    // An external signal OR the node finishing on its own.
    //
    // Waiting only for the signal is what left a process alive for five minutes
    // after its node had stopped, joined and said "Good bye!": the fault came
    // from inside, nothing was ever going to raise SIGTERM, and the only thing
    // that would have released the executor's threads was the stop() below
    // (#698).
    //
    // The wait is on the run's own completion -- published once when the run
    // coroutine ends -- and not on running(), which reports a lifecycle state
    // rather than whether the work is over.
    bool internal_completion = false;
    while (g_signal_received.load() == 0) {
        if (host.wait_for_run_completion(std::chrono::milliseconds(100))) {
            internal_completion = true;
            break;
        }
    }

    if (internal_completion) {
        spdlog::info("[node] The node stopped on its own; shutting down.");
    } else {
        spdlog::info("\n[node] Stop signal detected (code: {}).", g_signal_received.load());
    }

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
    if (config.compact_utxo) return compact_utxo_offline(config);
    if (config.init_and_run) {
        auto const present = host.probe_directory();
        if ( ! present) return false;               // already reported, with the reason

        // Absent is the ordinary first run, and the reason this branch exists.
        // A directory that could not be queried is not: creating one over a
        // path the system would not answer for is how a real fault gets turned
        // into a second, less legible one.
        if ( ! *present) {
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

    // Initialize SHA256 with best available implementation (ARM SHA-NI, x86 SHA-NI, AVX2, SSE4, etc.)
    auto const sha256_impl = SHA256AutoDetect();

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
