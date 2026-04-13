// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>

#include <stdint.h>
#include <string.h>

#include <kth/capi/chain/history_compact.h>
#include <kth/capi/chain/history_compact_list.h>
#include <kth/capi/primitives.h>

#include "../test_helpers.hpp"

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("C-API HistoryCompact - destruct null is safe", "[C-API HistoryCompact]") {
    kth_chain_history_compact_destruct(NULL);
}

// ---------------------------------------------------------------------------
// Preconditions
// ---------------------------------------------------------------------------

TEST_CASE("C-API HistoryCompact - copy null aborts",
          "[C-API HistoryCompact][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_history_compact_copy(NULL));
}

TEST_CASE("C-API HistoryCompact - getter null aborts",
          "[C-API HistoryCompact][precondition]") {
    KTH_EXPECT_ABORT(kth_chain_history_compact_kind(NULL));
}

// ---------------------------------------------------------------------------
// List
// ---------------------------------------------------------------------------

TEST_CASE("C-API HistoryCompactList - construct default is empty", "[C-API HistoryCompactList]") {
    kth_history_compact_list_mut_t list = kth_chain_history_compact_list_construct_default();
    REQUIRE(list != NULL);
    REQUIRE(kth_chain_history_compact_list_count(list) == 0);
    kth_chain_history_compact_list_destruct(list);
}

TEST_CASE("C-API HistoryCompactList - destruct null is safe", "[C-API HistoryCompactList]") {
    kth_chain_history_compact_list_destruct(NULL);
}
