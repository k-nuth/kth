// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// See protocols_coro.hpp for documentation on what this file replaces.

#include <kth/network/protocols_coro.hpp>

#include <algorithm>

#include <boost/unordered/unordered_flat_map.hpp>

#include <asio/co_spawn.hpp>
#include <asio/experimental/awaitable_operators.hpp>

namespace kth::network {

using namespace ::asio::experimental::awaitable_operators;
using namespace std::chrono_literals;

// =============================================================================
// Message Helpers
// =============================================================================

awaitable_expected<raw_message> wait_for_any_message(
    peer_session& peer,
    std::chrono::seconds timeout)
{
    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer timer(executor, timeout);

    auto result = co_await (
        peer.messages().async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
        timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
    );

    if (result.index() == 1) {
        co_return std::unexpected(error::channel_timeout);
    }

    auto& [ec, raw] = std::get<0>(result);
    if (ec) {
        co_return std::unexpected(error::channel_stopped);
    }

    co_return raw;
}

// =============================================================================
// Version Handshake Protocol
// =============================================================================

namespace {

domain::message::version make_version_message(
    handshake_config const& config,
    infrastructure::config::authority const& peer_authority)
{
    domain::message::version version;
    version.set_value(config.protocol_version);
    version.set_services(config.services);
    version.set_timestamp(uint64_t(zulu_time()));
    version.set_address_receiver(peer_authority.to_network_address());
    version.set_nonce(config.nonce);
    version.set_user_agent(config.user_agent);
    version.set_start_height(config.start_height);

    // The peer's services cannot be reflected, so zero it
    version.address_receiver().set_services(domain::message::version::service::none);

    // We match our declared services
    version.address_sender().set_services(config.services);

    return version;
}

bool is_user_agent_blacklisted(
    std::string const& user_agent,
    std::vector<std::string> const& blacklist)
{
    return std::any_of(blacklist.begin(), blacklist.end(),
        [&user_agent](std::string const& blacklisted) {
            if (blacklisted.size() <= user_agent.size()) {
                return std::equal(blacklisted.begin(), blacklisted.end(), user_agent.begin());
            }
            return std::equal(user_agent.begin(), user_agent.end(), blacklisted.begin());
        });
}

bool validate_peer_version(
    domain::message::version const& peer_version,
    handshake_config const& config,
    infrastructure::config::authority const& authority)
{
    // Check blacklist
    if (is_user_agent_blacklisted(peer_version.user_agent(), config.user_agent_blacklist)) {
        spdlog::debug("[protocol] Invalid user agent (blacklisted) for peer [{}] user agent: {}",
            authority, peer_version.user_agent());
        return false;
    }

    // Check invalid services
    if ((peer_version.services() & config.invalid_services) != 0) {
        spdlog::debug("[protocol] Invalid peer services ({}) for [{}]",
            peer_version.services(), authority);
        return false;
    }

    // Check minimum services
    if ((peer_version.services() & config.minimum_services) != config.minimum_services) {
        spdlog::debug("[protocol] Insufficient peer services ({}) for [{}]",
            peer_version.services(), authority);
        return false;
    }

    // Check minimum version
    if (peer_version.value() < config.minimum_version) {
        spdlog::debug("[protocol] Insufficient peer protocol version ({}) for [{}]",
            peer_version.value(), authority);
        return false;
    }

    return true;
}

} // anonymous namespace

awaitable_expected<handshake_result> perform_handshake(
    peer_session& peer,
    handshake_config const& config)
{
    auto const& authority = peer.authority();

    spdlog::debug("[protocol] Starting handshake with [{}]", authority);

    // Send our version message
    auto version_msg = make_version_message(config, authority);
    auto send_ec = co_await peer.send(version_msg);
    if (send_ec != error::success) {
        spdlog::debug("[protocol] Failed to send version to [{}]", authority);
        co_return std::unexpected(send_ec);
    }

    // We need to receive both version and verack from the peer
    // The order may vary, so we track what we've received
    bool got_version = false;
    bool got_verack = false;
    domain::message::version::const_ptr peer_version;

    auto deadline = std::chrono::steady_clock::now() + config.timeout;

    while (!got_version || !got_verack) {
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
            deadline - std::chrono::steady_clock::now());

        if (remaining <= 0s) {
            spdlog::debug("[protocol] Handshake timeout with [{}]", authority);
            co_return std::unexpected(error::channel_timeout);
        }

        // Wait for next message
        auto msg_result = co_await wait_for_any_message(peer, remaining);
        if (!msg_result) {
            spdlog::debug("[protocol] Failed to receive message from [{}]: {}",
                authority, msg_result.error().message());
            co_return std::unexpected(msg_result.error());
        }

        auto const& raw = *msg_result;
        auto const& command = raw.heading.command();

        if (command == domain::message::version::command && !got_version) {
            // Parse version message
            byte_reader reader(raw.payload);
            auto version_result = domain::message::version::from_data(reader, peer.negotiated_version());
            if (!version_result) {
                spdlog::debug("[protocol] Failed to parse version from [{}]", authority);
                co_return std::unexpected(error::bad_stream);
            }
            auto version = std::make_shared<domain::message::version>(std::move(*version_result));

            spdlog::debug("[protocol] Received version from [{}] protocol ({}) user agent: {}",
                authority, version->value(), version->user_agent());

            // Validate
            if (!validate_peer_version(*version, config, authority)) {
                co_return std::unexpected(error::channel_stopped);
            }

            peer_version = version;
            got_version = true;

            // Send verack in response
            auto verack_ec = co_await peer.send(domain::message::verack{});
            if (verack_ec != error::success) {
                spdlog::debug("[protocol] Failed to send verack to [{}]", authority);
                co_return std::unexpected(verack_ec);
            }
        }
        else if (command == domain::message::verack::command && !got_verack) {
            spdlog::debug("[protocol] Received verack from [{}]", authority);
            got_verack = true;
        }
        else {
            // Unexpected message during handshake - log but continue
            spdlog::debug("[protocol] Unexpected message '{}' during handshake with [{}]",
                command, authority);
        }
    }

