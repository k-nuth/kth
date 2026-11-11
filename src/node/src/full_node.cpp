// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/detail/block_chain_internal.hpp>

#include <kth/node/full_node.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>

#include <kth/blockchain.hpp>
#include <kth/domain/multi_crypto_support.hpp>
#include <kth/node/configuration.hpp>
#include <kth/node/define.hpp>
#include <kth/node/detail/fee_filter.hpp>
#include <kth/node/detail/tx_announcements.hpp>
#include <kth/node/handlers/chain_data.hpp>
#include <kth/node/mining/work.hpp>
#include <kth/node/sv2/jd_settings.hpp>
#include <kth/node/sync/tip_announcement.hpp>
#include <kth/node/user_agent.hpp>
#include <kth/infrastructure/utility/pseudo_random.hpp>

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
    : node_settings_(configuration.node)
    , chain_settings_(configuration.chain)
    , network_type_(configuration.net)
    , network_settings_(configuration.network)
    , network_(network_settings_)
    , block_submissions_(std::make_shared<blockchain::block_submission_channel>(4, 1))
    , chain_(
        configuration.chain,
        configuration.database,
        network_type_,
        configuration.network.relay_transactions,
        block_submissions_
    )
    , rpc_settings_(configuration.rpc)
    , sv2_settings_(configuration.sv2)
#else
    : node_settings_(configuration.node)
    , chain_settings_(configuration.chain)
    , network_type_(configuration.net)
    // No sync owner exists in the browser build. Refuse instead of queueing.
    , block_submissions_()
    , chain_(
        configuration.chain,
        configuration.database,
        network_type_,
        true,
        block_submissions_
    )
    , rpc_settings_(configuration.rpc)
    , sv2_settings_(configuration.sv2)
#endif
{
#if ! defined(__EMSCRIPTEN__)
    spdlog::debug("[full_node] configuration.network.threads = {}", configuration.network.threads);
    spdlog::debug("[full_node] network_settings_.threads = {}", network_settings_.threads);

    // Set user agent after initialization (network_ holds reference to network_settings_)
    network_settings_.user_agent = get_user_agent({});
#endif
}

full_node::~full_node() {
    spdlog::debug("[full_node] destructor starting");
    // Only stop/join if not already stopped
    if (!stopped()) {
        spdlog::debug("[full_node] destructor - not stopped, calling stop/join");
        stop();
        join();
    }
    spdlog::debug("[full_node] destructor - about to destroy members");
}

// =============================================================================
// Lifecycle
// =============================================================================

