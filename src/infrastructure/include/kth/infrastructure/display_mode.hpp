// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_DISPLAY_MODE_HPP
#define KTH_INFRASTRUCTURE_DISPLAY_MODE_HPP

#include <algorithm>
#include <cctype>
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

/// Parse display_mode from string (case-insensitive)
inline display_mode parse_display_mode(std::string_view str) noexcept {
    auto eq = [](std::string_view a, std::string_view b) {
        return a.size() == b.size() &&
            std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                return std::tolower(static_cast<unsigned char>(x)) ==
                       std::tolower(static_cast<unsigned char>(y));
            });
    };
    if (eq(str, "tui"))    return display_mode::tui;
    if (eq(str, "log"))    return display_mode::log;
    if (eq(str, "daemon")) return display_mode::daemon;
    return display_mode::log;  // default
}

} // namespace kth

#endif // KTH_INFRASTRUCTURE_DISPLAY_MODE_HPP