    // Calculate negotiated version
    auto negotiated = std::min(peer_version->value(), config.protocol_version);

    // Update peer session
    peer.set_peer_version(peer_version);
    peer.set_negotiated_version(negotiated);

    spdlog::debug("[protocol] Handshake complete with [{}], negotiated version {}",
        authority, negotiated);

    co_return handshake_result{peer_version, negotiated};
}

// =============================================================================
// Direct Handshake (no message pump required)
// =============================================================================
//
// This version of handshake reads/writes directly to the socket without
// needing peer->run() to be running. This enables structured concurrency
// by eliminating the need to spawn run() with detached before handshake.
//
// After handshake completes, the peer is sent to peer_supervisor which
// spawns run() + protocols in a tracked task_group.
//
// =============================================================================

awaitable_expected<handshake_result> perform_handshake_direct(
    peer_session& peer,
    handshake_config const& config)
{
    auto const& authority = peer.authority();

    spdlog::debug("[protocol] Starting direct handshake with [{}]", authority);

    // Send our version message (directly to socket)
    auto version_msg = make_version_message(config, authority);
    auto send_ec = co_await peer.send_direct(version_msg);
    if (send_ec != error::success) {
        spdlog::debug("[protocol] Failed to send version to [{}]", authority);
        co_return std::unexpected(send_ec);
    }

    // We need to receive both version and verack from the peer
    // The order may vary, so we track what we've received
    bool got_version = false;
    bool got_verack = false;
    domain::message::version::const_ptr peer_version;

    auto deadline = std::chrono::steady_clock::now() + config.timeout;
    auto executor = co_await ::asio::this_coro::executor;

    while (!got_version || !got_verack) {
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
            deadline - std::chrono::steady_clock::now());

        if (remaining <= 0s) {
            spdlog::debug("[protocol] Handshake timeout with [{}]", authority);
            co_return std::unexpected(error::channel_timeout);
        }

        // Read message directly from socket with timeout
        // Use racing pattern with timer
        ::asio::steady_timer timer(executor, remaining);

        auto result = co_await (
            peer.read_message_direct() ||
            timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
        );

        // Check which completed first
        if (result.index() == 1) {
            // Timer won - timeout
            spdlog::debug("[protocol] Handshake timeout with [{}]", authority);
            co_return std::unexpected(error::channel_timeout);
        }

        // Message received
        auto& msg_result = std::get<0>(result);
        if (!msg_result) {
            spdlog::debug("[protocol] Failed to receive message from [{}]: {}",
                authority, msg_result.error().message());
            co_return std::unexpected(msg_result.error());
        }

        auto const& raw = *msg_result;
        auto const& command = raw.heading.command();

        if (command == domain::message::version::command && !got_version) {
            // Parse version message
            byte_reader reader(raw.payload);
            auto version_result = domain::message::version::from_data(reader, peer.negotiated_version());
            if (!version_result) {
                spdlog::debug("[protocol] Failed to parse version from [{}]", authority);
                co_return std::unexpected(error::bad_stream);
            }
            auto version = std::make_shared<domain::message::version>(std::move(*version_result));

            spdlog::debug("[protocol] Received version from [{}] protocol ({}) user agent: {}",
                authority, version->value(), version->user_agent());

            // Validate
            if (!validate_peer_version(*version, config, authority)) {
                co_return std::unexpected(error::channel_stopped);
            }

            peer_version = version;
            got_version = true;

            // Send verack in response (directly to socket)
            auto verack_ec = co_await peer.send_direct(domain::message::verack{});
            if (verack_ec != error::success) {
                spdlog::debug("[protocol] Failed to send verack to [{}]", authority);
                co_return std::unexpected(verack_ec);
            }
        }
        else if (command == domain::message::verack::command && !got_verack) {
            spdlog::debug("[protocol] Received verack from [{}]", authority);
            got_verack = true;
        }
        else {
            // Unexpected message during handshake - log but continue
            spdlog::debug("[protocol] Unexpected message '{}' during handshake with [{}]",
                command, authority);
        }
    }

    // Calculate negotiated version
    auto negotiated = std::min(peer_version->value(), config.protocol_version);

    // Update peer session
    peer.set_peer_version(peer_version);
    peer.set_negotiated_version(negotiated);

    spdlog::debug("[protocol] Direct handshake complete with [{}], negotiated version {}",
        authority, negotiated);

    co_return handshake_result{peer_version, negotiated};
}

