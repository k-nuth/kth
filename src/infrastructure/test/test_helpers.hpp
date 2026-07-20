// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_TEST_HELPERS_HPP
#define KTH_INFRASTRUCTURE_TEST_HELPERS_HPP

#include <string>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>

#include <kth/infrastructure/hash_define.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>

// Test macros
#define CHECK_MESSAGE(cond, msg) do { INFO(msg); CHECK(cond); } while((void)0, 0)
#define REQUIRE_MESSAGE(cond, msg) do { INFO(msg); REQUIRE(cond); } while((void)0, 0)

// Pretty printing for infrastructure types via Catch2's StringMaker
// customization point (the idiomatic Catch2 form; avoids ADL-visible
// operator<< overloads).
namespace Catch {

template <>
struct StringMaker<kth::hash_digest> {
    static
    std::string convert(kth::hash_digest const& x) {
        return kth::encode_hash(x);
    }
};

} // namespace Catch

#endif // KTH_INFRASTRUCTURE_TEST_HELPERS_HPP
