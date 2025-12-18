// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/protocols_coro.hpp>

#include <algorithm>

#include <asio/co_spawn.hpp>
#include <asio/experimental/awaitable_operators.hpp>

namespace kth::network {

using namespace ::asio::experimental::awaitable_operators;
using namespace std::chrono_literals;

// =============================================================================
// Message Helpers
// =============================================================================

::asio::awaitable<std::expected<raw_message, code>> wait_for_any_message(
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
    version.set_timestamp(static_cast<uint64_t>(zulu_time()));
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

::asio::awaitable<std::expected<handshake_result, code>> perform_handshake(
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

                spdlog::trace("[protocol] Sent ping to [{}] nonce={}", peer.authority(), last_ping_nonce);
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
                    spdlog::trace("[protocol] Received pong from [{}] nonce={}",
                        peer.authority(), pong_result->nonce());
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

::asio::awaitable<std::expected<domain::message::address, code>> request_addresses(
    peer_session& peer,
    std::chrono::seconds timeout)
{
    // Send getaddr
    auto ec = co_await peer.send(domain::message::get_address{});
    if (ec != error::success) {
        co_return std::unexpected(ec);
    }

    // Wait for addr response
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true) {
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
            deadline - std::chrono::steady_clock::now());

        if (remaining <= 0s) {
            co_return std::unexpected(error::channel_timeout);
        }

        auto msg_result = co_await wait_for_any_message(peer, remaining);
        if (!msg_result) {
            co_return std::unexpected(msg_result.error());
        }

        auto const& raw = *msg_result;
        if (raw.heading.command() == domain::message::address::command) {
            byte_reader reader(raw.payload);
            auto addr_result = domain::message::address::from_data(reader, peer.negotiated_version());
            if (!addr_result) {
                co_return std::unexpected(error::bad_stream);
            }
            co_return std::move(*addr_result);
        }
        // Continue waiting for addr message
    }
}

::asio::awaitable<code> send_addresses(
    peer_session& peer,
    domain::message::address const& addresses)
{
    co_return co_await peer.send(addresses);
}

} // namespace kth::network