handshake_config make_handshake_config(
    settings const& network_settings,
    uint32_t current_height,
    uint64_t nonce)
{
    return handshake_config{
        .protocol_version = network_settings.protocol_maximum,
        .services = network_settings.services,
        .invalid_services = network_settings.invalid_services,
        .minimum_version = network_settings.protocol_minimum,
        .minimum_services = domain::message::version::service::none,
        .user_agent = network_settings.user_agent,
        .start_height = current_height,
        .nonce = nonce,
        .timeout = std::chrono::seconds(network_settings.channel_handshake_seconds),
        .user_agent_blacklist = network_settings.user_agent_blacklist
    };
}

// =============================================================================
// Ping/Pong Protocol
// =============================================================================

::asio::awaitable<code> run_ping_pong(
    peer_session& peer,
    std::chrono::seconds ping_interval)
{
    spdlog::trace("[protocol] Starting ping/pong loop for [{}] with interval {}s",
        peer.authority(), ping_interval.count());

    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer ping_timer(executor);

    uint64_t last_ping_nonce = 0;

    while (!peer.stopped()) {
        // Wait for either:
        // 1. Ping interval expires -> send ping
        // 2. Message received -> check if it's ping/pong
        ping_timer.expires_after(ping_interval);

        auto result = co_await (
            peer.messages().async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
            ping_timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
        );

        if (peer.stopped()) {
            break;
        }

        if (result.index() == 1) {
            // Timer expired - send ping
            auto [timer_ec] = std::get<1>(result);
            if (!timer_ec) {
                pseudo_random::fill(reinterpret_cast<uint8_t*>(&last_ping_nonce), sizeof(last_ping_nonce));
                domain::message::ping ping_msg(last_ping_nonce);

                auto ec = co_await peer.send(ping_msg);
                if (ec != error::success) {
                    spdlog::debug("[protocol] Failed to send ping to [{}]", peer.authority());
                    co_return ec;
                }

                spdlog::trace("[protocol] Sent ping to [{}]", peer.authority());
            }
        }
        else {
            // Message received
            auto& [ec, raw] = std::get<0>(result);
            if (ec) {
                co_return error::channel_stopped;
            }

            auto const& command = raw.heading.command();

            if (command == domain::message::ping::command) {
                // Received ping - respond with pong
                byte_reader reader(raw.payload);
                auto ping_result = domain::message::ping::from_data(reader, peer.negotiated_version());
                if (ping_result) {
                    domain::message::pong pong_msg(ping_result->nonce());
                    auto pong_ec = co_await peer.send(pong_msg);
                    if (pong_ec != error::success) {
                        spdlog::debug("[protocol] Failed to send pong to [{}]", peer.authority());
                    }
                    spdlog::trace("[protocol] Responded pong to [{}]", peer.authority());
                }
            }
            else if (command == domain::message::pong::command) {
                // Received pong - could verify nonce matches our last ping
                byte_reader pong_reader(raw.payload);
                auto pong_result = domain::message::pong::from_data(pong_reader, peer.negotiated_version());
                if (pong_result) {
                    spdlog::trace("[protocol] Received pong from [{}]", peer.authority());
                }
            }
            // Other messages are ignored by this protocol
        }
    }

    co_return error::channel_stopped;
}

