// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tui_dashboard.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace kth::node_exe {

using namespace ftxui;

// Color palette - Knuth purple + BCH green theme
namespace colors {
    // Knuth brand colors (purple/violet)
    auto const kth_purple = Color::RGB(75, 0, 130);
    auto const kth_violet = Color::RGB(100, 50, 180);
    auto const kth_light = Color::RGB(140, 100, 200);

    // BCH green
    auto const bch_green = Color::RGB(10, 193, 142);
    auto const bch_light = Color::RGB(78, 215, 175);

    // Standard colors
    auto const green = Color::RGB(76, 175, 80);
    auto const red = Color::RGB(244, 67, 54);
    auto const orange = Color::RGB(255, 152, 0);
    auto const blue = Color::RGB(33, 150, 243);
    auto const cyan = Color::RGB(0, 188, 212);
    auto const yellow = Color::RGB(255, 235, 59);
    auto const white = Color::RGB(255, 255, 255);
    auto const gray = Color::RGB(158, 158, 158);
    auto const dark_gray = Color::RGB(97, 97, 97);
}

// ASCII art logo lines (from executor::print_ascii_art)
static std::vector<std::string> const logo_lines = {
    R"(    ...                                         )",
    R"(    .-=*#%%=                            :-=+++*#:)",
    R"(    :+*%%%@=                            .:--#@@#.)",
    R"(       :%%@=                      .:.      :%@#. )",
    R"(       .%%@=                    .*%%-     :#@%:  )",
    R"(       :%%@=       ..          .#@%=     .#@%-   )",
    R"(       :%%@= .=###**+.     -+++#%%%***.  +@%=  :=+*+-)",
    R"(       :%%%-  :%%=:.       :--%@%*-::.  =%%= -+*=-%@@=)",
    R"(       :%%%: :*+.   .::.     =%@*.     :%@#:++:   +@@+)",
    R"(       :%%%*+#:    .#%%%=   -%@#.     .*%%%*:     *%@-)",
    R"(       -%%%@@%*-   :#@@%=  :#@#.      +@%%+      :%%%.)",
    R"(       =@%%-+%@%+.  .-=-  .#@%:.=*.  -%%%-      .#@%- )",
    R"(       -@%%. :*%@#-      .*@%+=#%-  :%@#: .--  .*@%-  )",
    R"(     .:*@%%:   =%@@*-.   +@%%@%+.  .*@#.  *@@*=#@#:   )",
    R"(    .*#####*: -*#####*.  *%%*=.    .**:   :*#%#+-.    )",
    R"(    ........  ..  ....   ...                ..       )",
};

static std::string const slogan = "High Performance Bitcoin Cash Node";

tui_dashboard::tui_dashboard() {
    status_.start_time = std::chrono::system_clock::now();
    splash_start_ = std::chrono::steady_clock::now();

    // Define navigable screens (after splash)
    navigable_screens_ = {
        screen_id::dashboard,
        screen_id::network,
        screen_id::blockchain,
        screen_id::mempool,
        screen_id::logs,
        screen_id::terminal,
    };

    // Initialize terminal with welcome message
    terminal_history_.push_back("");
    terminal_history_.push_back("    **** KNUTH 64 BASIC V2 ****");
    terminal_history_.push_back("");
    terminal_history_.push_back(" 64K RAM SYSTEM  38911 BASIC BYTES FREE");
    terminal_history_.push_back("");
    terminal_history_.push_back("READY.");
}

tui_dashboard::~tui_dashboard() {
    stop();
}

