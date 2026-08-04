// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include "reorg_chain_fixture.hpp"

using namespace kth;

// =============================================================================
// Reorg end-to-end: the real stack on a throwaway directory
// =============================================================================
//
// Everything merged for reorg support so far has been verified only in pieces.
// This brings up an actual block_chain — header index, flat-file block store,
// undo files and UTXO-Z together — which is the only place their disagreements
// can show up.

TEST_CASE("a real chain can be created and started on a temp directory", "[reorg][e2e]") {
    test::chain_fixture fixture("start");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());

    // Freshly initialized: genesis only, and the active chain resolves it.
    auto& chain = fixture.chain();
    CHECK(chain.headers().active_tip_height() == 0);
    CHECK(chain.headers().active_at(0) != blockchain::header_index::null_index);

    // The validated tip starts at genesis too — nothing above it is connected.
    auto const heights = chain.get_last_heights();
    REQUIRE(heights.has_value());
    CHECK(heights->block == 0);
}
