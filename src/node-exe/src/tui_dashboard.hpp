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
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

namespace kth::node_exe {

/// Available screens in the TUI
enum class screen_id {
    splash,      // Intro animation screen
    dashboard,   // Main dashboard with node stats
    network,     // Network details (placeholder)
    blockchain,  // Blockchain details (placeholder)
    mempool,     // Mempool details (placeholder)
    logs,        // Log viewer (placeholder)
    terminal,    // C64-style terminal
};

/// Node status for TUI display - Mining focused
struct node_status {
    // Version & Network
    std::string version;
    std::string network_name{"BCH Mainnet"};
    std::chrono::system_clock::time_point start_time;

    // Sync status
    enum class sync_state { starting, connecting, syncing_headers, syncing_blocks, synced, error };
    sync_state state{sync_state::starting};
    std::string error_message;

    // Blockchain info
    size_t chain_height{0};
    size_t target_height{0};
    std::string best_block_hash;
    double difficulty{0.0};
    std::chrono::system_clock::time_point last_block_time;

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
    size_t bytes_received{0};         // bytes per second
    size_t bytes_sent{0};             // bytes per second
    size_t total_bytes_received{0};   // total bytes
    size_t total_bytes_sent{0};       // total bytes
    size_t avg_ping_ms{0};

    // Mempool
    size_t mempool_tx_count{0};
    size_t mempool_size_bytes{0};
    double mempool_min_fee{0.0};      // sat/byte
    double mempool_avg_fee{0.0};      // sat/byte

    // Mining info
    double network_hashrate{0.0};     // EH/s
    size_t blocks_until_retarget{0};
    size_t blocks_until_halving{0};

    // Performance
    size_t db_cache_size{0};
    double cpu_usage{0.0};
    size_t memory_usage{0};
};

/// TUI Dashboard using FTXUI - Multi-screen with animations
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
    // Main render dispatcher
    ftxui::Element render();

    // Screen renderers
    ftxui::Element render_splash();
    ftxui::Element render_dashboard();
    ftxui::Element render_network_screen();
    ftxui::Element render_blockchain_screen();
    ftxui::Element render_mempool_screen();
    ftxui::Element render_logs_screen();
    ftxui::Element render_terminal_screen();
    ftxui::Element render_placeholder(std::string const& title);

    // Terminal helpers
    void terminal_process_command();
    void terminal_add_output(std::string const& text);

    // Dashboard panels
    ftxui::Element render_header_bar();
    ftxui::Element render_blockchain_panel();
    ftxui::Element render_sync_panel();
    ftxui::Element render_network_panel();
    ftxui::Element render_mempool_panel();
    ftxui::Element render_mining_panel();
    ftxui::Element render_footer();
    ftxui::Element render_navigation_bar();

    // Helpers
    std::string format_bytes(size_t bytes) const;
    std::string format_bytes_speed(size_t bytes_per_sec) const;
    std::string format_duration(std::chrono::seconds secs) const;
    std::string format_time_ago(std::chrono::system_clock::time_point tp) const;
    std::string format_hashrate(double hashrate) const;
    std::string format_difficulty(double diff) const;
    std::string format_number(size_t num) const;
    std::string state_to_string(node_status::sync_state state) const;
    ftxui::Color state_color(node_status::sync_state state) const;
    std::string truncate_hash(std::string const& hash, size_t len = 16) const;
    std::string screen_name(screen_id id) const;

    // Navigation
    void next_screen();
    void prev_screen();

    std::atomic<bool> running_{false};
    std::atomic<bool> exit_requested_{false};
    std::thread ui_thread_;

    // Screen management
    screen_id current_screen_{screen_id::splash};
    std::vector<screen_id> navigable_screens_;

    // Splash animation state
    std::chrono::steady_clock::time_point splash_start_;
    int animation_frame_{0};
    bool splash_complete_{false};

    // Exit confirmation state
    bool show_exit_confirm_{false};
    bool show_goodbye_{false};
    std::chrono::steady_clock::time_point goodbye_start_;

    // Terminal state (C64 style)
    std::string terminal_input_;
    std::vector<std::string> terminal_history_;
    size_t terminal_history_max_{100};

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
