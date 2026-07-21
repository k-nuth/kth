// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SETTINGS_HPP
#define KTH_NODE_SETTINGS_HPP

#include <cstdint>
#include <string>

#include <kth/domain.hpp>
#include <kth/infrastructure/display_mode.hpp>
#include <kth/infrastructure/utility/asio.hpp>
#include <kth/node/define.hpp>

namespace kth::node {

/// JSON-RPC server settings. Always present so config parsing is uniform; the
/// server itself is only compiled when KTH_WITH_RPC is defined and only started
/// when `enabled` is true.
struct KND_API rpc_settings {
    rpc_settings();

    /// Runtime on/off switch (config: rpc.enabled).
    bool enabled;
    /// Address to bind the RPC listener to (default 127.0.0.1 — localhost only).
    std::string bind;
    /// TCP port for the RPC listener (default 8332).
    uint16_t port;
    /// HTTP Basic-Auth credentials. When both are empty the server falls back to
    /// the auto-generated .cookie file (bitcoind-style).
    std::string user;
    std::string password;
};

/// Common database configuration settings, properties not thread safe.
struct KND_API settings {
    settings();
    settings(domain::config::network context);

    /// Properties.
    uint32_t sync_peers;
    uint32_t sync_timeout_seconds;
    uint32_t block_latency_seconds;
    bool refresh_transactions;
    bool compact_blocks_high_bandwidth;
    bool ds_proofs_enabled;

    /// Display mode for console output (tui, log, daemon)
    display_mode display{display_mode::log};

    /// Helpers.
    asio::duration block_latency() const;
};

} // namespace kth::node

#endif //KTH_NODE_SETTINGS_HPP
