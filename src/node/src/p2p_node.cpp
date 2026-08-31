// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/p2p_node.hpp>

#include <kth/node/detail/block_announcements.hpp>

#include <algorithm>

#include <kth/node/handlers/ping.hpp>
#include <kth/node/handlers/pong.hpp>

#include <asio/experimental/awaitable_operators.hpp>
#include <asio/use_awaitable.hpp>
#include <fmt/format.h>

namespace kth::node {
using namespace kth::network;

using namespace ::asio::experimental::awaitable_operators;
using namespace std::chrono_literals;

// =============================================================================
// P2P Node implementation
// =============================================================================

p2p_node::p2p_node(settings const& settings)
    : settings_(settings)
    , pool_("network", thread_ceiling(settings.threads))  // default 4, 0 = all cores
    , manager_(pool_.get_executor())
    , peer_db_(settings.peers_file, settings.host_pool_capacity)
    , top_block_({null_hash, 0})
    , new_peer_channel_(std::make_unique<concurrent_channel<peer_event>>(pool_.get_executor(), 100))
    , stop_signal_(std::make_unique<concurrent_event_channel>(pool_.get_executor(), 1))
    , peer_notification_channel_(std::make_unique<concurrent_channel<peer_notification>>(pool_.get_executor(), 100))
    // One slot: this is a doorbell, and a second ring while the first is
    // unanswered says nothing the first does not.
    , block_announcement_channel_(std::make_unique<concurrent_channel<blocks_announced>>(pool_.get_executor(), 1))
{
    spdlog::debug("[p2p_node] p2p_node constructor - thread pool size: {}", pool_.size());
    // Register default message handlers using typed registration
    // The make_handler<> wrapper handles parsing automatically
    dispatcher_.register_handler<domain::message::ping>(handlers::ping::handle);
    dispatcher_.register_handler<domain::message::pong>(handlers::pong::handle);
}

p2p_node::~p2p_node() {
    spdlog::debug("[p2p_node] destructor starting");
    stop();
    spdlog::debug("[p2p_node] destructor - stop() done, calling join()...");
    join();
    spdlog::debug("[p2p_node] destructor done");
}

::asio::awaitable<code> p2p_node::start() {
    // One transition, and this object is single-use.
    //
    // A stop closes new_peer_channel_, stop_signal_ and peer_notification_channel_
    // — built once, in the constructor's init list, and never rebuilt — and
    // join() on an asio::thread_pool is terminal. A second start was nonetheless
    // ADMITTED, because the service state said "not running" after every stop, so
    // the call
    // returned success and then ran on closed channels over a pool that could
    // not serve it.
    //
    // Supporting restart would mean rebuilding the pool, the channels and the
    // tasks, not clearing flags. Until something does that, this refuses.
    // ONE atomic step. `created → running` is the only admission there is, so a
    // second start finds something other than `created` and is refused — and the
    // value it found says why.
    //
    // Nothing to serialise: the admission and the active state are the same word,
    // so a stop() racing this either wins the transition or loses it. It cannot
    // land between two of them, because there is only one.
    auto expected = lifecycle_state::fresh;
    if ( ! lifecycle_.compare_exchange_strong(expected, lifecycle_state::active,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        if (expected == lifecycle_state::stopped) {
            spdlog::error("[p2p_node] start() refused: this node was already stopped.");
        } else {
            spdlog::error("[p2p_node] start() refused: this node has already been started once. "
                "A p2p_node is single-use — its channels and thread pool do not come back.");
        }
        co_return error::operation_failed;
    }

    // Load peer database (unified storage for hosts, bans, and reputation)
    if (!peer_db_.load()) {
        spdlog::warn("[p2p_node] Failed to load peer database, starting fresh");
        // Try to import from legacy files
        auto imported_hosts = peer_db_.import_hosts_cache(settings_.hosts_file);
        auto imported_bans = peer_db_.import_banlist(settings_.banlist_file);
        if (imported_hosts > 0 || imported_bans > 0) {
            spdlog::info("[p2p_node] Imported {} hosts and {} bans from legacy files",
                imported_hosts, imported_bans);
        }
    }

    auto const [available, banned] = peer_db_.count_by_status();
    spdlog::info("[p2p_node] Loaded {} peers ({} available, {} banned)",
        peer_db_.size(), available, banned);

    // Seed if needed
    if (available < settings_.host_pool_capacity / 2) {
        co_await run_seeding();
    }

    seeded_ = true;
    co_return error::success;
}

::asio::awaitable<code> p2p_node::run() {
    spdlog::info("[p2p_node] run() STARTING");

    if (stopped()) {
        spdlog::debug("[p2p_node] run() - already stopped");
        co_return error::service_stopped;
    }

    // Admitted once, like start(). A second run() would spawn every network task
    // again -- among them the status task, and two coroutines waiting on the one
    // status_timer_ means a cancellation ends whichever happens to be waiting.
    //
    // Its own flag rather than a fourth lifecycle state: nothing pairs with this
    // one. No stop reads it, and it is not published together with anything, so
    // folding it into that word would make the transition table describe two
    // unrelated things.
    if (run_admitted_.exchange(true, std::memory_order_acq_rel)) {
        spdlog::error("[p2p_node] run() refused: this node is already running.");
        co_return error::operation_failed;
    }

    // Run all network tasks in parallel using task_group on pool_ executor.
    // We use task_group instead of && operator because && runs all coroutines
    // on the caller's executor (which may be single-threaded). task_group
    // uses pool_.get_executor() which has multiple threads, allowing true
    // parallelism and proper coroutine scheduling.
    task_group network_tasks("network_tasks", pool_.get_executor());

    spdlog::debug("[p2p_node] Spawning peer_supervisor...");
    network_tasks.spawn("peer_supervisor", [this]() -> ::asio::awaitable<void> {
        spdlog::debug("[p2p_node] peer_supervisor coroutine starting");
        co_await peer_supervisor();
        spdlog::debug("[p2p_node] peer_supervisor coroutine finished");
    });

    spdlog::debug("[p2p_node] Spawning run_inbound...");
    network_tasks.spawn("run_inbound", [this]() -> ::asio::awaitable<void> {
        spdlog::debug("[p2p_node] run_inbound coroutine starting");
        co_await run_inbound();
        spdlog::debug("[p2p_node] run_inbound coroutine finished");
    });

    spdlog::debug("[p2p_node] Spawning run_outbound...");
    network_tasks.spawn("run_outbound", [this]() -> ::asio::awaitable<void> {
        spdlog::debug("[p2p_node] run_outbound coroutine starting");
        co_await run_outbound();
        spdlog::debug("[p2p_node] run_outbound coroutine finished");
    });

    // Wait for supervisor to be ready before connecting manual peers
    while (!supervisor_ready_.load(std::memory_order_acquire) && !stopped()) {
        co_await ::asio::post(pool_.get_executor(), ::asio::use_awaitable);
    }

    // Connect to configured manual peers (supervisor is now ready)
    if (!settings_.peers.empty()) {
        spdlog::debug("[p2p_node] run() - connecting to {} manual peers", settings_.peers.size());
        for (auto const& peer : settings_.peers) {
            auto result = co_await connect(peer.host(), peer.port());
            if (!result) {
                spdlog::warn("[p2p_node] Failed to connect to configured peer {}:{} - {}",
                    peer.host(), peer.port(), result.error().message());
            }
        }
    }

    spdlog::debug("[p2p_node] All tasks spawned, waiting on join...");
    // Wait for all tasks to complete (i.e., until stop() is called)
    co_await network_tasks.join();
    spdlog::info("[p2p_node] run() ENDED - all tasks completed");

    co_return error::success;
}

void p2p_node::stop() {
    // ONE atomic step, and the value it replaces says what this stop has to do.
    auto const previous = lifecycle_.exchange(lifecycle_state::stopped, std::memory_order_acq_rel);

    if (previous == lifecycle_state::stopped) {
        return;             // someone else already did all of this
    }
    if (previous == lifecycle_state::fresh) {
        // Nothing was ever running, so there is nothing to cancel, stop or save —
        // but the claim above stands, and it is what refuses a later start.
        return;
    }

    supervisor_ready_ = false;  // Not for restart: see start(), which refuses one.

    // Before the channel cancellations below, and after the stopped state is
    // published, so a wake
    // that lands before the timer is armed is covered by the loop's own re-read.
    wake_status_task();

    // Cancel and close channels - cancel() wakes up pending async ops, close() alone does NOT!
    if (stop_signal_) {
        stop_signal_->cancel();
        stop_signal_->close();
    }

    // Close the new peer channel to wake up peer_supervisor
    if (new_peer_channel_) {
        new_peer_channel_->cancel();
        new_peer_channel_->close();
    }

    // Close the peer notification channel to wake up sync layer
    if (peer_notification_channel_) {
        peer_notification_channel_->cancel();
        peer_notification_channel_->close();
    }

    // Same for the announcement doorbell: the bridge that answers it waits on
    // this and on nothing else, so a channel left open is a task join() waits
    // for forever. The registered hashes go with it — a stopping node owes no
    // follow-up to anybody.
    if (block_announcement_channel_) {
        block_announcement_channel_->cancel();
        block_announcement_channel_->close();
    }
    {
        std::lock_guard<std::mutex> const guard(announced_blocks_mutex_);
        announced_blocks_.clear();
        announced_blocks_overflowed_ = false;
    }

    // Stop acceptor - this causes run_inbound() to exit
    if (acceptor_) {
        std::error_code ec;
        acceptor_->close(ec);
    }

    // Stop all peers - this causes peer->run() to exit, which allows
    // peer_supervisor's peer_tasks.join() to complete
    manager_.stop_all();

    // Save peer database (unified storage)
    if (!peer_db_.save()) {
        spdlog::warn("[p2p_node] Failed to save peer database on shutdown");
    }

    // NOTE: We do NOT call pool_.stop() here!
    // With structured concurrency, all coroutines will complete naturally:
    // 1. run_inbound() exits (acceptor closed)
    // 2. run_outbound() exits (the node is stopped)
    // 3. peer_supervisor() exits after peer_tasks.join() completes
    // 4. run() returns, then join() can complete
    // Calling pool_.stop() here would abort pending work and prevent clean shutdown.
}

void p2p_node::join() {
    pool_.join();
}

// Properties
// -----------------------------------------------------------------------------

settings const& p2p_node::network_settings() const {
    return settings_;
}

bool p2p_node::stopped() const {
    // Anything that is not `active` counts as stopped, which is what every caller
    // and every loop condition means by it: `fresh` has not started and `stopped`
    // has finished.
    return lifecycle_.load(std::memory_order_acquire) != lifecycle_state::active;
}

size_t p2p_node::connection_count() const {
    return manager_.count_snapshot();
}

threadpool& p2p_node::thread_pool() {
    return pool_;
}

message_dispatcher& p2p_node::dispatcher() {
    return dispatcher_;
}

void p2p_node::ban_peer(
    peer_session::ptr const& peer,
    std::chrono::seconds duration,
    ban_reason reason)
{
    if (peer) {
        peer_db_.ban(peer->authority(), duration, reason);
        peer->stop(error::channel_stopped);
    }
}

bool p2p_node::is_banned(infrastructure::config::authority const& authority) const {
    return peer_db_.is_banned(authority);
}

// =============================================================================
// Peer Database
// =============================================================================

peer_database& p2p_node::peer_db() {
    return peer_db_;
}

peer_database const& p2p_node::peer_db() const {
    return peer_db_;
}

bool p2p_node::report_misbehavior(
    peer_session::ptr const& peer,
    int score,
    std::string_view reason)
{
    if (!peer) {
        return false;
    }
    bool banned = report_misbehavior(peer->authority(), score, reason);
    if (banned) {
        // Disconnect the peer when they get banned
        peer->stop();
    }
    return banned;
}

bool p2p_node::report_misbehavior(
    infrastructure::config::authority const& authority,
    int score,
    std::string_view reason)
{
    if (!reason.empty()) {
        spdlog::debug("[p2p_node] Misbehavior from [{}]: {} (score +{})",
            authority.to_string(), reason, score);
    }

    // add_misbehavior auto-bans when threshold is reached and returns true if banned
    return peer_db_.add_misbehavior(authority, score);
}

void p2p_node::record_peer_performance(
    peer_session::ptr const& peer,
    uint32_t blocks,
    uint32_t time_ms)
{
    if (!peer) {
        return;
    }
    record_peer_performance(peer->authority(), blocks, time_ms);
}

void p2p_node::record_peer_performance(
    infrastructure::config::authority const& authority,
    uint32_t blocks,
    uint32_t time_ms)
{
    peer_db_.record_block_download(authority, blocks, time_ms);
}

bool p2p_node::is_slow_peer(infrastructure::config::authority const& authority,
                             double threshold_ms) const
{
    return peer_db_.is_slow_peer(authority, threshold_ms);
}

double p2p_node::get_peer_speed(infrastructure::config::authority const& authority) const
{
    return peer_db_.get_peer_speed(authority);
}

// =============================================================================
// Message Dispatcher implementation
// =============================================================================

::asio::awaitable<bool> message_dispatcher::dispatch(peer_session& peer, raw_message const& msg) {
    auto const& command = msg.heading.command();

    spdlog::debug("[dispatcher] Dispatching '{}' from [{}]", command, peer.authority_with_agent());

    // Look for specific handler
    auto it = handlers_.find(command);
    if (it != handlers_.end()) {
        auto result = co_await it->second(peer, msg);
        switch (result) {
            case message_result::disconnect:
                co_return false;
            case message_result::continue_processing:
            case message_result::handled:
                co_return true;
            case message_result::not_handled:
                // Fall through to default handler
                break;
        }
    }

    // Try default handler
    if (default_handler_) {
        auto result = co_await default_handler_(peer, msg);
        co_return result != message_result::disconnect;
    }

    // No handler - just continue
    spdlog::trace("[dispatcher] Unhandled message '{}' from [{}]", command, peer.authority_with_agent());
    co_return true;
}

void message_dispatcher::register_handler(std::string const& command, message_handler_fn handler) {
    handlers_[command] = std::move(handler);
}

void message_dispatcher::set_default_handler(message_handler_fn handler) {
    default_handler_ = std::move(handler);
}

infrastructure::config::checkpoint p2p_node::top_block() const {
    return top_block_.load();
}

void p2p_node::set_top_block(infrastructure::config::checkpoint const& top) {
    top_block_.store(top);
}

void p2p_node::set_top_block(infrastructure::config::checkpoint&& top) {
    top_block_.store(std::move(top));
}

// Manual connections
// -----------------------------------------------------------------------------

awaitable_expected<peer_session::ptr> p2p_node::connect(
    std::string const& host,
    uint16_t port)
{
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto executor = co_await ::asio::this_coro::executor;

    // Use async_connect from peer_session.hpp
    auto result = co_await async_connect(
        executor,
        host,
        port,
        settings_,
        std::chrono::seconds(settings_.connect_timeout_seconds));

    if (!result) {
        co_return std::unexpected(result.error());
    }

    auto peer = *result;
    auto const ip = peer->authority().asio_ip();

    // Check if already connected to this IP
    if (co_await manager_.exists_by_ip(ip)) {
        spdlog::debug("[p2p_node] Already connected to IP {}, skipping", ip.to_string());
        peer->stop();
        co_return std::unexpected(error::address_in_use);
    }

    // Check if banned before proceeding
    if (peer_db_.is_banned(peer->authority())) {
        spdlog::debug("[p2p_node] Rejecting connection to banned peer {}:{}", host, port);
        peer->stop();
        co_return std::unexpected(error::address_blocked);
    }

    // Create response channel for handshake result
    auto response_channel = std::make_shared<handshake_response_channel>(executor, 1);

    // Send peer to supervisor for FULL lifecycle management (structured concurrency)
    // The supervisor will do: peer->run() && (handshake && protocols)
    // This eliminates the need for detached coroutines entirely!
    if (new_peer_channel_ && new_peer_channel_->is_open()) {
        co_await new_peer_channel_->async_send(
            std::error_code{},
            peer_event{peer, peer_direction::outbound, response_channel},
            ::asio::use_awaitable);
    } else {
        peer->stop();
        co_return std::unexpected(error::service_stopped);
    }

    // Wait for supervisor to complete handshake and send result
    auto [recv_ec, response] = co_await response_channel->async_receive(
        ::asio::as_tuple(::asio::use_awaitable));

    if (recv_ec || response.result != error::success) {
        // Handshake failed - peer is already stopped by supervisor
        auto err = recv_ec ? error::channel_stopped : response.result;
        spdlog::debug("[p2p_node] Handshake failed with {}:{} - {}",
            host, port, err.message());
        co_return std::unexpected(err);
    }

    spdlog::debug("[p2p_node] Connected to {}:{}, version {}, {}",
        host, port, peer->negotiated_version(),
        peer->peer_version()->user_agent());

    co_return peer;
}

// Host management
// -----------------------------------------------------------------------------

size_t p2p_node::address_count() const {
    auto const [available, banned] = peer_db_.count_by_status();
    return available;
}

bool p2p_node::store(address const& addr) {
    return peer_db_.store_address(addr);
}

code p2p_node::fetch_address(address& out) const {
    return peer_db_.fetch_address(out);
}

bool p2p_node::remove(address const& addr) {
    return peer_db_.remove_address(addr);
}

// Peer access
// -----------------------------------------------------------------------------

peer_manager& p2p_node::peers() {
    return manager_;
}

void p2p_node::announce_blocks(hash_list const& hashes) {
    if (hashes.empty()) {
        return;
    }

    {
        std::lock_guard<std::mutex> const guard(announced_blocks_mutex_);
        for (auto const& hash : hashes) {
            // Coalesced here, where nothing can be lost, rather than by the
            // channel. Eight peers announcing one block register it once, and
            // the test is a hash lookup because peers decide how much arrives.
            if (announced_blocks_.size() >= max_announced_blocks
                && ! announced_blocks_.contains(hash)) {
                // Refused, and said so. Dropping it silently is what would lose
                // the one announcement that mattered; recorded, it still
                // produces the single request any announcement produces.
                announced_blocks_overflowed_ = true;
                continue;
            }
            announced_blocks_.insert(hash);
        }
    }

    // Registration first, doorbell second. A consumer that drains between the
    // two already sees what was just registered, and the ring that follows is
    // merely spurious; a ring dropped for a full channel is one whose queued
    // predecessor will bring the consumer back to a set that now includes this.
    // There is no ordering in which a registered hash goes undrained.
    if ( ! block_announcement_channel_->try_send(std::error_code{}, blocks_announced{})) {
        spdlog::trace("[p2p_node] Block announcement doorbell already ringing");
    }
}

void p2p_node::announce_from_headers(peer_session& peer, raw_message const& raw) {
    auto hashes = detail::announced_by_headers(raw.payload, peer.negotiated_version());
    if (hashes.empty()) {
        spdlog::debug("[p2p_node] Unparseable or empty headers announcement from [{}]",
            peer.authority());
        return;
    }
    spdlog::debug("[p2p_node] {} block(s) announced by headers from [{}]",
        hashes.size(), peer.authority());
    announce_blocks(std::move(hashes));
}

void p2p_node::announce_from_compact_block(peer_session& peer, raw_message const& raw) {
    auto hashes = detail::announced_by_compact_block(raw.payload, peer.negotiated_version());
    if (hashes.empty()) {
        spdlog::debug("[p2p_node] Unparseable cmpctblock announcement from [{}]", peer.authority());
        return;
    }
    spdlog::debug("[p2p_node] Block announced by cmpctblock from [{}]", peer.authority());
    announce_blocks(std::move(hashes));
}

void p2p_node::announce_from_inventory(peer_session& peer, raw_message const& raw) {
    auto hashes = detail::announced_by_inventory(raw.payload, peer.negotiated_version());
    if (hashes.empty()) {
        // Transactions, or nothing we act on. Said out loud rather than dropped
        // in silence: this path knows what it is not handling.
        spdlog::trace("[p2p_node] inv from [{}] announces no blocks", peer.authority());
        return;
    }
    spdlog::debug("[p2p_node] {} block(s) announced by inv from [{}]",
        hashes.size(), peer.authority());
    announce_blocks(std::move(hashes));
}

p2p_node::announced_blocks p2p_node::take_announced_blocks() {
    std::lock_guard<std::mutex> const guard(announced_blocks_mutex_);

    announced_blocks taken;
    taken.hashes.reserve(announced_blocks_.size());
    for (auto const& hash : announced_blocks_) {
        taken.hashes.push_back(hash);
    }
    taken.overflowed = announced_blocks_overflowed_;

    announced_blocks_.clear();
    announced_blocks_overflowed_ = false;
    return taken;
}

concurrent_channel<blocks_announced>& p2p_node::block_announcements() {
    return *block_announcement_channel_;
}

concurrent_channel<peer_notification>& p2p_node::peer_events() {
    return *peer_notification_channel_;
}

// Internal coroutines
// -----------------------------------------------------------------------------

::asio::awaitable<void> p2p_node::run_seeding() {
    spdlog::info("[p2p_node] Starting seeding from {} seeds", settings_.seeds.size());

    if (settings_.seeds.empty()) {
        spdlog::info("[p2p_node] No seeds configured");
        co_return;
    }

    // Use pool's executor for parallel DNS resolution, not this_coro::executor
    // (which might be a single-threaded io_context from the caller)
    auto executor = pool_.get_executor();

    // Copy seeds to avoid reference issues in coroutines
    std::vector<infrastructure::config::endpoint> seeds_copy(
        settings_.seeds.begin(), settings_.seeds.end());

    // Use task_group for structured concurrency - no detached!
    task_group seed_tasks("seed_tasks", executor);

    // Track seeds completed for early exit
    auto seeds_completed = std::make_shared<std::atomic<size_t>>(0);
    auto const total_seeds = seeds_copy.size();

    // Launch all seed connections in parallel
    for (auto const& seed : seeds_copy) {
        if (stopped()) break;

        auto task_name = fmt::format("seed_{}:{}:{}", seed.host(), seed.port(), seed_task_counter_.fetch_add(1));
        seed_tasks.spawn(task_name, [this, host = seed.host(), port = seed.port(), seeds_completed]() -> ::asio::awaitable<void> {
            co_await connect_to_seed(host, port, seeds_completed);
        });
    }

    spdlog::debug("[p2p_node] run_seeding: {} seed tasks spawned", seeds_copy.size());

    // Wait for seeds with early exit conditions
    // We use a timer loop to check for early exit while tasks run
    ::asio::steady_timer check_timer(executor);
    auto const max_wait = std::chrono::seconds(settings_.connect_timeout_seconds + 35);
    auto const start_time = std::chrono::steady_clock::now();

    while (seed_tasks.has_active_tasks() && !stopped()) {
        // Check elapsed time
        if (std::chrono::steady_clock::now() - start_time >= max_wait) {
            spdlog::debug("[p2p_node] Seeding timeout reached");
            break;
        }

        // Early exit if we have enough addresses
        if (auto const [avail, _] = peer_db_.count_by_status(); avail >= settings_.host_pool_capacity / 4) {
            spdlog::info("[p2p_node] Collected sufficient addresses, stopping seeding early");
            break;
        }

        // Wait a bit before checking again
        check_timer.expires_after(1s);
        co_await check_timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
    }

    // Wait for remaining tasks to complete (structured concurrency)
    co_await seed_tasks.join();

    auto const [available_after, _] = peer_db_.count_by_status();
    spdlog::info("[p2p_node] Seeding complete, {} addresses available", available_after);
}

::asio::awaitable<void> p2p_node::connect_to_seed(
    std::string seed_host,
    uint16_t seed_port,
    std::shared_ptr<std::atomic<size_t>> seeds_completed)
{
    spdlog::debug("[p2p_node] connect_to_seed: starting for {}:{}", seed_host, seed_port);

    // Use pool's executor explicitly for parallel operations
    auto executor = pool_.get_executor();

    try {
        auto result = co_await async_connect(
            executor,
            seed_host,
            seed_port,
            settings_,
            std::chrono::seconds(settings_.connect_timeout_seconds));

        if (!result) {
            spdlog::debug("[p2p_node] Failed to connect to seed {}:{} - {}",
                seed_host, seed_port, result.error().message());
            ++(*seeds_completed);
            co_return;
        }

        spdlog::debug("[p2p_node] connect_to_seed: connected to {}:{}", seed_host, seed_port);
        auto peer = *result;

        // Run the seeding protocol using structured concurrency:
        // peer->run() && seeding_protocol()
        // This eliminates the need for detached coroutines!
        co_await (
            peer->run() &&
            [&]() -> ::asio::awaitable<void> {
                // Generate nonce for handshake and set on peer for identification
                uint64_t nonce = generate_nonce();
                peer->set_nonce(nonce);

                // Perform handshake (uses channels - run() is running in parallel!)
                spdlog::debug("[p2p_node] connect_to_seed: performing handshake for {}:{}", seed_host, seed_port);
                auto config = make_handshake_config(settings_, top_block_.load().height(), nonce);
                auto handshake_result = co_await perform_handshake(*peer, config);

                if (!handshake_result) {
                    spdlog::debug("[p2p_node] Seed handshake failed {}:{} - {}",
                        seed_host, seed_port, handshake_result.error().message());
                    peer->stop();
                    co_return;
                }

                spdlog::debug("[p2p_node] connect_to_seed: handshake complete for {}:{}", seed_host, seed_port);

                // Send getaddr request
                auto ec = co_await peer->send(domain::message::get_address{});
                if (ec != error::success) {
                    peer->stop();
                    co_return;
                }

                // Wait for addr response with timeout
                // Note: Some nodes send a small self-announcement addr (1-2 addresses)
                // after handshake, before responding to getaddr with many addresses.
                // We accumulate addresses and stop after getting enough or timeout.
                ::asio::steady_timer timer(executor);
                timer.expires_after(30s);
                size_t total_addresses = 0;
                constexpr size_t minimum_useful_addresses = 10;

                while (!peer->stopped()) {
                    auto msg_result = co_await (
                        peer->messages().async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
                        timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
                    );

                    if (msg_result.index() == 1) {
                        // Timeout
                        break;
                    }

                    auto& [recv_ec, raw] = std::get<0>(msg_result);
                    if (recv_ec) {
                        break;
                    }

                    auto const& command = raw.heading.command();
                    if (command == domain::message::address::command ||
                        command == domain::message::addrv2::command) {

                        byte_reader reader(raw.payload);
                        infrastructure::message::network_address::list addresses;

                        if (command == domain::message::addrv2::command) {
                            // Parse as addrv2 (BIP155)
                            auto addrv2_result = domain::message::addrv2::from_data(
                                reader, peer->negotiated_version());
                            if (addrv2_result) {
                                addresses = addrv2_result->to_network_addresses();
                                spdlog::info("[p2p_node] Seed {}:{} sent ADDRV2: {} entries, {} IPv4/IPv6",
                                    seed_host, seed_port,
                                    addrv2_result->addresses().size(), addresses.size());
                            }
                        } else if (command == domain::message::address::command) {
                            // Parse as legacy addr
                            auto addr_result = domain::message::address::from_data(
                                reader, peer->negotiated_version());
                            if (addr_result) {
                                addresses = std::move(addr_result->addresses());
                                spdlog::info("[p2p_node] Seed {}:{} sent ADDR (legacy): {} entries",
                                    seed_host, seed_port, addresses.size());
                            }
                        } else {
                            spdlog::warn("[p2p_node] Seed {}:{} sent unknown address command: '{}'",
                                seed_host, seed_port, command);
                        }

                        if (!addresses.empty()) {
                            size_t stored = 0;
                            for (auto const& addr : addresses) {
                                if (peer_db_.store_address(addr)) {
                                    ++stored;
                                }
                            }
                            total_addresses += stored;
                            spdlog::debug("[p2p_node] Got {} addresses from seed {}:{} (total: {})",
                                stored, seed_host, seed_port, total_addresses);

                            // If we got enough addresses, stop waiting
                            if (total_addresses >= minimum_useful_addresses) {
                                break;
                            }
                        }
                    }
                }

                spdlog::info("[p2p_node] Seed {}:{} provided {} total addresses",
                    seed_host, seed_port, total_addresses);

                peer->stop();
            }()
        );
    } catch (std::exception const& e) {
        spdlog::debug("[p2p_node] Seed {} exception: {}", seed_host, e.what());
    }

    ++(*seeds_completed);
}

::asio::awaitable<void> p2p_node::run_outbound() {
    co_await maintain_outbound_connections();
}

::asio::awaitable<void> p2p_node::run_inbound() {
    if (settings_.inbound_port == 0) {
        spdlog::info("[p2p_node] Inbound connections disabled (port 0)");
        co_return;
    }

    auto executor = co_await ::asio::this_coro::executor;

    // Use async_listen from peer_session.hpp
    auto listen_result = co_await async_listen(executor, settings_.inbound_port);
    if (!listen_result) {
        spdlog::error("[p2p_node] Failed to start listening: {}", listen_result.error().message());
        co_return;
    }

    acceptor_ = std::make_unique<::asio::ip::tcp::acceptor>(std::move(*listen_result));

    // Wait for peer_supervisor to be ready (deterministic synchronization)
    while (!supervisor_ready_.load(std::memory_order_acquire) && !stopped()) {
        co_await ::asio::post(executor, ::asio::use_awaitable);
    }

    spdlog::info("[p2p_node] Listening on port {}", settings_.inbound_port);

    // Accept loop
    while (!stopped()) {
        // Use async_accept from peer_session.hpp
        auto result = co_await async_accept(*acceptor_, settings_);

        if (!result) {
            if (result.error() == error::service_stopped) {
                break;
            }
            spdlog::debug("[p2p_node] Accept error: {}", result.error().message());
            continue;
        }

        auto peer = *result;

        // Check if banned before proceeding
        if (peer_db_.is_banned(peer->authority())) {
            spdlog::debug("[p2p_node] Rejecting inbound connection from banned peer {}",
                peer->authority());
            peer->stop();
            continue;
        }

        // Send peer to supervisor for FULL lifecycle management (structured concurrency)
        // Supervisor will do: peer->run() && (handshake && add_to_manager && protocols)
        // No response channel needed for inbound - we don't wait for handshake result
        if (new_peer_channel_ && new_peer_channel_->is_open()) {
            co_await new_peer_channel_->async_send(
                std::error_code{},
                peer_event{peer, peer_direction::inbound, nullptr},
                ::asio::use_awaitable);
        } else {
            peer->stop();
        }
    }
}

::asio::awaitable<void> p2p_node::run_peer_protocols(peer_session::ptr peer) {
    spdlog::debug("[p2p_node] Starting protocols for peer [{}]", peer->authority());

    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer check_timer(executor);
    auto const ping_interval = std::chrono::seconds(settings_.channel_heartbeat_minutes * 60);
    auto const check_interval = std::chrono::seconds(5);  // Check stopped flag every 5s

    spdlog::debug("[p2p_node] Ping interval for [{}]: {}s", peer->authority(), ping_interval.count());

    auto last_ping = std::chrono::steady_clock::now();

    while (!peer->stopped() && !stopped()) {
        // Use short check_interval for responsive shutdown, but track ping timing separately
        check_timer.expires_after(check_interval);

        // Wait for message or check timer
        // Using short timer ensures we detect stopped status promptly
        auto result = co_await (
            peer->messages().async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
            check_timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
        );

        if (peer->stopped() || stopped()) {
            spdlog::debug("[p2p_node:protocols] [{}] Breaking due to stopped flag", peer->authority());
            break;
        }

        if (result.index() == 1) {
            // Timer expired - check if we need to send ping
            auto [timer_ec] = std::get<1>(result);
            if (timer_ec) {
                // Timer was cancelled (e.g., by message arrival) - this is fine
                continue;
            }

            // Check if it's time to send a ping
            auto now = std::chrono::steady_clock::now();
            if (now - last_ping >= ping_interval) {
                auto const nonce = generate_ping_nonce();
                domain::message::ping ping_msg(nonce);

                auto ec = co_await peer->send(ping_msg);
                if (ec != error::success) {
                    spdlog::debug("[p2p_node] Failed to send ping to [{}]", peer->authority());
                    break;
                }
                peer->record_ping_sent(nonce);
                spdlog::trace("[p2p_node] Sent ping to [{}]", peer->authority());
                last_ping = now;
            }
            continue;
        }

        // Message received
        auto& [ec, raw] = std::get<0>(result);
        if (ec) {
            spdlog::debug("[p2p_node:protocols] [{}] Message receive error: {}", peer->authority(), ec.message());
            break;
        }

        auto const& command = raw.heading.command();

        // Route by command before the dispatcher sees it.
        //
        // `block` and `addr`/`addrv2` go to the per-request channel a waiting
        // request is reading. `headers` is the one that has to be told apart
        // first — see below — and is CONSUMED here either way, as a response or
        // as an announcement. `cmpctblock` and `inv` are only OBSERVED: their
        // hashes are registered as announcements and the message carries on to
        // the dispatcher, so a handler registered for either still receives it.
        if (command == domain::message::headers::command) {
            // Nothing on the wire says which of the two this is, so the answer
            // comes from state only a request can set, claimed once (see
            // peer_session's "Header request attribution"). Claimed: this is
            // the answer to our getheaders. Not claimed: nobody asked, so it is
            // an announcement — and it does NOT go into the response channel,
            // where it would wait to be handed to some later request as if it
            // had answered it, and where a burst would suspend this loop.
            if (peer->claim_headers_response()) {
                auto [send_ec] = co_await peer->headers_responses().async_send(
                    std::error_code{}, raw, ::asio::as_tuple(::asio::use_awaitable));
                if (!send_ec) {
                    continue;  // Message delivered to response channel
                }
                // Channel closed - fall through to dispatcher
            } else {
                announce_from_headers(*peer, raw);
                continue;
            }
        } else if (command == domain::message::compact_block::command) {
            // BIP152 announcement. Its first field is the block header, which
            // is all this needs: the compact body is not reconstructed and no
            // BIP152 support is implied. These arrive because we asked for them
            // — `sendcmpct(true, 1)` goes out right after the handshake — and
            // the wedged run received eight per new block and understood none
            // of them (#706).
            announce_from_compact_block(*peer, raw);
            // Observed, not consumed: this reads the header out of it and lets
            // the message carry on to the dispatcher, so a handler registered
            // for `cmpctblock` still sees it. Only `headers` is taken here, and
            // that one is consumed either as a response or as an announcement.
        } else if (command == domain::message::inventory::command) {
            // The oldest of the three announcement forms: hashes, no headers.
            // Transaction entries are not this issue's business and are left
            // alone.
            announce_from_inventory(*peer, raw);
            // Observed, not consumed — as above, and it also carries the
            // transaction entries this path deliberately ignores, which a
            // handler registered for `inv` may well want.
        } else if (command == domain::message::block::command) {
            auto [send_ec] = co_await peer->block_responses().async_send(
                std::error_code{}, raw, ::asio::as_tuple(::asio::use_awaitable));
            if (!send_ec) {
                continue;
            }
        } else if (command == domain::message::address::command ||
                   command == domain::message::addrv2::command) {
            // Route both addr and addrv2 to the same channel
            // The receiver will check the command to know which parser to use
            auto [send_ec] = co_await peer->addr_responses().async_send(
                std::error_code{}, raw, ::asio::as_tuple(::asio::use_awaitable));
            if (!send_ec) {
                continue;
            }
        } else if (command == domain::message::get_address::command) {
            // Serve `getaddr`: reply with our known good peers (addrv2 when the
            // peer negotiated BIP-155, otherwise addr). Crawlers rely on this to
            // discover the network, so a silent non-answer hurts discoverability.
            auto const authorities = peer_db_.get_connectable(1000);
            infrastructure::message::network_address::list addresses;
            addresses.reserve(authorities.size());
            for (auto const& authority : authorities) {
                addresses.push_back(authority.to_network_address());
            }
            co_await send_addresses_auto(*peer, addresses);
            continue;
        }

        // Dispatch message through handlers
        bool should_continue = co_await dispatcher_.dispatch(*peer, raw);
        if (!should_continue) {
            spdlog::debug("[p2p_node] Handler requested disconnect for [{}]", peer->authority());
            break;
        }
    }

    // Cleanup
    spdlog::debug("[p2p_node] Ending protocols for peer [{}]", peer->authority());
    spdlog::debug("[p2p_node] Calling peer->stop() for [{}]", peer->authority());
    peer->stop();
    spdlog::debug("[p2p_node] Calling manager_.remove() for [{}]", peer->authority());
    co_await manager_.remove(peer);
    spdlog::debug("[p2p_node] manager_.remove() completed for [{}]", peer->authority());
}

void p2p_node::probe_status_task(status_probe_point point) const {
    if (status_probe_) {
        status_probe_(point);
    }
}

void p2p_node::wake_status_task() noexcept {
    // Posted to the STRAND, not to the pool. The wait runs on the pool and stop()
    // runs on whatever thread asked for it, and `pool_` has several threads — so
    // posting to its executor would put the cancellation on some thread while the
    // status coroutine is resuming into expires_after() on another. Same
    // executor is not serialized execution. The strand is what makes these two
    // mutually exclusive.
    //
    // @par Why the handler cannot outlive this object
    // The only threads that can run it are the pool's, and join() waits for them;
    // callers stop() and then join() before destroying this. A handler still
    // queued when the pool drains is destroyed with it.
    try {
        ::asio::post(status_strand_, [this] {
            // Guarded rather than passed an error_code: this asio has no
            // cancel(error_code&) overload, and cancel() throws on failure —
            // an exception out of a handler leaves the pool's run loop, which
            // is the thread's whole body.
            try {
                status_timer_.cancel();
            } catch (...) {
            }
        });
    } catch (...) {
    }
}

::asio::awaitable<void> p2p_node::maintain_outbound_connections() {
    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer timer(executor);

    // Wait for peer_supervisor to be ready (deterministic synchronization)
    // The && operator starts coroutines but doesn't guarantee execution order.
    while (!supervisor_ready_.load(std::memory_order_acquire) && !stopped()) {
        // Yield to allow peer_supervisor to start
        co_await ::asio::post(executor, ::asio::use_awaitable);
    }

    spdlog::info("[p2p_node] maintain_outbound_connections started, target: {} peers", settings_.outbound_connections);

    // Maximum number of parallel connection attempts
    // Should match or exceed outbound_connections target for faster peer acquisition
    constexpr size_t max_parallel_attempts = 32;

    // Spawn independent status logging task (doesn't block on connection attempts)
    //
    // On the STRAND rather than the pool: this task and the cancellation that
    // ends it both touch status_timer_, and only a strand makes them mutually
    // exclusive. Spawning on the pool's executor would leave the coroutine free
    // to resume into expires_after() on one thread while cancel() runs on
    // another.
    ::asio::co_spawn(status_strand_, [this]() -> ::asio::awaitable<void> {
        // The timer is a member now, not a local, so a stop can end this wait
        // instead of waiting it out. This task is detached and join() waits for
        // the pool it keeps alive, so ten seconds of sleeping here is ten seconds
        // of shutdown with nothing to do.
        while (!stopped()) {
            probe_status_task(status_probe_point::before_arm);

            // Re-read, because the window between the condition above and the
            // arming below is real: a stop can land in it, and without this the
            // task arms a ten-second wait only to be woken straight out of it.
            // The wake covers that; not arming at all is cheaper and says the
            // same thing.
            if (stopped()) {
                break;
            }

            status_timer_.expires_after(10s);
            probe_status_task(status_probe_point::armed);

            auto [ec] = co_await status_timer_.async_wait(::asio::as_tuple(::asio::use_awaitable));

            if (ec == ::asio::error::operation_aborted) {
                // A stop asked for this, and it is the only reason the wait ends
                // early. Deliberately silent and deliberately not a status line:
                // there is nothing left to report on, and a logger can throw
                // during a teardown.
                probe_status_task(status_probe_point::woken);
                break;
            }
            if (ec || stopped()) {
                break;
            }

            auto const current = manager_.count_snapshot();
            auto const target = settings_.outbound_connections;
            auto const [hosts, banned] = peer_db_.count_by_status();
            auto const pending = pending_connections_.size();
            probe_status_task(status_probe_point::reported);
            spdlog::info("[p2p_node:status] Peers: {}/{} (target), pending: {}, hosts: {}, banned: {}",
                current, target, pending, hosts, banned);
        }
    }, ::asio::detached);

    while (!stopped()) {
        auto const current_count = manager_.count_snapshot();
        auto const target = settings_.outbound_connections;
        auto const [host_count, ban_count] = peer_db_.count_by_status();
        auto const pending_count = pending_connections_.size();

        if (current_count < target) {
            // Need more connections
            auto const needed = target - current_count;

            spdlog::debug("[p2p_node] Connections: {}/{}, hosts: {}, banned: {}",
                current_count, target, host_count, ban_count);

            // Always try max_parallel_attempts to find working peers faster
            // Many addresses may be stale, so cast a wide net
            auto const batch_size = max_parallel_attempts;
            std::vector<domain::message::network_address> addresses;
            addresses.reserve(batch_size);

            size_t skipped_banned = 0;
            size_t skipped_connected = 0;
            size_t skipped_pending = 0;
            size_t skipped_cooldown = 0;

            // Cooldown duration for failed connections (30 seconds)
            constexpr auto failed_cooldown = 30s;
            auto const now_steady = std::chrono::steady_clock::now();

            // Clean up expired cooldown entries periodically
            failed_connections_.erase_if([&](auto const& entry) {
                return entry.second <= now_steady;
            });

            // Fetch addresses, filtering out duplicates and already-connected IPs
            for (size_t attempts = 0; attempts < batch_size * 3 && addresses.size() < batch_size && !stopped(); ++attempts) {
                domain::message::network_address addr;
                auto ec = peer_db_.fetch_address(addr);
                if (ec) {
                    break;
                }

                auto const authority = infrastructure::config::authority(addr);
                auto const ip = authority.asio_ip();

                // Skip if in cooldown from recent failure
                bool in_cooldown = false;
                failed_connections_.cvisit(ip, [&](auto const& entry) {
                    if (entry.second > now_steady) {
                        in_cooldown = true;
                    }
                });
                if (in_cooldown) {
                    ++skipped_cooldown;
                    continue;
                }

                // Skip if already pending connection to this IP
                if (pending_connections_.contains(ip)) {
                    ++skipped_pending;
                    continue;
                }

                // Skip if already connected to this IP
                if (co_await manager_.exists_by_ip(ip)) {
                    ++skipped_connected;
                    continue;
                }

                // Skip if banned
                if (peer_db_.is_banned(ip)) {
                    ++skipped_banned;
                    continue;
                }

                // Check for duplicate IP in current batch
                bool duplicate = false;
                for (auto const& existing : addresses) {
                    if (infrastructure::config::authority(existing).asio_ip() == ip) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) {
                    continue;
                }

                addresses.push_back(addr);
            }

            if (addresses.empty()) {
                spdlog::warn("[p2p_node] No addresses available (hosts: {}, banned: {}, skipped: {} banned, {} connected, {} pending, {} cooldown)",
                    host_count, ban_count, skipped_banned, skipped_connected, skipped_pending, skipped_cooldown);

                // Re-seed if host pool is too low
                if (host_count < settings_.host_pool_capacity / 4) {
                    spdlog::info("[p2p_node] Host pool low ({}), triggering re-seed...", host_count);
                    co_await run_seeding();
                }
            } else {
                spdlog::debug("[p2p_node] Attempting {} parallel connections (need {}, have {})",
                    addresses.size(), needed, current_count);

                // Use task_group for structured concurrency - no detached!
                task_group connection_tasks("connection_tasks", executor);

                for (auto const& addr : addresses) {
                    auto const authority = infrastructure::config::authority(addr);
                    auto const ip = authority.asio_ip();

                    // Add to pending connections before spawning
                    pending_connections_.insert(ip);

                    auto task_name = fmt::format("conn_{}:{}", authority, conn_task_counter_.fetch_add(1));
                    connection_tasks.spawn(task_name, [this, addr, ip, failed_cooldown]() -> ::asio::awaitable<void> {
                        auto const authority = infrastructure::config::authority(addr);
                        spdlog::trace("[p2p_node] Attempting connection to {}", authority);

                        auto result = co_await connect(authority.to_hostname(), authority.port());

                        // Remove from pending connections when done
                        pending_connections_.erase(ip);

                        if (!result) {
                            spdlog::trace("[p2p_node] Connection failed to {}: {}",
                                authority, result.error().message());
                            // Add to cooldown to prevent rapid reconnection
                            failed_connections_.insert_or_assign(ip,
                                std::chrono::steady_clock::now() + failed_cooldown);
                            if (peer_db_.remove_address(addr)) {
                                spdlog::trace("[p2p_node] Removed failed address {} from database", authority);
                            }
                        } else {
                            // Connection succeeded - remove from cooldown if present
                            failed_connections_.erase(ip);
                            spdlog::debug("[p2p_node] Connected to {}", authority);
                        }
                    });
                }

                // Wait for ALL connection attempts to complete (structured concurrency!)
                co_await connection_tasks.join();

                // Continue loop to check if we need more connections
                continue;
            }
        }

        // Wait before next check
        // Use shorter interval when we're below target to recover faster from bans
        // Keep wait_time short to allow quick shutdown response
        auto const wait_time = (current_count < target)
            ? std::chrono::milliseconds(500)  // Fast retry when below target
            : std::chrono::seconds(5);        // Normal check when at target

        timer.expires_after(wait_time);
        auto [ec] = co_await timer.async_wait(::asio::as_tuple(::asio::use_awaitable));
        if (ec) {
            break;  // Timer cancelled (shutdown)
        }
    }
}

// =============================================================================
// Peer Supervisor (Structured Concurrency)
// =============================================================================
//
// The peer_supervisor manages all peer lifecycles using the nursery pattern.
// Instead of spawning detached coroutines for each peer, all peer tasks are
// tracked in a task_group and properly awaited on shutdown.
//
// Flow:
//   1. run_inbound() and connect() send peer_events to new_peer_channel_
//   2. peer_supervisor receives events and spawns peer tasks into task_group
//   3. On stop(), stop_signal_ is triggered
//   4. peer_supervisor stops accepting new peers and waits for all tasks to complete
//
// =============================================================================

::asio::awaitable<void> p2p_node::peer_supervisor() {
    spdlog::debug("[p2p_node] peer_supervisor started");

    task_group peer_tasks("peer_tasks", pool_.get_executor());

    // Track ALL spawned peers (including those not yet in manager)
    // This is needed for clean shutdown - manager_.stop_all() only stops
    // peers that completed handshake, but we need to stop ALL peers.
    std::vector<peer_session::ptr> all_spawned_peers;

    // Signal that we're ready to receive peers (deterministic synchronization)
    supervisor_ready_.store(true, std::memory_order_release);

    while (!stopped()) {
        // Wait for new peer event
        // NOTE: We don't use || with stop_signal_ because the || operator in asio
        // waits for BOTH operations to complete, not just the first one.
        // Instead, stop() closes new_peer_channel_ which causes async_receive to return error.
        auto [ec, event] = co_await new_peer_channel_->async_receive(
            ::asio::as_tuple(::asio::use_awaitable));

        if (ec) {
            // Channel closed (stop() was called) or error - exit
            spdlog::debug("[p2p_node] peer_supervisor channel closed: {}", ec.message());
            break;
        }

        auto peer = event.peer;
        auto direction = event.direction;
        auto response_channel = event.response_channel;

        // Track this peer for shutdown
        all_spawned_peers.push_back(peer);

        spdlog::debug("[p2p_node] peer_supervisor received {} peer [{}]",
            direction == peer_direction::inbound ? "inbound" : "outbound",
            peer->authority());

        // Spawn FULL peer lifecycle into the task group (tracked, not detached!)
        // This runs: peer->run() && (handshake && add_to_manager && protocols)
        // The && operator ensures both branches run in parallel and we wait for both.
        // When peer disconnects, both exit and && completes.
        auto task_name = fmt::format("peer_{}_{}:{}",
            direction == peer_direction::inbound ? "in" : "out",
            peer->authority(), peer_task_counter_.fetch_add(1));
        peer_tasks.spawn(task_name, [this, peer, direction, response_channel]() -> ::asio::awaitable<void> {
            try {
                co_await (
                    [&]() -> ::asio::awaitable<void> {
                        co_await peer->run();
                        spdlog::debug("[p2p_node] Peer [{}] run() completed", peer->authority());
                    }() &&
                    [&]() -> ::asio::awaitable<void> {
                        // Generate nonce for handshake and set on peer for identification
                        uint64_t nonce = generate_nonce();
                        peer->set_nonce(nonce);
                        auto config = make_handshake_config(settings_, top_block_.load().height(), nonce);

                        // Perform handshake (uses channels - run() is already running!)
                        auto handshake_result = co_await perform_handshake(*peer, config);

                        if (!handshake_result) {
                            spdlog::debug("[p2p_node] Handshake failed for [{}]: {}",
                                peer->authority(), handshake_result.error().message());
                            // Send failure response if caller is waiting
                            if (response_channel) {
                                response_channel->try_send(std::error_code{}, handshake_response{handshake_result.error()});
                            }
                            peer->stop();
                            spdlog::debug("[p2p_node] Peer [{}] lambda completed (handshake failed)", peer->authority());
                            co_return;
                        }

                        // Add to peer manager
                        auto add_ec = co_await manager_.add(peer);
                        if (add_ec != error::success) {
                            spdlog::debug("[p2p_node] Failed to add peer [{}] to manager: {}",
                                peer->authority(), add_ec.message());
                            if (response_channel) {
                                response_channel->try_send(std::error_code{}, handshake_response{add_ec});
                            }
                            peer->stop();
                            spdlog::debug("[p2p_node] Peer [{}] lambda completed (add failed)", peer->authority());
                            co_return;
                        }

                        // Update peer_database with version info from handshake
                        if (auto ver = peer->peer_version(); ver) {
                            auto [record, result] = peer_db_.get_or_create(peer->authority());
                            using enum get_result;
                            if (result != created_not_stored) {
                                // Record is in database (was existing or just created)
                                record.user_agent = ver->user_agent();
                                record.services = ver->services();
                                record.record_success();
                                (void)peer_db_.update(record);
                            }
                            // If created_not_stored: at capacity, data is lost (acceptable)
                        }

                        // Track connection time to detect quick disconnects
                        auto const connection_start = std::chrono::steady_clock::now();

                        // Notify sync layer of new peer (CSP pattern)
                        spdlog::info("[p2p_node:peer_event] CONNECTED: {} (nonce={})",
                            peer->authority(), peer->nonce());
                        peer_notification_channel_->try_send(std::error_code{},
                            peer_notification{peer, peer_event_type::connected});

                        // Send initial ping to get latency data
                        {
                            auto const ping_nonce = generate_ping_nonce();
                            domain::message::ping ping_msg(ping_nonce);
                            auto ec = co_await peer->send(ping_msg);
                            if (ec == error::success) {
                                peer->record_ping_sent(ping_nonce);
                            }
                        }

                        // Announce our capabilities right after the handshake,
                        // the way BCHN does: prefer header announcements, offer
                        // compact blocks (BIP-152 v1), and advertise our relay
                        // fee filter (1 sat/byte). Best-effort.
                        co_await peer->send(domain::message::send_headers{});
                        co_await peer->send(domain::message::send_compact(true, 1));
                        co_await peer->send(domain::message::fee_filter{1000});

                        // Send success response to caller
                        if (response_channel) {
                            response_channel->try_send(std::error_code{}, handshake_response{error::success});
                        }

                        // Run protocols until peer disconnects
                        co_await run_peer_protocols(peer);

                        // Check if connection was too short (quick disconnect = unreliable peer)
                        auto const connection_duration = std::chrono::steady_clock::now() - connection_start;
                        constexpr auto min_connection_duration = 5s;
                        if (connection_duration < min_connection_duration) {
                            // Add to cooldown to prevent rapid reconnection
                            constexpr auto quick_disconnect_cooldown = 30s;
                            failed_connections_.insert_or_assign(
                                peer->authority().asio_ip(),
                                std::chrono::steady_clock::now() + quick_disconnect_cooldown);

                            // Report misbehavior (20 points per quick disconnect)
                            // After 5 quick disconnects (100 points), peer gets auto-banned by peer_database
                            if (peer_db_.add_misbehavior(peer->authority(), 20)) {
                                spdlog::info("[p2p_node] Peer {} auto-banned due to repeated quick disconnects",
                                    peer->authority());
                            }

                            spdlog::debug("[p2p_node] Quick disconnect from {} ({}ms), adding to cooldown and misbehavior",
                                peer->authority(),
                                std::chrono::duration_cast<std::chrono::milliseconds>(connection_duration).count());
                        }

                        // Notify sync layer of peer disconnection (CSP pattern)
                        spdlog::info("[p2p_node:peer_event] DISCONNECTED: {} (nonce={})",
                            peer->authority(), peer->nonce());
                        peer_notification_channel_->try_send(std::error_code{},
                            peer_notification{peer, peer_event_type::disconnected});

                        spdlog::debug("[p2p_node] Peer [{}] lambda completed (protocols ended)", peer->authority());
                    }()
                );
                spdlog::debug("[p2p_node] Peer [{}] && operator completed", peer->authority());
            } catch (std::exception const& e) {
                spdlog::debug("[p2p_node] Peer [{}] task exception: {}",
                    peer->authority(), e.what());
                // Send error response if caller is still waiting
                if (response_channel) {
                    response_channel->try_send(std::error_code{}, handshake_response{error::operation_failed});
                }
            }

            spdlog::debug("[p2p_node] Peer [{}] full lifecycle completed", peer->authority());
        });
    }

    spdlog::debug("[p2p_node] peer_supervisor stopping {} spawned peers before join",
        all_spawned_peers.size());

    // Stop ALL spawned peers (including those not yet in manager)
    // This ensures peers still in handshake phase are also stopped,
    // allowing their run() to exit and peer_tasks.join() to complete.
    for (auto& peer : all_spawned_peers) {
        peer->stop();
    }

    spdlog::debug("[p2p_node] peer_supervisor waiting for {} active peer tasks",
        peer_tasks.active_count());

    // Debug: identify which peers might be stuck
    for (auto& peer : all_spawned_peers) {
        if (!peer->stopped()) {
            spdlog::warn("[p2p_node] Peer [{}] still not stopped after stop() call!", peer->authority());
        }
    }

    // Log peers that are still "active" (their task hasn't completed)
    spdlog::info("[p2p_node] Waiting for peer_tasks.join() - if this hangs, check which peer task didn't complete");

    // CRITICAL: Wait for all peer tasks to complete
    // This is the structured concurrency guarantee - no orphaned tasks!
    co_await peer_tasks.join();

    spdlog::info("[p2p_node] peer_supervisor finished - all peer tasks completed");
}

uint64_t p2p_node::generate_nonce() {
    return pseudo_random::generate<uint64_t>();
}

// static
uint64_t p2p_node::generate_ping_nonce() {
    // peer_session tracks the in-flight ping in `pending_ping_nonce_`, using
    // zero to mean "none in flight" (see record_pong_received). A zero nonce
    // would therefore make the peer's own echo look unsolicited and drop the
    // latency sample, so never hand one out.
    //
    // Drawing over [1, max] says that directly. BCHN spells the same thing as a
    // retry loop (`while (nonce == 0) GetRandBytes(...)`) for want of a bounded
    // draw; ours rejects the same single value, just inside generate().
    return pseudo_random::generate<uint64_t>(1, max_uint64);
}

} // namespace kth::node