::asio::awaitable<code> full_node::start() {
#if ! defined(__EMSCRIPTEN__)
    if (!stopped()) {
        co_return error::operation_failed;
    }
    if (auto const reason = mining::mining_startup_reason(sv2_settings_, network_type_);
            ! reason.empty()) {
        spdlog::error("[node] {}", reason);
        co_return error::operation_failed;
    }
    std::string jd_error;
    if ( ! sv2::validate_jd_settings(sv2_settings_, jd_error)) {
        spdlog::error("[node] {}", jd_error);
        co_return error::operation_failed;
    }
#endif

    // Start the blockchain (pass disk magic for block file storage)
    if (!chain_.start(get_disk_magic(network_type_))) {
        spdlog::error("[node] Failure starting blockchain.");
        co_return error::operation_failed;
    }

    // Reload the persisted mempool (re-validated against the current tip) before
    // the network comes up, so peers can be served from a warm mempool.
    co_await chain_.load_mempool_from_disk();

    // Serve the connected chain: headers for a locator, and the blocks a
    // peer asks for after an announcement or during its own sync. Both run in
    // the peer's service, off its read loop: their answers wait on the peer's
    // data queue.
    network_.register_served_message_handler<domain::message::get_headers>(
        [this](peer_session& peer, domain::message::get_headers const& request) {
            return handlers::chain_data::handle_get_headers(chain_, peer, request);
        });
    network_.register_served_message_handler<domain::message::get_data>(
        [this](peer_session& peer, domain::message::get_data const& request) {
            return handlers::chain_data::handle_get_data(chain_, peer, request);
        });
    // Each newly connected tip, owed to every peer by the sync coordinator, is
    // announced by the peer's own service.
    network_.set_tip_announcement_handler(
        [this](peer_session& peer, hash_digest const& tip, std::optional<hash_digest>& last_announced) {
            return sync::announce_tip(chain_.headers(), peer, tip, last_announced);
        });
    // The fee filter (BIP133) each peer's service sends on its schedule: the
    // mempool's minimum, rounded as BCHN rounds it, never below the relay
    // floor. Only when the node relays transactions, and only to peers that
    // understand it.
    if (network_settings_.relay_transactions) {
        auto const floor = uint64_t(std::llround(chain_settings_.byte_fee_satoshis * 1000.0));
        auto rounder = std::make_shared<detail::fee_filter_rounder const>(floor);
        network_.set_fee_filter_handler(
            [this, floor, rounder](peer_session& peer) -> ::asio::awaitable<std::optional<uint64_t>> {
                if (peer.negotiated_version() < domain::message::version::level::bip133) {
                    co_return std::nullopt;
                }
                auto const minimum = co_await chain_.mempool_minimum_fee();
                // BCHN rounds down two times in three.
                auto const step_down = pseudo_random::generate<uint32_t>(0, 2) != 0;
                co_return detail::fee_filter_to_send(*rounder, minimum, floor, step_down);
            });
        // The transactions each peer's service announces: the mempool's order
        // of admission, at most an announcement's worth for the next block's
        // size limit at a time.
        network_.set_tx_announcement_handler(detail::tx_announcement_sources{
            .last = [this] { return chain_.mempool_last_admission(); },
            .admitted_after = [this](uint64_t after, size_t limit) -> ::asio::awaitable<detail::admitted_batch> {
                auto admitted = co_await chain_.mempool_admitted_after(after, limit);
                co_return detail::admitted_batch{std::move(admitted.entries), admitted.last};
            },
            .limit = [this] {
#if defined(KTH_CURRENCY_BCH)
                // The next block's size limit (BCHN: ceil(7 * MB * 0.5 s)).
                auto const state = chain_.chain_state();
                return detail::max_announcements(state ? state->dynamic_max_block_size()
                                                       : chain_settings_.default_consensus_block_size);
#else
                // A fixed one-million-byte limit.
                return detail::max_announcements(max_block_size);
#endif
            },
            .pool = [this]() -> ::asio::awaitable<detail::admitted_batch> {
                auto pooled = co_await chain_.mempool_snapshot();
                co_return detail::admitted_batch{std::move(pooled.entries), pooled.last};
            },
        });
    }

// The gRPC server binds first: if it cannot, nothing else has started yet.
#if defined(KTH_WITH_GRPC) && ! defined(__EMSCRIPTEN__)
    if (rpc_settings_.enabled) {
        grpc_server_ = std::make_unique<grpc_rpc::server>(rpc_settings_, chain_, network_type_);
        auto const port = grpc_server_->start();
        if ( ! port) {
            // Asked for and unavailable is a failed start, not a node quietly
            // running without the service it was configured to offer. Before the
            // network starts, so a failed start leaves only the chain, closed here
            // as for a network that fails to start.
            spdlog::error("[node] gRPC server failed to start: {}", port.error());
            grpc_server_.reset();
            (void)chain_.stop();
            co_return error::listen_failed;
        }
    } else {
        spdlog::info("[node] gRPC server disabled (rpc.enabled=false).");
    }
#endif

#if ! defined(__EMSCRIPTEN__)
    // Start the P2P network
    auto ec = co_await network_.start();
    if (ec != error::success) {
        spdlog::error("[node] Failure starting network: {}", ec.message());
#if defined(KTH_WITH_GRPC)
        // Asked to wind down, not joined here: join() of the node ends it, before
        // the chain is destroyed. Its calls see the stopped chain meanwhile.
        if (grpc_server_) grpc_server_->stop();
#endif
        (void)chain_.stop();
        co_return ec;
    }
#endif

#if defined(KTH_WITH_RPC) && ! defined(__EMSCRIPTEN__)
    if (rpc_settings_.enabled) {
        auto rpc_executor = network_.thread_pool().get_executor();
        rpc_server_ = std::make_unique<rpc::server>(rpc_settings_, chain_, rpc_executor, &network_);
    } else {
        spdlog::info("[node] JSON-RPC server disabled (rpc.enabled=false).");
    }
#elif defined(KTH_WITH_CAPNPROTO_RPC) && ! defined(__EMSCRIPTEN__)
    if (rpc_settings_.enabled) {
        capnp_reader_ = std::make_unique<capnp_rpc::chain_reader>(chain_);
        capnp_server_ = std::make_unique<capnp_rpc::server>(rpc_settings_, *capnp_reader_,
            network_.thread_pool().get_executor());
    } else {
        spdlog::info("[node] Cap'n Proto RPC server disabled (rpc.enabled=false).");
    }
#elif ! defined(__EMSCRIPTEN__) && ! defined(KTH_WITH_GRPC)
    if (rpc_settings_.enabled) {
        spdlog::warn("[node] rpc.enabled=true but this build has no RPC server "
            "(compile with Conan rpc=jsonrpc|grpc|capnproto, or CMake KTH_RPC_BACKEND=jsonrpc|grpc|capnproto).");
    }
#endif

#if ! defined(__EMSCRIPTEN__)
    if (sv2_settings_.enabled) {
        template_provider_ = std::make_unique<sv2::template_provider>(
            sv2_settings_, network_.thread_pool().get_executor(), chain_, network_type_);
    } else {
        spdlog::info("[node] Stratum V2 Template Provider disabled (sv2.enabled=false).");
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
    auto core = [this]() -> ::asio::awaitable<void> {
        co_await (run_blockchain_subscriber() && network_.run() && run_sync());
        // Embedders may stop P2P directly. Listeners must not keep run()
        // pending after the core tasks finish without full_node::stop().
#if defined(KTH_WITH_RPC)
        if (rpc_server_) rpc_server_->stop();
#endif
#if defined(KTH_WITH_GRPC)
        if (grpc_server_) grpc_server_->stop();
#endif
#if defined(KTH_WITH_CAPNPROTO_RPC)
        if (capnp_server_) capnp_server_->stop();
#endif
        if (template_provider_) template_provider_->stop();
    };
    co_await (core() && run_rpc() && run_sv2());
    spdlog::debug("[full_node] All parallel tasks completed");
#else
    // WASM: only blockchain subscriber (no network)
    co_await run_blockchain_subscriber();
#endif

    co_return error::success;
}

::asio::awaitable<void> full_node::run_rpc() {
#if defined(KTH_WITH_RPC) && ! defined(__EMSCRIPTEN__)
    if (rpc_server_) co_await rpc_server_->run();
#elif defined(KTH_WITH_CAPNPROTO_RPC) && ! defined(__EMSCRIPTEN__)
    if (capnp_server_) co_await capnp_server_->run();
#endif
    co_return;
}

#if ! defined(__EMSCRIPTEN__)
::asio::awaitable<void> full_node::run_sv2() {
    if (template_provider_) co_await template_provider_->run();
    co_return;
}
#endif

::asio::awaitable<void> full_node::run_blockchain_subscriber() {
    spdlog::info("[full_node] run_blockchain_subscriber() STARTING");
    auto blockchain_channel = subscribe_blockchain();
    if (!blockchain_channel) {
        spdlog::debug("[full_node] run_blockchain_subscriber() - no channel, exiting");
        co_return;
    }
    spdlog::debug("[full_node] run_blockchain_subscriber() - channel obtained, entering loop");

    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer timer(executor);

    while (!stopped()) {
        // Non-blocking try_receive
        size_t fork_height{};
        block_const_ptr_list_const_ptr incoming{};
        block_const_ptr_list_const_ptr outgoing{};

        bool received = blockchain_channel->try_receive([&](std::error_code ec, size_t fh, auto in, auto out) {
            if (!ec) {
                fork_height = fh;
                incoming = std::move(in);
                outgoing = std::move(out);
            }
        });

        if (received) {
            if (!handle_reorganized(error::success, fork_height, incoming, outgoing)) {
                unsubscribe_blockchain(blockchain_channel);
                break;
            }
        } else if (!blockchain_channel->is_open()) {
            // Channel closed
            break;
        } else {
            // No data, sleep briefly
            timer.expires_after(std::chrono::milliseconds(100));
            auto [ec] = co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                break;  // Timer cancelled
            }
        }
    }
    spdlog::info("[full_node] run_blockchain_subscriber() ENDED");
}

