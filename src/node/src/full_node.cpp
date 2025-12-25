// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/full_node.hpp>

#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>

#include <kth/blockchain.hpp>
#include <kth/node/configuration.hpp>
#include <kth/node/define.hpp>
#include <kth/node/sync_session.hpp>
#include <kth/node/user_agent.hpp>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/experimental/awaitable_operators.hpp>

namespace kth::node {

using namespace kth::blockchain;
using namespace kth::domain::chain;
using namespace kth::domain::config;
using namespace ::asio::experimental::awaitable_operators;

#if ! defined(__EMSCRIPTEN__)
using namespace kth::network;
#endif

// =============================================================================
// Construction
// =============================================================================

full_node::full_node(configuration const& configuration)
#if ! defined(__EMSCRIPTEN__)
    : multi_crypto_setter(configuration.network)
    , node_settings_(configuration.node)
    , chain_settings_(configuration.chain)
    , network_type_(get_network(configuration.network.identifier, configuration.network.inbound_port == 48333))
    , chain_thread_pool_(configuration.chain.cores)
    , network_settings_(configuration.network)
    , network_(network_settings_)
    , chain_(
        chain_thread_pool_,
        configuration.chain,
        configuration.database,
        network_type_,
        configuration.network.relay_transactions
    )
#else
    : multi_crypto_setter()
    , node_settings_(configuration.node)
    , chain_settings_(configuration.chain)
    , network_type_(domain::config::network::mainnet)
    , chain_thread_pool_(std::thread::hardware_concurrency())
    , chain_(
        chain_thread_pool_,
        configuration.chain,
        configuration.database,
        network_type_
    )
#endif
{
#if ! defined(__EMSCRIPTEN__)
    spdlog::debug("[full_node] configuration.network.threads = {}", configuration.network.threads);
    spdlog::debug("[full_node] network_settings_.threads = {}", network_settings_.threads);

    // Set user agent after initialization (network_ holds reference to network_settings_)
    std::vector<std::string> features;
#if defined(KTH_CURRENCY_BCH)
    features.push_back(format_eb(configuration.chain.default_consensus_block_size));
#endif
    network_settings_.user_agent = get_user_agent(features);
#endif
}

full_node::~full_node() {
    // Only stop/join if not already stopped
    if (!stopped()) {
        stop();
        join();
    }
}

// =============================================================================
// Lifecycle
// =============================================================================

::asio::awaitable<code> full_node::start() {
#if ! defined(__EMSCRIPTEN__)
    if (!stopped()) {
        co_return error::operation_failed;
    }
#endif

    // Start the blockchain
    if (!chain_.start()) {
        spdlog::error("[node] Failure starting blockchain.");
        co_return error::operation_failed;
    }

#if ! defined(__EMSCRIPTEN__)
    // Start the P2P network
    auto ec = co_await network_.start();
    if (ec != error::success) {
        spdlog::error("[node] Failure starting network: {}", ec.message());
        (void)chain_.stop();
        co_return ec;
    }
#endif

    spdlog::info("[node] Node started successfully.");
    co_return error::success;
}

::asio::awaitable<code> full_node::run() {
#if ! defined(__EMSCRIPTEN__)
    if (stopped()) {
        co_return error::service_stopped;
    }
#endif

    // Get current chain heights
    auto const heights = chain_.get_last_heights();
    if ( ! heights) {
        spdlog::error("[node] The blockchain is corrupt.");
        co_return error::operation_failed;
    }
    auto const [header_height, block_height] = *heights;

    auto const top_hash = chain_.get_block_hash(block_height);
    if ( ! top_hash) {
        spdlog::error("[node] The blockchain is corrupt.");
        co_return error::operation_failed;
    }

#if ! defined(__EMSCRIPTEN__)
    network_.set_top_block({*top_hash, block_height});
#endif

    spdlog::info("[node] Node start heights: header-sync ({}), block-sync ({}).", header_height, block_height);

#if ! defined(__EMSCRIPTEN__)
    // Run all background tasks in parallel using structured concurrency.
    // This blocks until ALL tasks complete (i.e., until stop() is called).
    // No detached coroutines - everything is properly awaited.
    spdlog::debug("[full_node] Starting parallel tasks with && operator");
    co_await (
        run_blockchain_subscriber() &&
        network_.run() &&
        run_sync()
    );
    spdlog::debug("[full_node] All parallel tasks completed");
#else
    // WASM: only blockchain subscriber (no network)
    co_await run_blockchain_subscriber();
#endif

    co_return error::success;
}

::asio::awaitable<void> full_node::run_blockchain_subscriber() {
    spdlog::debug("[full_node] run_blockchain_subscriber() starting");
    auto blockchain_channel = subscribe_blockchain();
    if (!blockchain_channel) {
        spdlog::debug("[full_node] run_blockchain_subscriber() - no channel, exiting");
        co_return;
    }
    spdlog::debug("[full_node] run_blockchain_subscriber() - channel obtained, entering loop");

    while (blockchain_channel->is_open() && !stopped()) {
        auto result = co_await blockchain_channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
        auto& [ec, fork_height, incoming, outgoing] = result;

        if (ec) {
            break;
        }

        if (!handle_reorganized(error::success, fork_height, incoming, outgoing)) {
            unsubscribe_blockchain(blockchain_channel);
            break;
        }
    }
}

#if ! defined(__EMSCRIPTEN__)
::asio::awaitable<void> full_node::run_sync() {
    spdlog::debug("[full_node] run_sync() starting");
    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer timer(executor);

    constexpr auto retry_delay = std::chrono::seconds(10);

    while (!stopped()) {
        // Wait until we have at least one connected peer
        spdlog::debug("[full_node] run_sync() waiting for peers, count={}", network_.connection_count());
        while (!stopped() && network_.connection_count() == 0) {
            timer.expires_after(std::chrono::milliseconds(500));
            auto [ec] = co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                co_return;
            }
        }

        if (stopped()) {
            co_return;
        }

        spdlog::info("[node] Starting initial block sync...");
        auto result = co_await sync_from_best_peer(chain_, network_, network_type_);

        if (result.error) {
            spdlog::warn("[node] Sync failed: {}, retrying in {}s...",
                result.error.message(), retry_delay.count());

            // Wait before retrying
            timer.expires_after(retry_delay);
            auto [ec] = co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                co_return;  // Timer cancelled, node stopping
            }
            continue;
        }