// =============================================================================
// Address Protocol
// =============================================================================

awaitable_expected<domain::message::address> request_addresses(
    peer_session& peer,
    std::chrono::seconds timeout)
{
    // Send getaddr
    auto ec = co_await peer.send(domain::message::get_address{});
    if (ec != error::success) {
        co_return std::unexpected(ec);
    }

    // Wait for addr response on the dedicated channel
    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer timer(executor, timeout);

    auto result = co_await (
        peer.addr_responses().async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
        timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
    );

    if (result.index() == 1) {
        co_return std::unexpected(error::channel_timeout);
    }

    auto& [recv_ec, raw] = std::get<0>(result);
    if (recv_ec) {
        co_return std::unexpected(error::channel_stopped);
    }

    // Parse the address message
    byte_reader reader(raw.payload);
    auto addr_result = domain::message::address::from_data(reader, peer.negotiated_version());
    if (!addr_result) {
        co_return std::unexpected(error::bad_stream);
    }

    co_return std::move(*addr_result);
}

::asio::awaitable<code> send_addresses(
    peer_session& peer,
    domain::message::address const& addresses)
{
    co_return co_await peer.send(addresses);
}

// =============================================================================
// Blockchain Protocol Handlers
// =============================================================================

// -----------------------------------------------------------------------------
// Header Sync Protocol
// -----------------------------------------------------------------------------

awaitable_expected<domain::message::headers> request_headers(
    peer_session& peer,
    hash_list const& locator_hashes,
    hash_digest const& stop_hash,
    std::chrono::seconds timeout)
{
    // Build getheaders message
    domain::message::get_headers request(locator_hashes, stop_hash);

    spdlog::debug("[protocol] Requesting headers from [{}] with {} locator hashes",
        peer.authority(), locator_hashes.size());

    // Send getheaders
    auto ec = co_await peer.send(request);
    if (ec != error::success) {
        spdlog::debug("[protocol] Failed to send getheaders to [{}]", peer.authority());
        co_return std::unexpected(ec);
    }

    // Wait for headers response on the dedicated channel
    // The message dispatcher routes 'headers' messages to this channel
    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer timer(executor, timeout);

    auto result = co_await (
        peer.headers_responses().async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
        timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
    );

    if (result.index() == 1) {
        spdlog::debug("[protocol] Timeout waiting for headers from [{}]", peer.authority());
        co_return std::unexpected(error::channel_timeout);
    }

    auto& [recv_ec, raw] = std::get<0>(result);
    if (recv_ec) {
        spdlog::debug("[protocol] Channel error waiting for headers from [{}]", peer.authority());
        co_return std::unexpected(error::channel_stopped);
    }

    // Parse the headers message
    byte_reader reader(raw.payload);
    auto headers_result = domain::message::headers::from_data(reader, peer.negotiated_version());
    if (!headers_result) {
        spdlog::debug("[protocol] Failed to parse headers from [{}]", peer.authority());
        co_return std::unexpected(error::bad_stream);
    }

    spdlog::debug("[protocol] Received {} headers from [{}]",
        headers_result->elements().size(), peer.authority());

    co_return std::move(*headers_result);
}

