// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>

#include <kth/domain/message/version.hpp>
#include <kth/node/detail/block_service.hpp>

using namespace kth;
using namespace kth::node::detail;

using service = domain::message::version::service;

// =============================================================================
// Which heights a peer is asked for, from the services it advertises
// =============================================================================

TEST_CASE("block service: a peer that serves every block is asked from the start of the range",
          "[block_service]") {
    CHECK(lowest_height_served(service::node_network, 400, 400) == 0u);
    // Both bits: NODE_NETWORK says it holds them all.
    CHECK(lowest_height_served(service::node_network | service::node_network_limited, 400, 400) == 0u);
    // Other services alongside change nothing.
    CHECK(lowest_height_served(service::node_network | service::node_bloom, 5, 400) == 0u);
}

TEST_CASE("block service: a peer that serves only recent blocks is asked for its last 288",
          "[block_service]") {
    SECTION("at the header tip") {
        // 113..400: 288 blocks.
        CHECK(lowest_height_served(service::node_network_limited, 400, 400) == 113u);
    }

    SECTION("behind the header tip: counted from the tip") {
        // It holds no block above 350 yet, as a peer that serves all of them
        // would not; what it does hold of the last 288 it serves.
        CHECK(lowest_height_served(service::node_network_limited, 350, 400) == 113u);
    }

    SECTION("ahead of the header tip: counted from its own") {
        // At 500 it serves 213..500 and refuses below.
        CHECK(lowest_height_served(service::node_network_limited, 500, 400) == 213u);
    }

    SECTION("on a chain no longer than 288 blocks it serves all of it") {
        CHECK(lowest_height_served(service::node_network_limited, 287, 287) == 0u);
        CHECK(lowest_height_served(service::node_network_limited, 100, 100) == 0u);
        CHECK(lowest_height_served(service::node_network_limited, 288, 288) == 1u);
    }

    SECTION("with other services alongside") {
        CHECK(lowest_height_served(service::node_network_limited | service::node_bloom, 400, 400) == 113u);
    }
}

TEST_CASE("block service: without either bit nothing is restricted here",
          "[block_service]") {
    // Such a peer never reaches the block download: peer_session::is_full_node
    // is false for it, and the peer provider leaves it out.
    CHECK(lowest_height_served(service::none, 400, 400) == 0u);
    CHECK(lowest_height_served(service::node_bloom, 400, 400) == 0u);
}
