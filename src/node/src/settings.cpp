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
{}

} // namespace kth::node
