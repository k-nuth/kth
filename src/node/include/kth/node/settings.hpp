// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SETTINGS_HPP
#define KTH_NODE_SETTINGS_HPP

#include <cstdint>
#include <kth/domain.hpp>
#include <kth/node/define.hpp>

namespace kth::node {

/// Common database configuration settings, properties not thread safe.
class KND_API settings {
public:
    settings();
    settings(domain::config::network context);

    /// Properties.
    uint32_t sync_peers;
    uint32_t sync_timeout_seconds;
    uint32_t block_latency_seconds;
    bool refresh_transactions;
    bool compact_blocks_high_bandwidth;
    bool ds_proofs_enabled;

    /// Helpers.
    asio::duration block_latency() const;
};

} // namespace kth::node

#endif //KTH_NODE_SETTINGS_HPP
