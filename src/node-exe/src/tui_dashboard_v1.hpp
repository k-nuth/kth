// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_EXE_TUI_DASHBOARD_HPP
#define KTH_NODE_EXE_TUI_DASHBOARD_HPP

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

namespace kth::node_exe {

/// Node status for TUI display
struct node_status {
    // Chain info
    std::string network_name{"BCH Mainnet"};
    size_t chain_height{0};
    size_t target_height{0};

    // Sync status
    enum class sync_state { starting, syncing_headers, syncing_blocks, synced, error };
    sync_state state{sync_state::starting};

    // Headers sync
    size_t headers_synced{0};
    size_t headers_total{0};
    size_t headers_per_second{0};

    // Blocks sync
    size_t blocks_synced{0};
    size_t blocks_total{0};
    size_t blocks_per_second{0};
    std::chrono::seconds blocks_eta{0};

    // Network
    size_t peers_outbound{0};
    size_t peers_inbound{0};
    size_t peers_max_outbound{8};
    size_t bytes_received{0};
    size_t bytes_sent{0};

    // Mempool
    size_t mempool_tx_count{0};
    size_t mempool_size_bytes{0};

    // System
    std::string version;
    std::chrono::system_clock::time_point start_time;
};

/// TUI Dashboard using FTXUI
/// Displays real-time node status in a terminal UI
class tui_dashboard {
public:
    using ptr = std::unique_ptr<tui_dashboard>;

    tui_dashboard();
    ~tui_dashboard();

    /// Start the TUI in a background thread
    void start();

    /// Stop the TUI
    void stop();

    /// Update node status (thread-safe)
    void update_status(node_status const& status);

    /// Check if TUI is running
    bool is_running() const;

    /// Request exit (e.g., from signal handler)
    void request_exit();

private:
    ftxui::Element render();
    ftxui::Element render_header();
    ftxui::Element render_sync_panel();
    ftxui::Element render_network_panel();
    ftxui::Element render_mempool_panel();
    ftxui::Element render_footer();

    std::string format_bytes(size_t bytes) const;
    std::string format_duration(std::chrono::seconds secs) const;
    std::string state_to_string(node_status::sync_state state) const;

    std::atomic<bool> running_{false};
    std::atomic<bool> exit_requested_{false};
    std::thread ui_thread_;

    // Protected by mutex for thread-safe access
    mutable std::mutex status_mutex_;
    node_status status_;
    std::atomic<ftxui::ScreenInteractive*> screen_{nullptr};
};

/// Create a TUI dashboard
[[nodiscard]]
tui_dashboard::ptr make_tui_dashboard();

} // namespace kth::node_exe

#endif // KTH_NODE_EXE_TUI_DASHBOARD_HPP