awaitable_expected<domain::message::headers> request_headers_from(
    peer_session& peer,
    hash_digest const& from_hash,
    std::chrono::seconds timeout)
{
    hash_list locator{from_hash};
    co_return co_await request_headers(peer, locator, null_hash, timeout);
}

// -----------------------------------------------------------------------------
// Block Sync Protocol
// -----------------------------------------------------------------------------

::asio::awaitable<code> request_blocks(
    peer_session& peer,
    hash_list const& block_hashes)
{
    if (block_hashes.empty()) {
        co_return error::success;
    }

    // Build inventory for block request
    domain::message::inventory_vector::list inventories;
    inventories.reserve(block_hashes.size());

    for (auto const& hash : block_hashes) {
        inventories.emplace_back(domain::message::inventory_vector::type_id::block, hash);
    }

    domain::message::get_data request(std::move(inventories));

    spdlog::debug("[protocol] Requesting {} blocks from [{}]",
        block_hashes.size(), peer.authority());

    // Send getdata - blocks will arrive asynchronously
    co_return co_await peer.send(request);
}

awaitable_expected<domain::message::block> request_block(
    peer_session& peer,
    hash_digest const& block_hash,
    std::chrono::seconds timeout)
{
    // Build single-block getdata
    domain::message::inventory_vector::list inventories{
        {domain::message::inventory_vector::type_id::block, block_hash}
    };
    domain::message::get_data request(std::move(inventories));

    spdlog::debug("[protocol] Requesting block {} from [{}]",
        encode_hash(block_hash), peer.authority());

    auto ec = co_await peer.send(request);
    if (ec != error::success) {
        co_return std::unexpected(ec);
    }

    // Wait for block response on the dedicated channel
    auto executor = co_await ::asio::this_coro::executor;
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true) {
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
            deadline - std::chrono::steady_clock::now());

        if (remaining <= 0s) {
            spdlog::debug("[protocol] Timeout waiting for block from [{}]", peer.authority());
            co_return std::unexpected(error::channel_timeout);
        }

        ::asio::steady_timer timer(executor, remaining);

        auto result = co_await (
            peer.block_responses().async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
            timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
        );

        if (result.index() == 1) {
            spdlog::debug("[protocol] Timeout waiting for block from [{}]", peer.authority());
            co_return std::unexpected(error::channel_timeout);
        }

        auto& [recv_ec, raw] = std::get<0>(result);
        if (recv_ec) {
            co_return std::unexpected(error::channel_stopped);
        }

        // Parse the block
        byte_reader reader(raw.payload);
        auto block_result = domain::message::block::from_data(reader, peer.negotiated_version());
        if (!block_result) {
            spdlog::debug("[protocol] Failed to parse block from [{}]", peer.authority());
            co_return std::unexpected(error::bad_stream);
        }

        // Verify it's the block we requested
        if (block_result->header().hash() != block_hash) {
            spdlog::debug("[protocol] Received unexpected block from [{}]", peer.authority());
            // Continue waiting for the correct block
            continue;
        }

        spdlog::debug("[protocol] Received block {} from [{}]",
            encode_hash(block_hash), peer.authority());

        co_return std::move(*block_result);
    }
}

