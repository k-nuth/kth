// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CONSENSUS_TEST_HELPERS_HPP
#define KTH_CONSENSUS_TEST_HELPERS_HPP

#include <catch2/catch_test_macros.hpp>
#include <type_traits>

// Test macros
#define CHECK_MESSAGE(cond, msg) \
    do {                         \
        INFO(msg);               \
        CHECK(cond);             \
    } while ((void)0, 0)
#define REQUIRE_MESSAGE(cond, msg) \
    do {                           \
        INFO(msg);                 \
        REQUIRE(cond);             \
    } while ((void)0, 0)

// Note: consensus module is standalone and doesn't use infrastructure types
// If needed, add specific helpers for consensus types here

#endif  // KTH_CONSENSUS_TEST_HELPERS_HPP
