// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/settings.hpp>

#include <kth/domain.hpp>

namespace kth::node {

using namespace kth::asio;

settings::settings()
    : sync_peers(0)
    , sync_timeout_seconds(5)
    , block_latency_seconds(60)
    , refresh_transactions(true)
    , compact_blocks_high_bandwidth(true)
    , ds_proofs_enabled(false)
{}

// There are no current distinctions spanning chain contexts.
settings::settings(domain::config::network context)
    : settings()
{}

duration settings::block_latency() const {
    return seconds(block_latency_seconds);
}

rpc_settings::rpc_settings()
    : enabled(false)
    , bind("127.0.0.1")
    , port(8332)
    , user()
    , password()
    , gbt_cache_size(10)
    , gbt_store_time(3600)
{}

sv2_settings::mining_settings::mining_settings()
    : enabled(false)
    , address()
{}

sv2_settings::sv2_settings()
    : enabled(false)
    , bind("127.0.0.1")
    , port(8442)
    , noise(true)
    , static_key_file(".sv2-static-key")
    , authority_key_file(".sv2-authority-key")
    , certificate_file()
    , authority_public_key()
    , certificate_validity_seconds(30 * 24 * 60 * 60)
    , handshake_timeout_seconds(10)
    , mining()
{}

} // namespace kth::node