awaitable_expected<std::vector<block_with_height>> request_blocks_batch(
    peer_session& peer,
    std::vector<std::pair<uint32_t, hash_digest>> const& blocks,
    std::chrono::seconds timeout)
{
    if (blocks.empty()) {
        co_return std::vector<block_with_height>{};
    }

    // Build hash -> height lookup map
    boost::unordered_flat_map<hash_digest, uint32_t> expected_blocks;
    expected_blocks.reserve(blocks.size());

    // Build inventory list for getdata
    domain::message::inventory_vector::list inventories;
    inventories.reserve(blocks.size());

    for (auto const& [height, hash] : blocks) {
        expected_blocks[hash] = height;
        inventories.emplace_back(domain::message::inventory_vector::type_id::block, hash);
    }

    domain::message::get_data request(std::move(inventories));

    spdlog::debug("[protocol] Requesting {} blocks in batch from [{}] ({}-{})",
        blocks.size(), peer.authority(),
        blocks.front().first, blocks.back().first);

    // Send ONE getdata with all block hashes
    auto ec = co_await peer.send(request);
    if (ec != error::success) {
        co_return std::unexpected(ec);
    }

    // Receive blocks as they arrive
    auto executor = co_await ::asio::this_coro::executor;
    auto deadline = std::chrono::steady_clock::now() + timeout;

    std::vector<block_with_height> received_blocks;
    received_blocks.reserve(blocks.size());

    while (received_blocks.size() < blocks.size()) {
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
            deadline - std::chrono::steady_clock::now());

        if (remaining <= 0s) {
            spdlog::debug("[protocol] Timeout waiting for batch blocks from [{}] ({}/{} received)",
                peer.authority(), received_blocks.size(), blocks.size());
            co_return std::unexpected(error::channel_timeout);
        }

        ::asio::steady_timer timer(executor, remaining);

        auto result = co_await (
            peer.block_responses().async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
            timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
        );

        if (result.index() == 1) {
            spdlog::debug("[protocol] Timeout waiting for batch blocks from [{}]", peer.authority());
            co_return std::unexpected(error::channel_timeout);
        }

        auto& [recv_ec, raw] = std::get<0>(result);
        if (recv_ec) {
            co_return std::unexpected(error::channel_stopped);
        }

        // Parse the block
        byte_reader reader(raw.payload);
        auto block_result = domain::message::block::from_data(reader, peer.negotiated_version());
        if (!block_result) {
            spdlog::debug("[protocol] Failed to parse block from [{}]", peer.authority());
            co_return std::unexpected(error::bad_stream);
        }

        // Look up the height for this block
        auto const block_hash = block_result->header().hash();
        auto it = expected_blocks.find(block_hash);
        if (it == expected_blocks.end()) {
            // Not a block we requested - ignore (could be from another request)
            spdlog::trace("[protocol] Received unexpected block from [{}], ignoring",
                peer.authority());
            continue;
        }

        auto height = it->second;
        expected_blocks.erase(it);

        spdlog::debug("[protocol] Received block {} (height {}) from [{}]",
            encode_hash(block_hash), height, peer.authority());

        received_blocks.push_back({height, std::move(*block_result)});
    }

    // Sort by height for caller convenience
    std::sort(received_blocks.begin(), received_blocks.end(),
        [](auto const& a, auto const& b) { return a.height < b.height; });

    spdlog::debug("[protocol] Received {} blocks in batch from [{}]",
        received_blocks.size(), peer.authority());

    co_return received_blocks;
}

// -----------------------------------------------------------------------------
// Inventory Protocol
// -----------------------------------------------------------------------------

::asio::awaitable<code> send_inventory(
    peer_session& peer,
    domain::message::inventory const& inv)
{
    spdlog::trace("[protocol] Sending {} inventory items to [{}]",
        inv.inventories().size(), peer.authority());
    co_return co_await peer.send(inv);
}

::asio::awaitable<code> send_getdata(
    peer_session& peer,
    domain::message::get_data const& request)
{
    spdlog::trace("[protocol] Sending getdata for {} items to [{}]",
        request.inventories().size(), peer.authority());
    co_return co_await peer.send(request);
}

// -----------------------------------------------------------------------------
// Transaction Protocol
// -----------------------------------------------------------------------------

::asio::awaitable<code> send_transaction(
    peer_session& peer,
    domain::message::transaction const& tx)
{
    spdlog::debug("[protocol] Sending transaction {} to [{}]",
        encode_hash(tx.hash()), peer.authority());
    co_return co_await peer.send(tx);
}

::asio::awaitable<code> request_mempool(
    peer_session& peer)
{
    spdlog::debug("[protocol] Requesting mempool from [{}]", peer.authority());
    co_return co_await peer.send(domain::message::memory_pool{});
}

// -----------------------------------------------------------------------------
// Response Helpers (for serving peers)
// -----------------------------------------------------------------------------

::asio::awaitable<code> send_headers(
    peer_session& peer,
    domain::message::headers const& headers)
{
    spdlog::trace("[protocol] Sending {} headers to [{}]",
        headers.elements().size(), peer.authority());
    co_return co_await peer.send(headers);
}

::asio::awaitable<code> send_block(
    peer_session& peer,
    domain::message::block const& block)
{
    spdlog::trace("[protocol] Sending block {} to [{}]",
        encode_hash(block.header().hash()), peer.authority());
    co_return co_await peer.send(block);
}