#if ! defined(__EMSCRIPTEN__)
::asio::awaitable<void> full_node::run_sync() {
    spdlog::info("[full_node] run_sync() STARTING - CSP sync system");

    // Create the header organizer (single instance for the sync lifetime)
    blockchain::header_organizer organizer(
        chain_.headers(),
        chain_.chain_settings(),
        network_type_
    );

    // Initialize the organizer
    if (!organizer.start()) {
        spdlog::error("[full_node] Failed to start header organizer");
        co_return;
    }

    // Sync tip from chain's header index
    organizer.sync_tip();

    // The index is materialised, so hydration is over. From here a header the
    // index cannot produce is a broken invariant rather than a reason to ask the
    // durable checkpoint: the chain state's header comes from memory, and it
    // stops depending on how far that checkpoint happened to get (#697).
    blockchain::detail::block_chain_internal::end_hydration(chain_);

    // Tell it how far blocks are already validated. Nothing else does until the
    // first connected publication, and deep-reorg parking measures depth against
    // this height: left at "nothing validated yet", a node that just came up
    // would follow any branch with more work no matter how deep it forks — in
    // exactly the window where finalization is not protecting either, since it
    // needs headers older than the finalization delay to have any effect.
    if (auto const heights = chain_.get_last_heights(); heights) {
        organizer.note_block_validated(static_cast<int32_t>(heights->block));
    } else {
        spdlog::error("[full_node] Could not read the validated height at startup; deep-reorg "
            "parking stays inactive until the first connected publication");
    }

    spdlog::info("[node] Starting CSP-based sync system...");

    // Run the CSP sync orchestrator on the network thread pool.
    // This ensures all sync coroutines (block download, header download, etc.)
    // run on the network pool's 32 threads instead of the single io_thread.
    co_await ::asio::co_spawn(
        network_.thread_pool().get_executor(),
        sync::sync_orchestrator(chain_, organizer, network_, network_type_,
            [this](std::string const& reason) { notify_fatal(reason); },
            sync::node_header_walk_timing, block_submissions_.get()),
        ::asio::use_awaitable
    );

    organizer.stop();
    spdlog::info("[full_node] run_sync() ENDED");
}
#endif

