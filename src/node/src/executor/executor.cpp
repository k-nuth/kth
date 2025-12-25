// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/executor/executor.hpp>

#include <csignal>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <print>
#include <thread>

#include <boost/core/null_deleter.hpp>

#include <kth/domain/multi_crypto_support.hpp>
#include <kth/node.hpp>
#include <kth/node/parser.hpp>
#include <kth/domain/version.hpp>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/signal_set.hpp>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace kth::node {

using namespace boost;
using namespace boost::system;
using namespace kd::chain;
using namespace kd::config;
using namespace kth::database;

#if ! defined(__EMSCRIPTEN__)
using namespace kth::network;
#endif

using namespace std::placeholders;
using boost::null_deleter;
using std::error_code;
using kth::database::data_base;
using std::placeholders::_1;

static constexpr int directory_exists = 0;
static constexpr int directory_not_found = 2;
static auto const mode = std::ofstream::out | std::ofstream::app;

// =============================================================================
// Construction / Destruction
// =============================================================================

executor::executor(kth::node::configuration const& config, bool stdout_enabled)
    : stdout_enabled_(stdout_enabled)
    , config_(config)
{
#if ! defined(__EMSCRIPTEN__)
    auto const& network = config_.network;
    kth::log::initialize(network.debug_file.string(), network.error_file.string(), stdout_enabled, network.verbose);
#endif // ! defined(__EMSCRIPTEN__)
}

executor::~executor() {
    // Ensure clean shutdown
    if (state_.load() == state::running) {
        stop();
    }
}

// =============================================================================
// IO Thread Management
// =============================================================================

void executor::start_io_thread() {
    // Create work guard to keep io_context alive even when there's no work
    work_guard_.emplace(io_context_.get_executor());

    // Start io_context in background thread
    io_thread_ = std::thread([this]() {
        io_context_.run();
    });
}

void executor::stop_io_thread() {
    // Release work guard to allow io_context to stop
    work_guard_.reset();

    // Stop io_context
    io_context_.stop();

    // Wait for thread to finish
    if (io_thread_.joinable()) {
        io_thread_.join();
    }
}

// =============================================================================
// Async Lifecycle
// =============================================================================

void executor::start_async(start_handler handler) {
    auto expected = state::stopped;
    if (!state_.compare_exchange_strong(expected, state::starting)) {
        if (handler) {
            handler(error::operation_failed);  // Already started/starting/stopping
        }
        return;
    }

    // Initialize output and directory
    initialize_output("", config_.database.db_mode);

    spdlog::info("[node] Press CTRL-C to stop the node.");
    spdlog::info("[node] Please wait while the node is starting...");

#if ! defined(KTH_DB_READONLY)
    auto ec = init_directory_if_necessary();
    if (ec != error::success) {
        auto const& directory = config_.database.directory;
        spdlog::error("[node] Failed to create directory {} with error, '{}'.", directory.string(), ec.message());
        if (handler) {
            handler(ec);
        }
        return;
    }
#endif

    // Create the node
    node_ = std::make_shared<kth::node::full_node>(config_);

    // Start IO thread
    start_io_thread();

    // Spawn the startup coroutine
    ::asio::co_spawn(io_context_, [this, handler]() -> ::asio::awaitable<void> {
        // Start the node
        auto start_ec = co_await node_->start();
        if (start_ec != error::success) {
            spdlog::error("[node] Node failed to start with error: {}.", start_ec.message());
            if (handler) {
                handler(start_ec);
            }
            // Notify sync version
            {
                std::lock_guard<std::mutex> lock(start_mutex_);
                start_result_ = start_ec;
            }
            start_cv_.notify_all();
            co_return;
        }

        spdlog::info("[node] Seeding is complete.");

        // Mark as running BEFORE calling run() so start() can return
        // run() blocks until the node is stopped, so we must notify first
        state_ = state::running;

        // Notify handler
        if (handler) {
            handler(error::success);
        }

        // Notify sync version so start() can return
        {
            std::lock_guard<std::mutex> lock(start_mutex_);
            start_result_ = error::success;
        }
        start_cv_.notify_all();

        spdlog::info("[node] Node is started.");

        // Run the node (starts P2P, sync, etc.)
        // This blocks until the node is stopped (via stop())
        auto run_ec = co_await node_->run();
        if (run_ec != error::success && run_ec != error::service_stopped) {
            spdlog::error("[node] Node run ended with error: {}.", run_ec.message());
        }

        spdlog::debug("[node] Node run() completed.");

    }, [this, handler](std::exception_ptr ep) {
        if (ep) {
            try {
                std::rethrow_exception(ep);
            } catch (std::exception const& e) {
                spdlog::error("[node] Startup exception: {}", e.what());
            }
            if (handler) {
                handler(error::operation_failed);
            }
            // Notify sync version
            {
                std::lock_guard<std::mutex> lock(start_mutex_);
                start_result_ = error::operation_failed;
            }
            start_cv_.notify_all();
        }
    });
}