::asio::awaitable<code> send_not_found(
    peer_session& peer,
    domain::message::not_found const& not_found)
{
    spdlog::trace("[protocol] Sending not_found for {} items to [{}]",
        not_found.inventories().size(), peer.authority());
    co_return co_await peer.send(not_found);
}

// -----------------------------------------------------------------------------
// Message Parsing Helpers
// -----------------------------------------------------------------------------
// TODO(fernando): Consider making these generic with a single template function:
//   template <typename Message>
//   std::expected<Message, code> parse_message(raw_message const&, uint32_t version);
//
// TODO(fernando): These functions may be replaced by make_handler<Message>() from
//   p2p_node.hpp if we move to a fully handler-based architecture. For now, these
//   are useful for request/response protocols where we wait for specific messages.
// -----------------------------------------------------------------------------

std::expected<domain::message::get_headers, code> parse_getheaders(
    raw_message const& raw,
    uint32_t version)
{
    if (raw.heading.command() != domain::message::get_headers::command) {
        return std::unexpected(error::bad_stream);
    }
    byte_reader reader(raw.payload);
    auto result = domain::message::get_headers::from_data(reader, version);
    if (!result) {
        return std::unexpected(error::bad_stream);
    }
    return std::move(*result);
}

std::expected<domain::message::get_data, code> parse_getdata(
    raw_message const& raw,
    uint32_t version)
{
    if (raw.heading.command() != domain::message::get_data::command) {
        return std::unexpected(error::bad_stream);
    }
    byte_reader reader(raw.payload);
    auto result = domain::message::get_data::from_data(reader, version);
    if (!result) {
        return std::unexpected(error::bad_stream);
    }
    return std::move(*result);
}

std::expected<domain::message::inventory, code> parse_inventory(
    raw_message const& raw,
    uint32_t version)
{
    if (raw.heading.command() != domain::message::inventory::command) {
        return std::unexpected(error::bad_stream);
    }
    byte_reader reader(raw.payload);
    auto result = domain::message::inventory::from_data(reader, version);
    if (!result) {
        return std::unexpected(error::bad_stream);
    }
    return std::move(*result);
}

std::expected<domain::message::headers, code> parse_headers(
    raw_message const& raw,
    uint32_t version)
{
    if (raw.heading.command() != domain::message::headers::command) {
        return std::unexpected(error::bad_stream);
    }
    byte_reader reader(raw.payload);
    auto result = domain::message::headers::from_data(reader, version);
    if (!result) {
        return std::unexpected(error::bad_stream);
    }
    return std::move(*result);
}

std::expected<domain::message::block, code> parse_block(
    raw_message const& raw,
    uint32_t version)
{
    if (raw.heading.command() != domain::message::block::command) {
        return std::unexpected(error::bad_stream);
    }
    byte_reader reader(raw.payload);
    auto result = domain::message::block::from_data(reader, version);
    if (!result) {
        return std::unexpected(error::bad_stream);
    }
    return std::move(*result);
}

std::expected<domain::message::transaction, code> parse_transaction(
    raw_message const& raw,
    [[maybe_unused]] uint32_t version)
{
    if (raw.heading.command() != domain::message::transaction::command) {
        return std::unexpected(error::bad_stream);
    }
    byte_reader reader(raw.payload);
    auto result = domain::message::transaction::from_data(reader, true);
    if (!result) {
        return std::unexpected(error::bad_stream);
    }
    return std::move(*result);
}

// -----------------------------------------------------------------------------
// Message Loop Handler
// -----------------------------------------------------------------------------

::asio::awaitable<code> run_message_loop(
    peer_session& peer,
    message_handler handler)
{
    while (!peer.stopped()) {
        // Wait for next message (no timeout - runs until stopped)
        auto result = co_await peer.messages().async_receive(
            ::asio::as_tuple(::asio::use_awaitable));

        auto& [ec, raw] = result;
        if (ec) {
            co_return error::channel_stopped;
        }

        // Dispatch to handler
        bool continue_loop = co_await handler(raw);
        if (!continue_loop) {
            break;
        }
    }

    co_return error::channel_stopped;
}

} // namespace kth::network