void full_node::set_fatal_handler(fatal_handler handler) {
    fatal_handler_ = std::move(handler);
}

void full_node::notify_fatal(std::string const& reason) {
    spdlog::critical("[node] Cannot continue: {}", reason);

    // Stop first, so the node's own tasks wind down even when nobody installed a
    // handler — a library embedding the node still gets a node that stops.
    stop();

    if (fatal_handler_) {
        fatal_handler_(reason);
    } else {
        spdlog::warn("[node] No fatal handler installed; the node stopped but its owner was not "
            "told, so the process may stay up with nothing left to do");
    }
}

void full_node::stop() {
#if defined(KTH_WITH_RPC) && ! defined(__EMSCRIPTEN__)
    if (rpc_server_) rpc_server_->stop();
#endif
#if defined(KTH_WITH_GRPC) && ! defined(__EMSCRIPTEN__)
    // Only asks (never blocks): this can run on a node executor thread.
    if (grpc_server_) grpc_server_->stop();
#endif
#if defined(KTH_WITH_CAPNPROTO_RPC) && ! defined(__EMSCRIPTEN__)
    if (capnp_server_) capnp_server_->stop();
#endif
#if ! defined(__EMSCRIPTEN__)
    if (template_provider_) template_provider_->stop();
    // IMPORTANT: Only stop the network here, NOT the chain!
    // The chain must remain operational until run() completes because
    // run_sync() may still be inside chain.submit_block() when this is called.
    // chain_.stop() is called in join() after run() has fully exited.
    network_.stop();
#endif
}

void full_node::join() {
#if defined(KTH_WITH_GRPC) && ! defined(__EMSCRIPTEN__)
    // First: in-flight calls still run chain work on the network executor and
    // read the chain, so both must outlive them. join() is the owner's thread,
    // not an executor thread, so waiting here blocks no node work. Ask first, in
    // case the owner went straight to join().
    if (grpc_server_) {
        grpc_server_->stop();
        grpc_server_->join();
    }
#endif
#if ! defined(__EMSCRIPTEN__)
    network_.join();
#endif

    // Now that run() has exited (all coroutines done), it's safe to stop the chain.
    // This must happen AFTER run() completes to avoid crashes in chain.submit_block().
    if (!chain_.stop()) {
        spdlog::error("[node] Failed to stop blockchain.");
    }

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
kth::node::p2p_node& full_node::network() {
    return network_;
}
#endif

// =============================================================================
// Thread Pools
// =============================================================================

threadpool& full_node::chain_thread_pool() {
    return chain_.thread_pool();
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
            encode_hash(kth::domain::chain::hash(block->header())));
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
        case domain::config::network::tempnet:
        case domain::config::network::chipnet:
            return domain::chain::block::genesis_chipnet();
#endif
        default:
        case domain::config::network::mainnet:
            return domain::chain::block::genesis_mainnet();
    }
}

} // namespace kth::node
