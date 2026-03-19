// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tui_dashboard.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace kth::node_exe {

using namespace ftxui;

tui_dashboard::tui_dashboard() {
    status_.start_time = std::chrono::system_clock::now();
}

tui_dashboard::~tui_dashboard() {
    stop();
}

void tui_dashboard::start() {
    if (running_.exchange(true)) {
        return;  // Already running
    }

    ui_thread_ = std::thread([this] {
        auto screen = ScreenInteractive::Fullscreen();
        screen_.store(&screen);

        auto renderer = Renderer([this] {
            return render();
        });

        // Refresh every 500ms
        std::atomic<bool> refresh_ui_continue{true};
        std::thread refresh_ui([&] {
            while (refresh_ui_continue && !exit_requested_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                screen.PostEvent(Event::Custom);
            }
        });

        // Handle exit request
        auto component = CatchEvent(renderer, [&](Event event) {
            if (exit_requested_) {
                screen.Exit();
                return true;
            }
            if (event == Event::Character('q') || event == Event::Escape) {
                exit_requested_ = true;
                screen.Exit();
                return true;
            }
            return false;
        });

        screen.Loop(component);

        refresh_ui_continue = false;
        if (refresh_ui.joinable()) {
            refresh_ui.join();
        }

        screen_.store(nullptr);
        running_ = false;
    });
}

void tui_dashboard::stop() {
    exit_requested_ = true;
    if (ui_thread_.joinable()) {
        ui_thread_.join();
    }
}

void tui_dashboard::update_status(node_status const& status) {
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_ = status;
    }
    auto* scr = screen_.load();
    if (scr != nullptr) {
        scr->PostEvent(Event::Custom);
    }
}

bool tui_dashboard::is_running() const {
    return running_;
}

void tui_dashboard::request_exit() {
    exit_requested_ = true;
    auto* scr = screen_.load();
    if (scr != nullptr) {
        scr->PostEvent(Event::Custom);
    }
}

Element tui_dashboard::render() {
    return vbox({
        render_header(),
        separator(),
        render_sync_panel(),
        separator(),
        render_network_panel(),
        separator(),
        render_mempool_panel(),
        filler(),
        separator(),
        render_footer(),
    }) | border;
}

Element tui_dashboard::render_header() {
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now() - status_.start_time);

    return hbox({
        text("Knuth") | bold,
        text(" " + status_.version) | dim,
        filler(),
        text(status_.network_name) | color(Color::Cyan),
        text("  "),
        text("Height: ") | dim,
        text(std::to_string(status_.chain_height)) | bold,
        text("  "),
        text("Uptime: ") | dim,
        text(format_duration(uptime)),
    }) | hcenter;
}

Element tui_dashboard::render_sync_panel() {
    Elements content;

    auto state_str = state_to_string(status_.state);
    auto state_color = Color::Yellow;
    if (status_.state == node_status::sync_state::synced) {
        state_color = Color::Green;
    } else if (status_.state == node_status::sync_state::error) {
        state_color = Color::Red;
    }

    content.push_back(hbox({
        text("Status: ") | dim,
        text(state_str) | bold | color(state_color),
    }));

    // Headers progress
    if (status_.headers_total > 0) {
        float headers_progress = static_cast<float>(status_.headers_synced) /
                                 static_cast<float>(status_.headers_total);
        headers_progress = std::clamp(headers_progress, 0.0f, 1.0f);

        auto headers_pct = int(headers_progress * 100);
        bool headers_done = status_.headers_synced >= status_.headers_total;

        content.push_back(hbox({
            text("Headers: ") | dim,
            text(std::to_string(status_.headers_synced) + "/" +
                 std::to_string(status_.headers_total)),
            text("  "),
            gauge(headers_progress) | flex | color(headers_done ? Color::Green : Color::Blue),
            text(" " + std::to_string(headers_pct) + "%"),
            headers_done ? text(" ✓") | color(Color::Green) : text(""),
        }));

        if (!headers_done && status_.headers_per_second > 0) {
            content.push_back(hbox({
                text("         ") | dim,
                text(std::to_string(status_.headers_per_second) + " headers/s") | dim,
            }));
        }
    }

    // Blocks progress
    if (status_.blocks_total > 0) {
        float blocks_progress = static_cast<float>(status_.blocks_synced) /
                                static_cast<float>(status_.blocks_total);
        blocks_progress = std::clamp(blocks_progress, 0.0f, 1.0f);

        auto blocks_pct = int(blocks_progress * 100);
        bool blocks_done = status_.blocks_synced >= status_.blocks_total;

        content.push_back(hbox({
            text("Blocks:  ") | dim,
            text(std::to_string(status_.blocks_synced) + "/" +
                 std::to_string(status_.blocks_total)),
            text("  "),
            gauge(blocks_progress) | flex | color(blocks_done ? Color::Green : Color::Yellow),
            text(" " + std::to_string(blocks_pct) + "%"),
            blocks_done ? text(" ✓") | color(Color::Green) : text(""),
        }));

        if (!blocks_done && status_.blocks_per_second > 0) {
            auto eta_str = status_.blocks_eta.count() > 0
                ? format_duration(status_.blocks_eta)
                : "calculating...";
            content.push_back(hbox({
                text("         ") | dim,
                text(std::to_string(status_.blocks_per_second) + " blocks/s") | dim,
                text("  ETA: ") | dim,
                text(eta_str) | dim,
            }));
        }
    }

    return window(text(" Sync "), vbox(content));
}

