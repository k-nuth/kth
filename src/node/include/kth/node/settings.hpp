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

/// RPC server settings, shared by whichever backend the build selected (JSON-RPC
/// when KTH_WITH_RPC is defined, gRPC when KTH_WITH_GRPC is). Always present so
/// config parsing is uniform; the server is only started when `enabled` is true.
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
    /// getblocktemplatelight job cache: how many recent jobs to keep, and for how
    /// long (seconds) before a job expires. Mirrors bitcoind -gbtcachesize /
    /// -gbtstoretime.
    uint32_t gbt_cache_size;
    uint32_t gbt_store_time;
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

    /// LOOPBACK:PORT of the local Electrum listener of the node's own wallet
    /// support, for wallets on this machine that do not speak P2P; empty for
    /// none (the default). Answers come from this node's own chain. Only a
    /// loopback address (127.0.0.0/8, [::1]) is accepted: a node must not
    /// learn what other people's wallets watch.
    std::string wallet_electrum;

    /// The first height the node's own wallet support reads for a wallet
    /// that does not give one: no wallet it answers received anything
    /// before it. 0 reads from genesis.
    uint32_t wallet_birthday = 0;

    /// Display mode for console output (tui, log, daemon)
    display_mode display{display_mode::log};

    /// Helpers.
    asio::duration block_latency() const;
};

} // namespace kth::node

#endif //KTH_NODE_SETTINGS_HPP
