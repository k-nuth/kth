// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_TEST_VMB_TESTS_HPP_
#define KTH_DOMAIN_TEST_VMB_TESTS_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <kth/domain.hpp>
#include <kth/infrastructure/utility/data.hpp>

#ifndef REQUIRE_MESSAGE
#define REQUIRE_MESSAGE(cond, msg) do { INFO(msg); REQUIRE(cond); } while((void)0, 0)
#endif
#ifndef CHECK_MESSAGE
#define CHECK_MESSAGE(cond, msg) do { INFO(msg); CHECK(cond); } while((void)0, 0)
#endif

namespace kth::test {

struct vmb_test_case {
    std::string ident;
    std::string description;
    std::string tx_hex;
    std::string utxos_hex;
    uint32_t input_num;
    domain::script_flags_t flags;
    bool expect_pass;
    bool script_only;
};

void run_vmb_test(vmb_test_case const& test);

} // namespace kth::test

using kth::test::run_vmb_test;

#endif // KTH_DOMAIN_TEST_VMB_TESTS_HPP_