void executor::stop_async(stop_handler handler) {
    auto expected = state::running;
    if (!state_.compare_exchange_strong(expected, state::stopping)) {
        // Not running or already stopping
        if (handler) {
            handler();
        }
        return;
    }

    spdlog::info("[node] Please wait while the node is stopping...");

    // Stop node
    if (node_) {
        node_->stop();
    }

    // Spawn cleanup coroutine
    ::asio::co_spawn(io_context_, [this, handler]() -> ::asio::awaitable<void> {
        // Wait for node to finish
        if (node_) {
            node_->join();
        }

        spdlog::info("[node] Node stopped successfully.");

        state_ = state::stopped;

        if (handler) {
            handler();
        }

        spdlog::info("[node] Good bye!");
        co_return;
    }, ::asio::detached);
}

// =============================================================================
// Sync Lifecycle
// =============================================================================

code executor::start() {
    std::unique_lock<std::mutex> lock(start_mutex_);

    // Start async
    start_async(nullptr);

    // Wait for completion with periodic wakeup
    // This allows external signal handlers to interrupt us
    while (!start_cv_.wait_for(lock, std::chrono::milliseconds(100), [this]() {
        return state_.load() == state::running || start_result_ != error::success;
    })) {
        // Check if we should abort (e.g., signal received)
        // The caller can check g_signal_received after start() returns
    }

    return start_result_;
}

void executor::stop() {
    if (state_.load() != state::running) {
        return;
    }

    std::promise<void> done_promise;
    auto done_future = done_promise.get_future();

    stop_async([&done_promise]() {
        done_promise.set_value();
    });

    // Wait for stop to complete
    done_future.wait();

    // Destroy node BEFORE stopping io_context
    // (node's components use io_context for timer cancellation, etc.)
    node_.reset();

    // Stop IO thread
    stop_io_thread();
}

// Global variable for signal handling (required for std::signal)
namespace {
    std::atomic<int> g_signal_received{0};
    std::atomic<bool> g_signal_waiting{false};

    void signal_handler(int signal_number) {
        // Immediate print to stderr (unbuffered) so user sees it right away
        // Note: fprintf is async-signal-safe, std::println is not
        std::fprintf(stderr, "\n[node] Signal %d received - initiating shutdown...\n", signal_number);
        std::fflush(stderr);
        g_signal_received.store(signal_number);
    }
}

