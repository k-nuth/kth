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
#include <kth/node/user_agent.hpp>
#include <kth/domain/version.hpp>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>

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

// static auto const application_name = "kth";
static constexpr int initialize_stop = 0;
static constexpr int directory_exists = 0;
static constexpr int directory_not_found = 2;
static auto const mode = std::ofstream::out | std::ofstream::app;

std::promise<kth::code> executor::stopping_; //NOLINT

executor::executor(kth::node::configuration const& config, bool stdout_enabled /*= true*/)
    : stdout_enabled_(stdout_enabled)
    , config_(config)
{
#if ! defined(__EMSCRIPTEN__)
    auto& network = config_.network;
    auto const verbose = network.verbose;

    network.user_agent = get_user_agent();

    kth::log::initialize(network.debug_file.string(), network.error_file.string(), stdout_enabled, verbose);
#endif // ! defined(__EMSCRIPTEN__)
}

executor::~executor() {
    if (running_) {
        signal_stop();
    }
    // Ensure io_context is stopped and thread is joined
    io_context_.stop();
    if (io_thread_.joinable()) {
        io_thread_.join();
    }
}

void executor::print_version(std::string_view extra) {
    std::println(KTH_VERSION_MESSAGE, kth::version, extra, KTH_CURRENCY_SYMBOL_STR, KTH_MICROARCHITECTURE_STR, march_names());
}

#if ! defined(KTH_DB_READONLY)
bool executor::init_directory(error_code& ec) {
    auto const& directory = config_.database.directory;

    if (create_directories(directory, ec)) {
        spdlog::info("[node] {}", fmt::format(KTH_INITIALIZING_CHAIN, directory.string()));

        auto const genesis = kth::node::full_node::get_genesis_block(get_network(config_.network.identifier, config_.network.inbound_port == 48333));
        auto const& settings = config_.database;

        data_base db(settings);
        auto const result = db.create(genesis);

        if ( ! result ) {
            spdlog::info("[node] {}", KTH_INITCHAIN_FAILED);
            return false;
        }

        spdlog::info("[node] {}", KTH_INITCHAIN_COMPLETE);
        return true;
    }

    return false;
}

// CAPI
bool executor::do_initchain(std::string_view extra) {
    initialize_output(extra, config_.database.db_mode);

    error_code ec;

    if (init_directory(ec)) {
        return true;
    }

    auto const& directory = config_.database.directory;

    if (ec.value() == directory_exists) {
        spdlog::error("[node] {}", fmt::format(KTH_INITCHAIN_EXISTS, directory.string()));
        return false;
    }

    spdlog::error("[node] {}", fmt::format(KTH_INITCHAIN_NEW, directory.string(), ec.message()));
    return false;
}

#endif // ! defined(KTH_DB_READONLY)

kth::node::full_node& executor::node() {
    return *node_;
}

kth::node::full_node const& executor::node() const {
    return *node_;
}

// Close must be called from main thread.
bool executor::close() {
    spdlog::info("[node] {}", KTH_NODE_STOPPING);

    if (node_) {
        node_->stop();
        node_->join();
        spdlog::info("[node] {}", KTH_NODE_STOPPED);
    }

    // Stop the io_context and wait for the io thread to finish
    io_context_.stop();
    if (io_thread_.joinable()) {
        io_thread_.join();
    }

    spdlog::info("[node] {}", KTH_GOOD_BYE);
    running_ = false;
    return true;
}

// private
bool executor::wait_for_signal_and_close() {
    // Wait for stop. Ensure calling close from the main thread.
    stopping_.get_future().wait();
    return close();
}

#if ! defined(KTH_DB_READONLY)

error_code executor::init_directory_if_necessary() {
    if (verify_directory()) return error::success;

    error_code ec;
    if (init_directory(ec)) return error::success;

    return ec;
}

// Helper to run node in coroutine context
void executor::run_node_async(start_modules mod) {
    ::asio::co_spawn(io_context_, [this, mod]() -> ::asio::awaitable<void> {
        // Start the node
        auto start_ec = co_await node_->start();
        if (start_ec != error::success) {
            spdlog::error("[node] {}", fmt::format(KTH_NODE_START_FAIL, start_ec.message()));
            if (run_handler_) {
                run_handler_(start_ec);
            }
            co_return;
        }

        spdlog::info("[node] {}", KTH_NODE_SEEDED);

        // Run the node (only for full mode, not just_chain)
        if (mod != start_modules::just_chain) {
            auto run_ec = co_await node_->run();
            if (run_ec != error::success) {
                spdlog::error("[node] {}", fmt::format(KTH_NODE_START_FAIL, run_ec.message()));
                if (run_handler_) {
                    run_handler_(run_ec);
                }
                co_return;
            }
        }

        spdlog::info("[node] {}", KTH_NODE_STARTED);

        if (run_handler_) {
            run_handler_(error::success);
        }
    }, ::asio::detached);

    // Run the io_context in a background thread (saved for proper shutdown)
    io_thread_ = std::thread([this]() {
        io_context_.run();
    });
}

bool executor::init_run_and_wait_for_signal(std::string_view extra, start_modules mod, kth::handle0 handler) {
    run_handler_ = std::move(handler);

    initialize_output(extra, config_.database.db_mode);

    spdlog::info("[node] {}", KTH_NODE_INTERRUPT);
    spdlog::info("[node] {}", KTH_NODE_STARTING);

    auto ec = init_directory_if_necessary();
    if (ec != error::success) {
        auto const& directory = config_.database.directory;
        spdlog::error("[node] {}", fmt::format(KTH_INITCHAIN_NEW, directory.string(), ec.message()));

        if (run_handler_) {
            run_handler_(ec);
        }
        auto res = wait_for_signal_and_close();
        return false;
    }

    // Now that the directory is verified we can create the node for it.
    node_ = std::make_shared<kth::node::full_node>(config_);
    running_ = true;

    // Start and run the node asynchronously
    run_node_async(mod);

    auto res = wait_for_signal_and_close();
    return res;
}

