// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_ASSERT_HPP
#define KTH_INFRASTRUCTURE_ASSERT_HPP

#ifdef NDEBUG
    #define KTH_ASSERT(expression)
    #define KTH_ASSERT_MSG(expression, text)
    #define DEBUG_ONLY(expression)
#else
    #include <cassert>
    #define KTH_ASSERT(expression) assert(expression)
    #define KTH_ASSERT_MSG(expression, text) assert((expression)&&(text))
    #define DEBUG_ONLY(expression) expression
#endif

// `KTH_CONTRACT` is intended for contract checks whose violation would
// corrupt observable output (sighash preimage, signature verification,
// consensus-critical serialization). Unlike `KTH_ASSERT`, it stays live
// in Release builds — a failure aborts the process instead of silently
// letting a broken hash / signature / block reach the network. Use it
// only for cheap, provably-should-hold predicates on the write path.
#include <cstdlib>
#define KTH_CONTRACT(expression) do { if ( ! (expression)) std::abort(); } while (0)

// This is used to prevent bogus compiler warnings about unused variables.
#define UNUSED(expression) (void)(expression)

#endif
