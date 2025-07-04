// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_TEST_HELPERS_HPP
#define KTH_INFRASTRUCTURE_TEST_HELPERS_HPP

#include <ostream>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include <kth/infrastructure/hash_define.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/istream_reader.hpp>

// Test macros
#define CHECK_MESSAGE(cond, msg) do { INFO(msg); CHECK(cond); } while((void)0, 0)
#define REQUIRE_MESSAGE(cond, msg) do { INFO(msg); REQUIRE(cond); } while((void)0, 0)

// Pretty printing for infrastructure types
namespace kth {

inline
std::ostream& operator<<(std::ostream& os, hash_digest const& x) {
    os << encode_hash(x);
    return os;
}

} // namespace kth

#endif // KTH_INFRASTRUCTURE_TEST_HELPERS_HPP
