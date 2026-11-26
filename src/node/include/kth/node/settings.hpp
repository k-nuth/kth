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

/// Stratum V2 Template Provider settings. The listener is disabled by default,
/// binds to localhost, and speaks plaintext frames unless `noise` is set.
struct KND_API sv2_settings {
    bool enabled = false;
    std::string bind = "127.0.0.1";
    uint16_t port = 8442;
    /// The Noise handshake and encrypted frames (sv2-spec 4) on every connection.
    bool noise = false;
    /// The Noise key and certificate files. Empty or relative paths are taken
    /// from the data directory: sv2_static_key and sv2_authority_key there by
    /// default, created on the first start with `noise`.
    std::string static_key_file;
    std::string authority_key_file;
    /// A certificate signed offline (74 bytes, sv2-spec 4.5.2). With one, the
    /// authority's secret key is not on the node and `authority_public_key`
    /// is required.
    std::string certificate_file;
    /// The authority public key in the sv2-spec 4.7 encoding: required with
    /// `certificate_file`, and checked against `authority_key_file` otherwise.
    std::string authority_public_key;
    /// The lifetime of the certificates the node signs.
    uint32_t certificate_validity_seconds = 30 * 24 * 60 * 60;
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
    // Serve compact block filters to light wallets (the cfilter messages). Read
    // only by a build with the option `neutrino`.
    bool compact_filters;
    // Serve wallets that load a bloom filter (BIP37). On unless turned off in
    // a build with the option `bloom_filters`, as on other Bitcoin Cash nodes;
    // off, and refused, in a build without it.
    bool bloom_filters;

    /// Display mode for console output (tui, log, daemon)
    display_mode display{display_mode::log};

    /// Helpers.
    asio::duration block_latency() const;
};

} // namespace kth::node

#endif //KTH_NODE_SETTINGS_HPP