void tui_dashboard::start() {
    if (running_.exchange(true)) {
        return;
    }

    ui_thread_ = std::thread([this] {
        auto screen = ScreenInteractive::Fullscreen();
        screen_.store(&screen);

        auto renderer = Renderer([this] {
            return render();
        });

        // Refresh for animations (60fps during splash, 2fps after)
        std::atomic<bool> refresh_ui_continue{true};
        std::thread refresh_ui([&] {
            while (refresh_ui_continue && !exit_requested_) {
                auto delay = (current_screen_ == screen_id::splash) ?
                    std::chrono::milliseconds(50) :  // Fast for animation
                    std::chrono::milliseconds(500);  // Slow for dashboard
                std::this_thread::sleep_for(delay);
                screen.PostEvent(Event::Custom);
            }
        });

        // Handle input events
        auto component = CatchEvent(renderer, [&](Event event) {
            if (exit_requested_) {
                screen.Exit();
                return true;
            }

            // Handle goodbye screen - exit after delay
            if (show_goodbye_) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - goodbye_start_).count();
                if (elapsed > 2000) {  // 2 seconds goodbye
                    exit_requested_ = true;
                    screen.Exit();
                }
                return true;  // Block all events during goodbye
            }

            // Handle exit confirmation dialog
            if (show_exit_confirm_) {
                if (event == Event::Character('y') || event == Event::Character('Y') || event == Event::Return) {
                    show_exit_confirm_ = false;
                    show_goodbye_ = true;
                    goodbye_start_ = std::chrono::steady_clock::now();
                    return true;
                }
                if (event == Event::Character('n') || event == Event::Character('N') || event == Event::Escape) {
                    show_exit_confirm_ = false;
                    return true;
                }
                return true;  // Block other events while dialog is shown
            }

            // Skip splash on any key
            if (current_screen_ == screen_id::splash && !splash_complete_) {
                if (event.is_character() || event == Event::Return ||
                    event == Event::Escape || event == Event::ArrowLeft ||
                    event == Event::ArrowRight) {
                    splash_complete_ = true;
                    current_screen_ = screen_id::dashboard;
                    return true;
                }
            }

            // Terminal input handling
            if (current_screen_ == screen_id::terminal) {
                // Only ESC exits from terminal (not Q, since we need to type)
                if (event == Event::Escape) {
                    show_exit_confirm_ = true;
                    return true;
                }
                // Enter - process command
                if (event == Event::Return) {
                    terminal_process_command();
                    return true;
                }
                // Backspace
                if (event == Event::Backspace && !terminal_input_.empty()) {
                    terminal_input_.pop_back();
                    return true;
                }
                // Character input (convert to uppercase like C64)
                if (event.is_character()) {
                    char c = event.character()[0];
                    if (c >= 'a' && c <= 'z') {
                        c = c - 'a' + 'A';  // Uppercase
                    }
                    terminal_input_ += c;
                    return true;
                }
                // Arrow keys still work for navigation
                if (event == Event::ArrowRight) {
                    next_screen();
                    return true;
                }
                if (event == Event::ArrowLeft) {
                    prev_screen();
                    return true;
                }
                return false;
            }

            // Show exit confirmation (not in terminal mode)
            if (event == Event::Character('q') || event == Event::Character('Q') || event == Event::Escape) {
                show_exit_confirm_ = true;
                return true;
            }

            // Navigation (only after splash)
            if (current_screen_ != screen_id::splash) {
                if (event == Event::ArrowRight || event == Event::Character('l')) {
                    next_screen();
                    return true;
                }
                if (event == Event::ArrowLeft || event == Event::Character('h')) {
                    prev_screen();
                    return true;
                }
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

void tui_dashboard::next_screen() {
    auto it = std::find(navigable_screens_.begin(), navigable_screens_.end(), current_screen_);
    if (it != navigable_screens_.end()) {
        ++it;
        if (it == navigable_screens_.end()) {
            it = navigable_screens_.begin();
        }
        current_screen_ = *it;
    }
}

void tui_dashboard::prev_screen() {
    auto it = std::find(navigable_screens_.begin(), navigable_screens_.end(), current_screen_);
    if (it != navigable_screens_.end()) {
        if (it == navigable_screens_.begin()) {
            it = navigable_screens_.end();
        }
        --it;
        current_screen_ = *it;
    }
}

Element tui_dashboard::render() {
    std::lock_guard<std::mutex> lock(status_mutex_);

    Element screen_content;
    switch (current_screen_) {
        case screen_id::splash:
            screen_content = render_splash();
            break;
        case screen_id::dashboard:
            screen_content = render_dashboard();
            break;
        case screen_id::network:
            screen_content = render_network_screen();
            break;
        case screen_id::blockchain:
            screen_content = render_blockchain_screen();
            break;
        case screen_id::mempool:
            screen_content = render_mempool_screen();
            break;
        case screen_id::logs:
            screen_content = render_logs_screen();
            break;
        case screen_id::terminal:
            screen_content = render_terminal_screen();
            break;
        default:
            screen_content = render_dashboard();
            break;
    }

    // Show goodbye screen
    if (show_goodbye_) {
        return vbox({
            filler(),
            hbox({
                filler(),
                vbox({
                    text("") ,
                    text("  ╔═══════════════════════════════════════╗  ") | color(colors::kth_violet),
                    text("  ║                                       ║  ") | color(colors::kth_violet),
                    text("  ║           Goodbye!                    ║  ") | color(colors::bch_green) | bold,
                    text("  ║                                       ║  ") | color(colors::kth_violet),
                    text("  ║     Thanks for using Knuth Node       ║  ") | color(colors::white),
                    text("  ║                                       ║  ") | color(colors::kth_violet),
                    text("  ║           kth.cash                    ║  ") | color(colors::kth_light),
                    text("  ║                                       ║  ") | color(colors::kth_violet),
                    text("  ╚═══════════════════════════════════════╝  ") | color(colors::kth_violet),
                    text(""),
                }),
                filler(),
            }),
            filler(),
        }) | border | color(colors::kth_purple);
    }

    // Show exit confirmation dialog if requested
    if (show_exit_confirm_) {
        auto dialog = vbox({
            text(""),
            hbox({
                filler(),
                vbox({
                    text("┌───────────────────────────────────┐") | color(colors::kth_violet),
                    text("│                                   │") | color(colors::kth_violet),
                    text("│   Are you sure you want to exit?  │") | color(colors::white) | bold,
                    text("│                                   │") | color(colors::kth_violet),
                    hbox({
                        text("│         ") | color(colors::kth_violet),
                        text("[Y]") | color(colors::bch_green) | bold,
                        text(" Yes    ") | color(colors::gray),
                        text("[N]") | color(colors::red) | bold,
                        text(" No          │") | color(colors::gray),
                    }),
                    text("│                                   │") | color(colors::kth_violet),
                    text("└───────────────────────────────────┘") | color(colors::kth_violet),
                }),
                filler(),
            }),
            text(""),
        });

        return dbox({
            screen_content | dim,
            filler(),
            dialog | center,
        });
    }

    return screen_content;
}

Element tui_dashboard::render_splash() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - splash_start_).count();

    // Animation timing (80s style typewriter effect)
    int const char_delay_ms = 3;          // Time per character (fast)
    int const line_delay_ms = 30;         // Extra delay per line
    int const slogan_delay_ms = 300;      // Delay before slogan
    int const hold_delay_ms = 3500;       // Hold splash after animation
    int const total_chars = [&] {
        int c = 0;
        for (auto const& line : logo_lines) c += line.size();
        return c;
    }();
    int const logo_duration = total_chars * char_delay_ms + logo_lines.size() * line_delay_ms;
    int const total_duration = logo_duration + slogan_delay_ms + slogan.size() * char_delay_ms + hold_delay_ms;

    // Check if animation complete
    if (elapsed > total_duration) {
        splash_complete_ = true;
        current_screen_ = screen_id::dashboard;
        return render_dashboard();
    }

    Elements logo_elements;
    int chars_shown = 0;
    int time_cursor = 0;

    // Render logo with typewriter effect
    for (size_t i = 0; i < logo_lines.size(); ++i) {
        auto const& line = logo_lines[i];
        int line_start_time = time_cursor;
        int line_end_time = line_start_time + line.size() * char_delay_ms;

        if (elapsed < line_start_time) {
            // Line not started yet
            break;
        }

        int visible_chars = std::min(
            static_cast<int>(line.size()),
            static_cast<int>((elapsed - line_start_time) / char_delay_ms)
        );

        std::string visible_text = line.substr(0, visible_chars);

        // Color gradient based on line position
        Color line_color;
        float t = static_cast<float>(i) / logo_lines.size();
        if (t < 0.3f) {
            line_color = colors::kth_light;
        } else if (t < 0.7f) {
            line_color = colors::kth_violet;
        } else {
            line_color = colors::kth_purple;
        }

        // Add blinking cursor at end if still typing this line
        if (visible_chars < static_cast<int>(line.size()) && (elapsed / 100) % 2 == 0) {
            visible_text += "█";
        }

        logo_elements.push_back(text(visible_text) | color(line_color) | bold);

        time_cursor = line_end_time + line_delay_ms;
    }

    // Slogan with typewriter effect
    Element slogan_element = text("");
    int slogan_start = logo_duration + slogan_delay_ms;
    if (elapsed > slogan_start) {
        int slogan_elapsed = elapsed - slogan_start;
        int visible_chars = std::min(
            static_cast<int>(slogan.size()),
            slogan_elapsed / char_delay_ms
        );
        std::string visible_slogan = slogan.substr(0, visible_chars);

        // Blinking cursor
        if (visible_chars < static_cast<int>(slogan.size()) && (elapsed / 100) % 2 == 0) {
            visible_slogan += "█";
        }

        slogan_element = text(visible_slogan) | color(colors::bch_green) | bold;
    }

    // Decorative elements - 80s style
    std::string border_char = "═";
    int border_pulse = (elapsed / 200) % 4;
    Color border_color = (border_pulse == 0) ? colors::kth_purple :
                         (border_pulse == 1) ? colors::kth_violet :
                         (border_pulse == 2) ? colors::kth_light :
                         colors::bch_green;

    // Skip hint
    Element skip_hint = (elapsed > 1000) ?
        (text("Press any key to skip...") | color(colors::gray) | dim) :
        text("");

    return vbox({
        filler(),
        hbox({
            filler(),
            vbox(logo_elements),
            filler(),
        }),
        text(""),
        hbox({
            filler(),
            slogan_element,
            filler(),
        }),
        text(""),
        text(""),
        hbox({
            filler(),
            skip_hint,
            filler(),
        }),
        filler(),
    }) | border | color(border_color);
}

Element tui_dashboard::render_dashboard() {
    return vbox({
        render_header_bar(),
        separator() | color(colors::dark_gray),
        hbox({
            vbox({
                render_blockchain_panel(),
                render_sync_panel(),
            }) | flex,
            separator() | color(colors::dark_gray),
            vbox({
                render_network_panel(),
                render_mempool_panel(),
                render_mining_panel(),
            }) | flex,
        }) | flex,
        separator() | color(colors::dark_gray),
        render_navigation_bar(),
        render_footer(),
    }) | border | color(colors::kth_purple);
}

Element tui_dashboard::render_network_screen() {
    return render_placeholder("NETWORK");
}

Element tui_dashboard::render_blockchain_screen() {
    return render_placeholder("BLOCKCHAIN");
}

Element tui_dashboard::render_mempool_screen() {
    return render_placeholder("MEMPOOL");
}

Element tui_dashboard::render_logs_screen() {
    return render_placeholder("LOGS");
}

Element tui_dashboard::render_terminal_screen() {
    // C64 color scheme
    auto const c64_blue = Color::RGB(64, 50, 133);      // C64 blue background
    auto const c64_light_blue = Color::RGB(134, 122, 222);  // C64 light blue text

    // Calculate how many lines we can show
    size_t const visible_lines = 20;

    // Get the lines to display (last N lines from history)
    Elements lines;
    size_t start_idx = terminal_history_.size() > visible_lines ?
                       terminal_history_.size() - visible_lines : 0;

    for (size_t i = start_idx; i < terminal_history_.size(); ++i) {
        lines.push_back(text(terminal_history_[i]) | color(c64_light_blue));
    }

    // Current input line with blinking cursor
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    bool cursor_visible = (ms / 500) % 2 == 0;

    std::string input_line = terminal_input_;
    if (cursor_visible) {
        input_line += "\u2588";  // Block cursor
    } else {
        input_line += " ";
    }
    lines.push_back(text(input_line) | color(c64_light_blue));

    // Fill remaining space
    while (lines.size() < visible_lines + 1) {
        lines.insert(lines.begin(), text(""));
    }

    return vbox({
        // C64 style header
        hbox({
            filler(),
            text("    **** KNUTH 64 BASIC V2 ****    ") | bold | color(c64_light_blue),
            filler(),
        }) | bgcolor(c64_blue),
        text("") | bgcolor(c64_blue),
        // Terminal content
        vbox(lines) | flex | bgcolor(c64_blue),
        text("") | bgcolor(c64_blue),
        separator() | color(colors::dark_gray),
        render_navigation_bar(),
        hbox({
            filler(),
            text("Type commands. Press ") | color(colors::gray),
            text("ESC") | bold | color(colors::bch_green),
            text(" to show exit dialog.") | color(colors::gray),
            filler(),
        }) | size(HEIGHT, EQUAL, 1),
    }) | border | color(c64_blue);
}

Element tui_dashboard::render_placeholder(std::string const& title) {
    return vbox({
        render_header_bar(),
        separator() | color(colors::dark_gray),
        filler(),
        hbox({
            filler(),
            vbox({
                text("┌─────────────────────────────┐") | color(colors::kth_violet),
                text("│                             │") | color(colors::kth_violet),
                text("│    " + title + " SCREEN    ") | color(colors::kth_light) | bold,
                text("│                             │") | color(colors::kth_violet),
                text("│    Coming soon...           │") | color(colors::gray),
                text("│                             │") | color(colors::kth_violet),
                text("└─────────────────────────────┘") | color(colors::kth_violet),
            }),
            filler(),
        }),
        filler(),
        separator() | color(colors::dark_gray),
        render_navigation_bar(),
        render_footer(),
    }) | border | color(colors::kth_purple);
}

Element tui_dashboard::render_header_bar() {
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now() - status_.start_time);

    auto state_str = state_to_string(status_.state);
    auto state_col = state_color(status_.state);

    std::string status_icon;
    switch (status_.state) {
        case node_status::sync_state::synced:     status_icon = "● "; break;
        case node_status::sync_state::error:      status_icon = "✖ "; break;
        case node_status::sync_state::starting:
        case node_status::sync_state::connecting: status_icon = "◐ "; break;
        default:                                  status_icon = "◉ "; break;
    }

    return hbox({
        text(" Knuth ") | bold | color(colors::kth_violet),
        text("v" + status_.version) | color(colors::gray) | dim,
        text("  "),
        separator() | color(colors::dark_gray),
        text("  "),
        text(status_icon) | color(state_col) | bold,
        text(state_str) | color(state_col) | bold,
        filler(),
        text("⛓ ") | color(colors::cyan),
        text(status_.network_name) | color(colors::cyan) | bold,
        text("  "),
        separator() | color(colors::dark_gray),
        text("  "),
        text("Height: ") | color(colors::gray),
        text(format_number(status_.chain_height)) | color(colors::white) | bold,
        text("  "),
        separator() | color(colors::dark_gray),
        text("  "),
        text("⏱ ") | color(colors::gray),
        text(format_duration(uptime)) | color(colors::white),
        text(" "),
    }) | size(HEIGHT, EQUAL, 1);
}

Element tui_dashboard::render_navigation_bar() {
    Elements tabs;

    for (auto const& scr : navigable_screens_) {
        bool is_current = (scr == current_screen_);
        auto name = screen_name(scr);

        if (is_current) {
            tabs.push_back(text(" " + name + " ") | bold | color(colors::kth_purple) | inverted);
        } else {
            tabs.push_back(text(" " + name + " ") | color(colors::gray));
        }
        tabs.push_back(text(" "));
    }

    return hbox({
        text(" ◀ ") | color(colors::kth_light),
        hbox(tabs),
        text(" ▶ ") | color(colors::kth_light),
        filler(),
        text("← → navigate") | color(colors::dark_gray) | dim,
        text(" "),
    }) | size(HEIGHT, EQUAL, 1);
}

Element tui_dashboard::render_blockchain_panel() {
    Elements rows;

    rows.push_back(hbox({
        text("█ ") | color(colors::bch_green),
        text("BLOCK HEIGHT") | bold | color(colors::bch_green),
    }));

    rows.push_back(hbox({
        text("   "),
        text(format_number(status_.chain_height)) | bold | color(colors::white) | size(WIDTH, GREATER_THAN, 10),
        status_.target_height > 0 ?
            text(" / " + format_number(status_.target_height)) | color(colors::gray) :
            text(""),
    }));

    rows.push_back(text(""));

    if (!status_.best_block_hash.empty()) {
        rows.push_back(hbox({
            text("   Hash: ") | color(colors::gray),
            text(truncate_hash(status_.best_block_hash, 24) + "...") | color(colors::cyan),
        }));
    }

    if (status_.difficulty > 0) {
        rows.push_back(hbox({
            text("   Difficulty: ") | color(colors::gray),
            text(format_difficulty(status_.difficulty)) | color(colors::orange),
        }));
    }

    if (status_.last_block_time != std::chrono::system_clock::time_point{}) {
        rows.push_back(hbox({
            text("   Last block: ") | color(colors::gray),
            text(format_time_ago(status_.last_block_time)) | color(colors::white),
        }));
    }

    return window(text(" ⛏ BLOCKCHAIN ") | bold | color(colors::bch_green), vbox(rows) | flex) | flex;
}

Element tui_dashboard::render_sync_panel() {
    Elements rows;

    if (status_.headers_total > 0) {
        float progress = static_cast<float>(status_.headers_synced) /
                        static_cast<float>(status_.headers_total);
        progress = std::clamp(progress, 0.0f, 1.0f);
        int pct = static_cast<int>(progress * 100);
        bool done = status_.headers_synced >= status_.headers_total;

        rows.push_back(hbox({
            text("Headers: ") | color(colors::gray),
            text(format_number(status_.headers_synced)) | color(done ? colors::green : colors::blue),
            text(" / ") | color(colors::dark_gray),
            text(format_number(status_.headers_total)) | color(colors::gray),
            text("  "),
            text(std::to_string(pct) + "%") | color(done ? colors::green : colors::blue),
            done ? text(" ✓") | color(colors::green) : text(""),
        }));

        rows.push_back(hbox({
            gauge(progress) | flex | color(done ? colors::green : colors::blue),
        }));

        if (!done && status_.headers_per_second > 0) {
            rows.push_back(hbox({
                text("   ") | color(colors::gray),
                text("↓ " + format_number(status_.headers_per_second) + "/s") | color(colors::cyan),
            }));
        }

        rows.push_back(text(""));
    }

    if (status_.blocks_total > 0) {
        float progress = static_cast<float>(status_.blocks_synced) /
                        static_cast<float>(status_.blocks_total);
        progress = std::clamp(progress, 0.0f, 1.0f);
        int pct = static_cast<int>(progress * 100);
        bool done = status_.blocks_synced >= status_.blocks_total;

        rows.push_back(hbox({
            text("Blocks:  ") | color(colors::gray),
            text(format_number(status_.blocks_synced)) | color(done ? colors::green : colors::orange),
            text(" / ") | color(colors::dark_gray),
            text(format_number(status_.blocks_total)) | color(colors::gray),
            text("  "),
            text(std::to_string(pct) + "%") | color(done ? colors::green : colors::orange),
            done ? text(" ✓") | color(colors::green) : text(""),
        }));

        rows.push_back(hbox({
            gauge(progress) | flex | color(done ? colors::green : colors::orange),
        }));

        if (!done) {
            Elements speed_row;
            speed_row.push_back(text("   ") | color(colors::gray));
            if (status_.blocks_per_second > 0) {
                speed_row.push_back(text("↓ " + format_number(status_.blocks_per_second) + "/s") | color(colors::cyan));
            }
            if (status_.blocks_eta.count() > 0) {
                speed_row.push_back(text("  ETA: ") | color(colors::gray));
                speed_row.push_back(text(format_duration(status_.blocks_eta)) | color(colors::white));
            }
            if (!speed_row.empty()) {
                rows.push_back(hbox(speed_row));
            }
        }
    }

    if (rows.empty()) {
        rows.push_back(text("Waiting for sync data...") | color(colors::gray) | dim);
    }

    return window(text(" ⟳ SYNCHRONIZATION ") | bold | color(colors::kth_violet), vbox(rows) | flex) | flex;
}

Element tui_dashboard::render_network_panel() {
    size_t total_peers = status_.peers_inbound + status_.peers_outbound;

    Elements rows;

    rows.push_back(hbox({
        text("⬡ ") | color(total_peers > 0 ? colors::green : colors::red),
        text("Peers: ") | color(colors::gray),
        text(std::to_string(total_peers)) | bold | color(total_peers > 0 ? colors::green : colors::red),
        text("  (") | color(colors::dark_gray),
        text("↑" + std::to_string(status_.peers_outbound)) | color(colors::cyan),
        text(" ↓" + std::to_string(status_.peers_inbound)) | color(colors::kth_violet),
        text(")") | color(colors::dark_gray),
    }));

    rows.push_back(text(""));

    rows.push_back(hbox({
        text("↓ ") | color(colors::green) | bold,
        text(format_bytes_speed(status_.bytes_received)) | color(colors::green),
        text("  "),
        text("↑ ") | color(colors::blue) | bold,
        text(format_bytes_speed(status_.bytes_sent)) | color(colors::blue),
    }));

    if (status_.total_bytes_received > 0 || status_.total_bytes_sent > 0) {
        rows.push_back(hbox({
            text("Total: ") | color(colors::gray),
            text("↓" + format_bytes(status_.total_bytes_received)) | color(colors::green) | dim,
            text(" ↑" + format_bytes(status_.total_bytes_sent)) | color(colors::blue) | dim,
        }));
    }

    if (status_.avg_ping_ms > 0) {
        rows.push_back(hbox({
            text("Ping: ") | color(colors::gray),
            text(std::to_string(status_.avg_ping_ms) + "ms") | color(
                status_.avg_ping_ms < 100 ? colors::green :
                status_.avg_ping_ms < 500 ? colors::orange : colors::red),
        }));
    }

    return window(text(" ⚡ NETWORK ") | bold | color(colors::cyan), vbox(rows) | flex) | flex;
}

Element tui_dashboard::render_mempool_panel() {
    Elements rows;

    rows.push_back(hbox({
        text("Transactions: ") | color(colors::gray),
        text(format_number(status_.mempool_tx_count)) | bold | color(colors::kth_violet),
    }));

    rows.push_back(hbox({
        text("Size: ") | color(colors::gray),
        text(format_bytes(status_.mempool_size_bytes)) | color(colors::white),
    }));

    if (status_.mempool_avg_fee > 0) {
        rows.push_back(hbox({
            text("Avg fee: ") | color(colors::gray),
            text(std::to_string(static_cast<int>(status_.mempool_avg_fee)) + " sat/B") | color(colors::orange),
        }));
    }

    if (status_.mempool_min_fee > 0) {
        rows.push_back(hbox({
            text("Min fee: ") | color(colors::gray),
            text(std::to_string(static_cast<int>(status_.mempool_min_fee)) + " sat/B") | color(colors::green),
        }));
    }

    return window(text(" 📦 MEMPOOL ") | bold | color(colors::kth_violet), vbox(rows) | flex) | flex;
}

Element tui_dashboard::render_mining_panel() {
    Elements rows;

    if (status_.network_hashrate > 0) {
        rows.push_back(hbox({
            text("Network Hashrate: ") | color(colors::gray),
            text(format_hashrate(status_.network_hashrate)) | bold | color(colors::bch_green),
        }));
    }

    if (status_.blocks_until_retarget > 0) {
        rows.push_back(hbox({
            text("Next retarget: ") | color(colors::gray),
            text(format_number(status_.blocks_until_retarget) + " blocks") | color(colors::orange),
        }));
    }

    if (status_.blocks_until_halving > 0) {
        rows.push_back(hbox({
            text("Next halving: ") | color(colors::gray),
            text(format_number(status_.blocks_until_halving) + " blocks") | color(colors::bch_green),
        }));
    }

    if (rows.empty()) {
        rows.push_back(text("Mining data pending...") | color(colors::gray) | dim);
    }

    return window(text(" ⛏ MINING ") | bold | color(colors::bch_green), vbox(rows) | flex) | flex;
}

Element tui_dashboard::render_footer() {
    return hbox({
        filler(),
        text("Press ") | color(colors::gray),
        text("Q") | bold | color(colors::bch_green),
        text(" or ") | color(colors::gray),
        text("ESC") | bold | color(colors::bch_green),
        text(" to exit") | color(colors::gray),
        text("  │  ") | color(colors::dark_gray),
        text("kth.cash") | color(colors::kth_violet) | bold,
        filler(),
    }) | size(HEIGHT, EQUAL, 1);
}

// Helper implementations

std::string tui_dashboard::format_bytes(size_t bytes) const {
    if (bytes >= 1024ULL * 1024 * 1024 * 1024) {
        return std::format("{:.2f} TB", bytes / (1024.0 * 1024.0 * 1024.0 * 1024.0));
    }
    if (bytes >= 1024ULL * 1024 * 1024) {
        return std::format("{:.2f} GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }
    if (bytes >= 1024 * 1024) {
        return std::format("{:.1f} MB", bytes / (1024.0 * 1024.0));
    }
    if (bytes >= 1024) {
        return std::format("{:.1f} KB", bytes / 1024.0);
    }
    return std::format("{} B", bytes);
}

std::string tui_dashboard::format_bytes_speed(size_t bytes_per_sec) const {
    return format_bytes(bytes_per_sec) + "/s";
}

std::string tui_dashboard::format_duration(std::chrono::seconds secs) const {
    auto total = secs.count();
    auto days = total / 86400;
    auto hours = (total % 86400) / 3600;
    auto minutes = (total % 3600) / 60;
    auto seconds = total % 60;

    return std::format("{:03}d {:02}h {:02}m {:02}s", days, hours, minutes, seconds);
}

std::string tui_dashboard::format_time_ago(std::chrono::system_clock::time_point tp) const {
    auto now = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - tp);
    return format_duration(diff) + " ago";
}

std::string tui_dashboard::format_hashrate(double hashrate) const {
    if (hashrate >= 1e18) {
        return std::format("{:.2f} EH/s", hashrate / 1e18);
    }
    if (hashrate >= 1e15) {
        return std::format("{:.2f} PH/s", hashrate / 1e15);
    }
    if (hashrate >= 1e12) {
        return std::format("{:.2f} TH/s", hashrate / 1e12);
    }
    if (hashrate >= 1e9) {
        return std::format("{:.2f} GH/s", hashrate / 1e9);
    }
    if (hashrate >= 1e6) {
        return std::format("{:.2f} MH/s", hashrate / 1e6);
    }
    return std::format("{:.0f} H/s", hashrate);
}

std::string tui_dashboard::format_difficulty(double diff) const {
    if (diff >= 1e15) {
        return std::format("{:.2f}P", diff / 1e15);
    }
    if (diff >= 1e12) {
        return std::format("{:.2f}T", diff / 1e12);
    }
    if (diff >= 1e9) {
        return std::format("{:.2f}G", diff / 1e9);
    }
    if (diff >= 1e6) {
        return std::format("{:.2f}M", diff / 1e6);
    }
    if (diff >= 1e3) {
        return std::format("{:.2f}K", diff / 1e3);
    }
    return std::format("{:.2f}", diff);
}

std::string tui_dashboard::format_number(size_t num) const {
    auto s = std::to_string(num);
    std::string result;
    result.reserve(s.size() + s.size() / 3);
    int count = 0;
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
        if (count > 0 && count % 3 == 0) {
            result.insert(result.begin(), ',');
        }
        result.insert(result.begin(), *it);
        ++count;
    }
    return result;
}

std::string tui_dashboard::state_to_string(node_status::sync_state state) const {
    switch (state) {
        case node_status::sync_state::starting:        return "STARTING";
        case node_status::sync_state::connecting:      return "CONNECTING";
        case node_status::sync_state::syncing_headers: return "SYNCING HEADERS";
        case node_status::sync_state::syncing_blocks:  return "SYNCING BLOCKS";
        case node_status::sync_state::synced:          return "SYNCED";
        case node_status::sync_state::error:           return "ERROR";
        default:                                       return "UNKNOWN";
    }
}

Color tui_dashboard::state_color(node_status::sync_state state) const {
    switch (state) {
        case node_status::sync_state::synced:          return colors::bch_green;
        case node_status::sync_state::error:           return colors::red;
        case node_status::sync_state::syncing_headers:
        case node_status::sync_state::syncing_blocks:  return colors::kth_violet;
        case node_status::sync_state::connecting:      return colors::cyan;
        default:                                       return colors::gray;
    }
}

std::string tui_dashboard::truncate_hash(std::string const& hash, size_t len) const {
    if (hash.length() <= len) return hash;
    return hash.substr(0, len);
}

std::string tui_dashboard::screen_name(screen_id id) const {
    switch (id) {
        case screen_id::splash:     return "SPLASH";
        case screen_id::dashboard:  return "DASHBOARD";
        case screen_id::network:    return "NETWORK";
        case screen_id::blockchain: return "BLOCKCHAIN";
        case screen_id::mempool:    return "MEMPOOL";
        case screen_id::logs:       return "LOGS";
        case screen_id::terminal:   return "TERMINAL";
        default:                    return "UNKNOWN";
    }
}

void tui_dashboard::terminal_add_output(std::string const& line) {
    terminal_history_.push_back(line);
    // Limit history size
    while (terminal_history_.size() > terminal_history_max_) {
        terminal_history_.erase(terminal_history_.begin());
    }
}

void tui_dashboard::terminal_process_command() {
    // Add command to history
    terminal_add_output(terminal_input_);

    // Parse and execute command
    std::string cmd = terminal_input_;
    terminal_input_.clear();

    // Convert to uppercase for comparison
    std::string cmd_upper;
    for (char c : cmd) {
        cmd_upper += (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
    }

    // Simple command processing
    if (cmd_upper.empty()) {
        // Empty command, just show READY
    } else if (cmd_upper == "HELP" || cmd_upper == "?") {
        terminal_add_output("");
        terminal_add_output("AVAILABLE COMMANDS:");
        terminal_add_output("  HELP     - SHOW THIS HELP");
        terminal_add_output("  STATUS   - SHOW NODE STATUS");
        terminal_add_output("  PEERS    - SHOW PEER COUNT");
        terminal_add_output("  HEIGHT   - SHOW BLOCK HEIGHT");
        terminal_add_output("  VERSION  - SHOW VERSION");
        terminal_add_output("  CLEAR    - CLEAR SCREEN");
        terminal_add_output("  SYS      - SHOW SYSTEM INFO");
        terminal_add_output("");
    } else if (cmd_upper == "STATUS") {
        terminal_add_output("");
        terminal_add_output("NODE STATUS: " + state_to_string(status_.state));
        terminal_add_output("");
    } else if (cmd_upper == "PEERS") {
        terminal_add_output("");
        terminal_add_output(std::format("PEERS: {} OUTBOUND, {} INBOUND",
            status_.peers_outbound, status_.peers_inbound));
        terminal_add_output("");
    } else if (cmd_upper == "HEIGHT") {
        terminal_add_output("");
        terminal_add_output(std::format("BLOCK HEIGHT: {}", status_.chain_height));
        terminal_add_output("");
    } else if (cmd_upper == "VERSION") {
        terminal_add_output("");
        terminal_add_output("KNUTH NODE V" + status_.version);
        terminal_add_output("");
    } else if (cmd_upper == "CLEAR" || cmd_upper == "CLS") {
        terminal_history_.clear();
        terminal_history_.push_back("");
        terminal_history_.push_back("    **** KNUTH 64 BASIC V2 ****");
        terminal_history_.push_back("");
        terminal_history_.push_back(" 64K RAM SYSTEM  38911 BASIC BYTES FREE");
        terminal_history_.push_back("");
    } else if (cmd_upper == "SYS") {
        terminal_add_output("");
        terminal_add_output(std::format("MEMORY: {} BYTES", status_.memory_usage));
        terminal_add_output(std::format("CPU: {:.1f}%", status_.cpu_usage));
        terminal_add_output(std::format("DB CACHE: {} BYTES", status_.db_cache_size));
        terminal_add_output("");
    } else if (cmd_upper.substr(0, 4) == "LOAD" || cmd_upper.substr(0, 3) == "RUN" ||
               cmd_upper.substr(0, 4) == "LIST" || cmd_upper.substr(0, 4) == "POKE" ||
               cmd_upper.substr(0, 4) == "PEEK") {
        terminal_add_output("");
        terminal_add_output("?SYNTAX ERROR");
        terminal_add_output("  (JUST KIDDING, THIS IS NOT A REAL C64)");
        terminal_add_output("");
    } else {
        terminal_add_output("");
        terminal_add_output("?SYNTAX ERROR");
        terminal_add_output("");
    }

    terminal_add_output("READY.");
}

tui_dashboard::ptr make_tui_dashboard() {
    return std::make_unique<tui_dashboard>();
}

} // namespace kth::node_exe
