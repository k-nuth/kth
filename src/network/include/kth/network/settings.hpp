// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_SETTINGS_HPP
#define KTH_NETWORK_SETTINGS_HPP

#include <cstddef>
#include <cstdint>

#include <kth/domain.hpp>
#include <kth/infrastructure.hpp>

#include <kth/network/define.hpp>

namespace kth::network {

/// Common database configuration settings, properties not thread safe.
class KN_API settings {
public:
    settings();
    settings(domain::config::network context);

    /// Properties.
    uint32_t threads;
    uint32_t protocol_maximum;
    uint32_t protocol_minimum;
    uint64_t services;
    uint64_t invalid_services;
    bool relay_transactions;
    bool validate_checksum;
    uint32_t identifier;
    uint16_t inbound_port;
    uint32_t inbound_connections;
    uint32_t outbound_connections;
    uint32_t manual_attempt_limit;
    uint32_t connect_batch_size;
    uint32_t connect_timeout_seconds;
    uint32_t channel_handshake_seconds;
    uint32_t channel_heartbeat_minutes;
    uint32_t channel_inactivity_minutes;
    uint32_t channel_expiration_minutes;
    uint32_t channel_germination_seconds;
    uint32_t host_pool_capacity;
    kth::path hosts_file;
    infrastructure::config::authority self;
    infrastructure::config::authority::list blacklist;
    infrastructure::config::endpoint::list peers;
    infrastructure::config::endpoint::list seeds;
    std::string user_agent;

    // [log]
    kth::path debug_file;
    kth::path error_file;
    kth::path archive_directory;
    size_t rotation_size;
    size_t minimum_free_space;
    size_t maximum_archive_size;
    size_t maximum_archive_files;
    infrastructure::config::authority statistics_server;
    bool verbose;
    bool use_ipv6;

    std::vector<std::string> user_agent_blacklist
#if defined(KTH_CURRENCY_BCH)
        {"/Bitcoin SV:"}
#endif
    ;

    /// Helpers.
    asio::duration connect_timeout() const;
    asio::duration channel_handshake() const;
    asio::duration channel_heartbeat() const;
    asio::duration channel_inactivity() const;
    asio::duration channel_expiration() const;
    asio::duration channel_germination() const;
};

} // namespace kth::network

#endif // KTH_NETWORK_SETTINGS_HPP
