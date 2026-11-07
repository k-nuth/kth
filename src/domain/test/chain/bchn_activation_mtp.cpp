// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// activation_tests.cpp isupgrade2027enabled / test_upgrade2027_activation_block_tracking of Bitcoin Cash Node (commit
// 07576013c91ff4a3a74acd85f189c69121cdad1b): the May 2027 upgrade activates when the median time past of the parent is at
// least upgrade2027ActivationTime (1810382400 on mainnet), not before. chain_state::activation turns the median time
// past of the eleven timestamps it is given into the rule flags.
// Original: Copyright (c) 2018-present The Bitcoin developers, MIT license.
//
// Not ported: BCHN's activation block tracker cache (a lookup structure for non-active chains), the nullptr parent, and
// IsAxionEnabled (a Nov 2020 MTP activation for networks without a hard-coded height: Knuth keeps a height per network,
// which bchn_activation_heights.cpp compares).

#include <test_helpers.hpp>

#if defined(KTH_CURRENCY_BCH)

#include <array>

#include <kth/domain/chain/chain_state.hpp>

using namespace kth;
namespace chain = kth::domain::chain;
using kth::domain::machine::script_flags;

namespace {

struct chain_state_test_access : chain::chain_state {
    using chain::chain_state::activation;
    using chain::chain_state::median_time_past;
};

constexpr domain::script_flags_t everything = ~domain::script_flags_t(0);
constexpr uint32_t activation_time = 1810382400;

// A window whose eleven timestamps put the median time past at `mtp`
chain::chain_state::data window_with_mtp(uint32_t mtp) {
    chain::chain_state::data values{};
    values.height = 1'000'000;
    values.hash = null_hash;
    values.allow_collisions_hash = null_hash;
    values.bits.self = 0x1d00ffff;
    values.version.self = 4;
    values.timestamp.self = mtp;
    values.timestamp.retarget = mtp;
    for (size_t i = 0; i < 11; ++i) {
        values.bits.ordered.push_back(0x1d00ffff);
        values.version.ordered.push_back(4);
        values.timestamp.ordered.push_back(mtp);
    }
    return values;
}

bool cantor_active(uint32_t mtp) {
    auto const flags = chain_state_test_access::activation(window_with_mtp(mtp), everything,
        domain::config::network::mainnet, bch_cantor_activation_time).flags;
    return chain::script::is_enabled(flags, script_flags::bch_2027_may);
}

} // namespace

TEST_CASE("BCHN activation_tests - isupgrade2027enabled", "[bchn_non_vm][activation][mtp]") {
    REQUIRE(std::to_underlying(bch_cantor_activation_time) == activation_time);
    CHECK_FALSE(cantor_active(activation_time - 1000000));
    CHECK_FALSE(cantor_active(activation_time - 1));
    CHECK(cantor_active(activation_time));
    CHECK(cantor_active(activation_time + 1));
}

// blockindex_tests.cpp median_time_past: the median of the last (up to) eleven block times, the block itself included. Each row is
// <block time, expected median time past> for a chain of 12 blocks; two series, one with consecutive and one with
// non-consecutive block times.
TEST_CASE("BCHN blockindex_tests - median_time_past", "[bchn_non_vm][mtp]") {
    using row = std::pair<uint32_t, uint32_t>;
    std::array<row, 12> const consecutive{{{0, 0}, {1, 1}, {2, 1}, {4, 2}, {4, 2}, {5, 4}, {7, 4}, {10, 4}, {12, 4}, {14, 5}, {17, 5}, {20, 7}}};
    std::array<row, 12> const irregular{{{0, 0}, {0, 0}, {1, 0}, {3, 1}, {2, 1}, {3, 2}, {4, 2}, {5, 3}, {6, 3}, {7, 3}, {8, 3}, {9, 4}}};
    for (auto const* series : {&consecutive, &irregular}) {
        for (size_t i = 0; i < series->size(); ++i) {
            chain::chain_state::data values{};
            size_t const first = i >= 10 ? i - 10 : 0;
            for (size_t j = first; j <= i; ++j) values.timestamp.ordered.push_back((*series)[j].first);
            CHECK(chain_state_test_access::median_time_past(values) == (*series)[i].second);
        }
    }
}

#endif // KTH_CURRENCY_BCH