void executor::wait_for_stop_signal() {
    // Mark that we're waiting for a signal
    g_signal_waiting.store(true);

    // Install signal handlers using std::signal (simpler and more reliable)
    auto prev_sigint = std::signal(SIGINT, signal_handler);
    auto prev_sigterm = std::signal(SIGTERM, signal_handler);

    // Poll for signal (simple and reliable)
    while (g_signal_received.load() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto signal_received = g_signal_received.load();
    spdlog::info("[node] Stop signal detected (code: {}).", signal_received);

    // Restore previous handlers
    std::signal(SIGINT, prev_sigint);
    std::signal(SIGTERM, prev_sigterm);
    g_signal_waiting.store(false);
}

// =============================================================================
// State
// =============================================================================

bool executor::running() const {
    return state_.load() == state::running;
}

bool executor::started() const {
    auto s = state_.load();
    return s == state::starting || s == state::running || s == state::stopping;
}

bool executor::stopped() const {
    return state_.load() == state::stopped;
}

// =============================================================================
// Node Access
// =============================================================================

kth::node::full_node& executor::node() {
    return *node_;
}

kth::node::full_node const& executor::node() const {
    return *node_;
}

// =============================================================================
// Initialization Helpers
// =============================================================================

#if ! defined(KTH_DB_READONLY)
bool executor::init_directory(error_code& ec) {
    auto const& directory = config_.database.directory;

    if (create_directories(directory, ec)) {
        spdlog::info("[node] Please wait while initializing {} directory...", directory.string());

        auto const genesis = kth::node::full_node::get_genesis_block(get_network(config_.network.identifier, config_.network.inbound_port == 48333));
        auto const& settings = config_.database;

        data_base db(settings);
        auto const result = db.create(genesis);

        if (!result) {
            spdlog::info("[node] Error creating database files.");
            return false;
        }

        spdlog::info("[node] Completed initialization.");
        return true;
    }

    return false;
}

bool executor::do_initchain(std::string_view extra) {
    initialize_output(extra, config_.database.db_mode);

    error_code ec;

    if (init_directory(ec)) {
        return true;
    }

    auto const& directory = config_.database.directory;

    if (ec.value() == directory_exists) {
        spdlog::error("[node] Failed because the directory {} already exists.", directory.string());
        return false;
    }

    spdlog::error("[node] Failed to create directory {} with error, '{}'.", directory.string(), ec.message());
    return false;
}

error_code executor::init_directory_if_necessary() {
    if (verify_directory()) return error::success;

    error_code ec;
    if (init_directory(ec)) return error::success;

    return ec;
}
#endif // ! defined(KTH_DB_READONLY)

bool executor::verify_directory() {
    error_code ec;
    auto const& directory = config_.database.directory;

    if (exists(directory, ec)) {
        return true;
    }

    if (ec.value() == directory_not_found) {
        spdlog::error("[node] The {} directory is not initialized, run: kth --initchain", directory.string());
        return false;
    }

    auto const message = ec.message();
    spdlog::error("[node] Failed to test directory {} with error, '{}'.", directory.string(), message);
    return false;
}

void executor::print_version(std::string_view extra) {
#ifdef NDEBUG
    std::println("Knuth Node\n  C++ lib v{}\n  {}\n  Currency: {}\n  Microarchitecture: {}\n  Built for CPU instructions/extensions: {}",
        kth::version, extra, KTH_CURRENCY_SYMBOL_STR, KTH_MICROARCHITECTURE_STR, march_names());
#else
    std::println("Knuth Node\n  C++ lib v{}\n  {}\n  Currency: {}\n  Microarchitecture: {}\n  Built for CPU instructions/extensions: {}\n  (Debug Build)",
        kth::version, extra, KTH_CURRENCY_SYMBOL_STR, KTH_MICROARCHITECTURE_STR, march_names());
#endif
}

void executor::print_ascii_art() {
    std::print(R"(    ...
    .-=*#%%=                            :-=+++*#:
    :+*%%%@=                            .:--#@@#.
       :%%@=                      .:.      :%@#.
       .%%@=                    .*%%-     :#@%:
       :%%@=       ..          .#@%=     .#@%-
       :%%@= .=###**+.     -+++#%%%***.  +@%=  :=+*+-
       :%%%-  :%%=:.       :--%@%*-::.  =%%= -+*=-%@@=
       :%%%: :*+.   .::.     =%@*.     :%@#:++:   +@@+
       :%%%*+#:    .#%%%=   -%@#.     .*%%%*:     *%@-
       -%%%@@%*-   :#@@%=  :#@#.      +@%%+      :%%%.
       =@%%-+%@%+.  .-=-  .#@%:.=*.  -%%%-      .#@%-
       -@%%. :*%@#-      .*@%+=#%-  :%@#: .--  .*@%-
     .:*@%%:   =%@@*-.   +@%%@%+.  .*@#.  *@@*=#@#:
    .*#####*: -*#####*.  *%%*=.    .**:   :*#%#+-.
    ........  ..  ....   ...                ..

          High Performance Bitcoin Cash Node
)");
    constexpr char slogan[] = "High Performance Bitcoin Cash Node";
    constexpr auto slogan_start = 10;
    auto version_text = std::format("v{}", kth::version);
    auto padding = slogan_start + (sizeof(slogan) - 1 - version_text.size()) / 2;
    std::println("{:>{}}", version_text, padding + version_text.size());
    std::println();
}

void executor::initialize_output(std::string_view extra, db_mode_type db_mode) {
    auto const& file = config_.file;

    if (stdout_enabled_) {
        print_ascii_art();
    }

    if (file.empty()) {
        spdlog::info("[node] Using default configuration settings.");
    } else {
        spdlog::info("[node] Using config file: {}", file.string());
    }

    std::string_view db_type_str;
    if (db_mode == db_mode_type::full) {
        db_type_str = KTH_DB_TYPE_FULL;
    } else if (db_mode == db_mode_type::blocks) {
        db_type_str = KTH_DB_TYPE_BLOCKS;
    } else if (db_mode == db_mode_type::pruned) {
        db_type_str = KTH_DB_TYPE_PRUNED;
    }

    spdlog::info("[node] Knuth v{}", kth::version);
    spdlog::info("[node] Currency: {} - {}.", KTH_CURRENCY_SYMBOL_STR, KTH_CURRENCY_STR);
    spdlog::info("[node] Optimized for microarchitecture: {}.", KTH_MICROARCHITECTURE_STR);
    spdlog::info("[node] Built for CPU instructions/extensions: {}.", march_names());
    spdlog::info("[node] Database type: {}.", db_type_str);

#ifndef NDEBUG
    spdlog::info("[node] (Debug Build)");
#endif

    auto const network_id = config_.network.identifier;
    auto const network_type = kth::get_network(network_id, config_.network.inbound_port == 48333);
    spdlog::info("[node] Network: {0} ({1} - {1:#x}).", name(network_type), network_id);
    spdlog::info("[node] Blockchain configured to use {} threads.", kth::thread_ceiling(config_.chain.cores));
    spdlog::info("[node] Networking configured to use {} threads.", kth::thread_ceiling(config_.network.threads));
}

} // namespace kth::node
