// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_DISPLAY_MODE_HPP
#define KTH_INFRASTRUCTURE_DISPLAY_MODE_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace kth {

/// Display mode for the node executable
/// Controls how output is shown to the user
enum class display_mode : uint8_t {
    /// TUI dashboard with real-time status
    tui = 0,

    /// Traditional scrolling log output (default)
    log = 1,

    /// Daemon mode - minimal console output (for systemd/background)
    daemon = 2
};

/// Convert display_mode to string
constexpr std::string_view to_string(display_mode mode) noexcept {
    switch (mode) {
        case display_mode::tui:    return "tui";
        case display_mode::log:    return "log";
        case display_mode::daemon: return "daemon";
        default:                   return "unknown";
    }
}

/// Parse display_mode from string
inline display_mode parse_display_mode(std::string_view str) noexcept {
    if (str == "tui" || str == "TUI")       return display_mode::tui;
    if (str == "log" || str == "LOG")       return display_mode::log;
    if (str == "daemon" || str == "DAEMON") return display_mode::daemon;
    return display_mode::log;  // default
}

} // namespace kth

#endif // KTH_INFRASTRUCTURE_DISPLAY_MODE_HPP