bool executor::init_run(std::string_view extra, start_modules mod, kth::handle0 handler) {
    run_handler_ = std::move(handler);

    initialize_output(extra, config_.database.db_mode);

    spdlog::info("[node] {}", KTH_NODE_INTERRUPT);
    spdlog::info("[node] {}", KTH_NODE_STARTING);

    auto ec = init_directory_if_necessary();
    if (ec != error::success) {
        auto const& directory = config_.database.directory;
        spdlog::error("[node] {}", fmt::format(KTH_INITCHAIN_NEW, directory.string(), ec.message()));

        if (run_handler_) {
            run_handler_(ec);
        }
        return false;
    }

    // Now that the directory is verified we can create the node for it.
    node_ = std::make_shared<kth::node::full_node>(config_);
    running_ = true;

    // Start and run the node asynchronously
    run_node_async(mod);

    return true;
}

#endif // ! defined(KTH_DB_READONLY)

bool executor::stopped() const {
    return node_ ? node_->stopped() : true;
}

// Stop signal.
// ----------------------------------------------------------------------------

void executor::handle_stop(int code) {
    // Reinitialize after each capture to prevent hard shutdown.
    // Do not capture failure signals as calling stop can cause flush lock file
    // to clear due to the aborted thread dropping the flush lock mutex.
    std::signal(SIGINT, handle_stop);
    std::signal(SIGTERM, handle_stop);

    if (code == initialize_stop) {
        return;
    }

    spdlog::info("[node] {}", fmt::format(KTH_NODE_SIGNALED, code));
    stop(kth::error::success);
}

void executor::signal_stop() {
    stop(kth::code());
}

// Manage the race between console stop and server stop.
void executor::stop(kth::code const& ec) {
    static std::once_flag stop_mutex;
    std::call_once(stop_mutex, [&](){ stopping_.set_value(ec); });
}

// Utilities.
// ----------------------------------------------------------------------------

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
    // Center version under the slogan
    constexpr char slogan[] = "High Performance Bitcoin Cash Node";
    constexpr auto slogan_start = 10;
    auto version_text = std::format("v{}", kth::version);
    auto padding = slogan_start + (sizeof(slogan) - 1 - version_text.size()) / 2;
    std::println("{:>{}}", version_text, padding + version_text.size());
    std::println();
}

// Set up logging.
void executor::initialize_output(std::string_view extra, db_mode_type db_mode) {
    auto const& file = config_.file;

    if (stdout_enabled_) {
        print_ascii_art();
    }

    if (file.empty()) {
        spdlog::info("[node] {}", KTH_USING_DEFAULT_CONFIG);
    } else {
        spdlog::info("[node] {}", fmt::format(KTH_USING_CONFIG_FILE, file.string()));
    }

    std::string_view db_type_str;
    if (db_mode == db_mode_type::full) {
        db_type_str = KTH_DB_TYPE_FULL;
    } else if (db_mode == db_mode_type::blocks) {
        db_type_str = KTH_DB_TYPE_BLOCKS;
    } else if (db_mode == db_mode_type::pruned) {
        db_type_str = KTH_DB_TYPE_PRUNED;
    }

    spdlog::info("[node] {}", fmt::format(KTH_VERSION_MESSAGE_INIT, kth::version));
    spdlog::info("[node] {}", extra);
    spdlog::info("[node] {}", fmt::format(KTH_CRYPTOCURRENCY_INIT, KTH_CURRENCY_SYMBOL_STR, KTH_CURRENCY_STR));
    spdlog::info("[node] {}", fmt::format(KTH_MICROARCHITECTURE_INIT, KTH_MICROARCHITECTURE_STR));
    spdlog::info("[node] {}", fmt::format(KTH_MARCH_EXTS_INIT, march_names()));
    spdlog::info("[node] {}", fmt::format(KTH_DB_TYPE_INIT, db_type_str));

#ifndef NDEBUG
    spdlog::info("[node] {}", KTH_DEBUG_BUILD_INIT);
#endif

    spdlog::info("[node] {}", fmt::format(KTH_NETWORK_INIT, name(kth::get_network(config_.network.identifier, config_.network.inbound_port == 48333)), config_.network.identifier));
    spdlog::info("[node] {}", fmt::format(KTH_BLOCKCHAIN_CORES_INIT, kth::thread_ceiling(config_.chain.cores)));
    spdlog::info("[node] {}", fmt::format(KTH_NETWORK_CORES_INIT, kth::thread_ceiling(config_.network.threads)));
}

// Use missing directory as a sentinel indicating lack of initialization.
bool executor::verify_directory() {
    error_code ec;
    auto const& directory = config_.database.directory;

    if (exists(directory, ec)) {
        return true;
    }

    if (ec.value() == directory_not_found) {
        spdlog::error("[node] {}", fmt::format(KTH_UNINITIALIZED_CHAIN, directory.string()));
        return false;
    }

    auto const message = ec.message();
    spdlog::error("[node] {}", fmt::format(KTH_INITCHAIN_TRY, directory.string(), message));
    return false;
}

} // namespace kth::node