        spdlog::info("[node] Initial sync complete: {} headers, {} blocks, height {}",
            result.headers_received, result.blocks_received, result.final_height);
        break;  // Sync successful, exit loop
    }
}
#endif

void full_node::stop() {
#if ! defined(__EMSCRIPTEN__)
    network_.stop();
#endif

    if (!chain_.stop()) {
        spdlog::error("[node] Failed to stop blockchain.");
    }
}

void full_node::join() {
#if ! defined(__EMSCRIPTEN__)
    network_.join();
#endif

    if (!chain_.close()) {
        spdlog::error("[node] Failed to close blockchain.");
    }
}

bool full_node::stopped() const {
#if ! defined(__EMSCRIPTEN__)
    return network_.stopped();
#else
    return true;  // In WASM, there's no network, always "stopped" from network perspective
#endif
}

// =============================================================================
// Properties
// =============================================================================

node::settings const& full_node::node_settings() const {
    return node_settings_;
}

blockchain::settings const& full_node::chain_settings() const {
    return chain_settings_;
}

#if ! defined(__EMSCRIPTEN__)
kth::network::settings const& full_node::network_settings() const {
    return network_.network_settings();
}
#endif

block_chain& full_node::chain() {
    return chain_;
}

block_chain& full_node::chain_kth() {
    return chain_;
}

#if ! defined(__EMSCRIPTEN__)
kth::network::p2p_node& full_node::network() {
    return network_;
}
#endif

// =============================================================================
// Thread Pools
// =============================================================================

threadpool& full_node::chain_thread_pool() {
    return chain_thread_pool_;
}

#if ! defined(__EMSCRIPTEN__)
threadpool& full_node::network_thread_pool() {
    return network_.thread_pool();
}
#endif

// =============================================================================
// Subscriptions
// =============================================================================

full_node::block_channel_ptr full_node::subscribe_blockchain() {
    return chain().subscribe_blockchain();
}

full_node::transaction_channel_ptr full_node::subscribe_transaction() {
    return chain().subscribe_transaction();
}

full_node::ds_proof_channel_ptr full_node::subscribe_ds_proof() {
    return chain().subscribe_ds_proof();
}

void full_node::unsubscribe_blockchain(block_channel_ptr const& channel) {
    chain().unsubscribe_blockchain(channel);
}

void full_node::unsubscribe_transaction(transaction_channel_ptr const& channel) {
    chain().unsubscribe_transaction(channel);
}

void full_node::unsubscribe_ds_proof(ds_proof_channel_ptr const& channel) {
    chain().unsubscribe_ds_proof(channel);
}

// =============================================================================
// Internal Handlers
// =============================================================================

bool full_node::handle_reorganized(
    code ec,
    size_t fork_height,
    block_const_ptr_list_const_ptr incoming,
    block_const_ptr_list_const_ptr outgoing)
{
    if (stopped() || ec == error::service_stopped) {
        return false;
    }

    if (ec) {
        spdlog::error("[node] Failure handling reorganization: {}", ec.message());
        stop();
        return false;
    }

    // Nothing to do here
    if (!incoming || incoming->empty()) {
        return true;
    }

    for (auto const& block : *outgoing) {
        spdlog::debug("[node] Reorganization moved block to orphan pool [{}]",
            encode_hash(block->header().hash()));
    }

    auto const height = *safe_add(fork_height, incoming->size());

#if ! defined(__EMSCRIPTEN__)
    network_.set_top_block({incoming->back()->hash(), height});
#endif

    return true;
}

// =============================================================================
// Utilities
// =============================================================================

domain::chain::block full_node::get_genesis_block(domain::config::network network) {
    switch (network) {
        case domain::config::network::testnet:
            return domain::chain::block::genesis_testnet();
        case domain::config::network::regtest:
            return domain::chain::block::genesis_regtest();
#if defined(KTH_CURRENCY_BCH)
        case domain::config::network::testnet4:
            return domain::chain::block::genesis_testnet4();
        case domain::config::network::scalenet:
            return domain::chain::block::genesis_scalenet();
        case domain::config::network::chipnet:
            return domain::chain::block::genesis_chipnet();
#endif
        default:
        case domain::config::network::mainnet:
            return domain::chain::block::genesis_mainnet();
    }
}

} // namespace kth::node