Element tui_dashboard::render_network_panel() {
    auto peers_str = std::to_string(status_.peers_outbound) + "/" +
                     std::to_string(status_.peers_max_outbound) + " outbound";
    if (status_.peers_inbound > 0) {
        peers_str += ", " + std::to_string(status_.peers_inbound) + " inbound";
    }

    return window(text(" Network "), vbox({
        hbox({
            text("Peers: ") | dim,
            text(peers_str),
        }),
        hbox({
            text("Traffic: ") | dim,
            text("↓ " + format_bytes(status_.bytes_received) + "/s") | color(Color::Green),
            text("  "),
            text("↑ " + format_bytes(status_.bytes_sent) + "/s") | color(Color::Blue),
        }),
    }));
}

Element tui_dashboard::render_mempool_panel() {
    return window(text(" Mempool "), hbox({
        text("Transactions: ") | dim,
        text(std::to_string(status_.mempool_tx_count)),
        text("  "),
        text("Size: ") | dim,
        text(format_bytes(status_.mempool_size_bytes)),
    }));
}

Element tui_dashboard::render_footer() {
    return hbox({
        text("Press ") | dim,
        text("q") | bold,
        text(" or ") | dim,
        text("ESC") | bold,
        text(" to exit") | dim,
    }) | hcenter;
}

std::string tui_dashboard::format_bytes(size_t bytes) const {
    std::ostringstream oss;
    if (bytes >= 1024 * 1024 * 1024) {
        oss << std::fixed << std::setprecision(1) << (bytes / (1024.0 * 1024.0 * 1024.0)) << " GB";
    } else if (bytes >= 1024 * 1024) {
        oss << std::fixed << std::setprecision(1) << (bytes / (1024.0 * 1024.0)) << " MB";
    } else if (bytes >= 1024) {
        oss << std::fixed << std::setprecision(1) << (bytes / 1024.0) << " KB";
    } else {
        oss << bytes << " B";
    }
    return oss.str();
}

std::string tui_dashboard::format_duration(std::chrono::seconds secs) const {
    auto total = secs.count();
    auto hours = total / 3600;
    auto minutes = (total % 3600) / 60;
    auto seconds = total % 60;

    std::ostringstream oss;
    if (hours > 0) {
        oss << hours << "h " << minutes << "m";
    } else if (minutes > 0) {
        oss << minutes << "m " << seconds << "s";
    } else {
        oss << seconds << "s";
    }
    return oss.str();
}

std::string tui_dashboard::state_to_string(node_status::sync_state state) const {
    switch (state) {
        case node_status::sync_state::starting:        return "Starting...";
        case node_status::sync_state::syncing_headers: return "Syncing Headers";
        case node_status::sync_state::syncing_blocks:  return "Syncing Blocks";
        case node_status::sync_state::synced:          return "Synced";
        case node_status::sync_state::error:           return "Error";
        default:                                       return "Unknown";
    }
}

tui_dashboard::ptr make_tui_dashboard() {
    return std::make_unique<tui_dashboard>();
}

} // namespace kth::node_exe
