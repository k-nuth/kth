// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <string_view>

#include <kth/blockchain.hpp>
#include <kth/blockchain/validate/validate_header.hpp>
#include <kth/domain/config/parser.hpp>

// The checkpoints and headers below are BCH mainnet's.
#if defined(KTH_CURRENCY_BCH)

using namespace kth;
using namespace kth::blockchain;

namespace {

domain::chain::header header_from(std::string_view hex) {
    auto const data = decode_base16(hex);
    REQUIRE(data);
    byte_reader reader(*data);
    auto header = domain::chain::header::from_data(reader);
    REQUIRE(header);
    return *header;
}

// Mainnet 951146: the last block shared by the chain that took the May 2026
// upgrade and the chain that kept the previous rules.
constexpr std::string_view shared_951146 =
    "00600622607adf435cbfd7307eb190ba9750946f487083d6be4df6000000000000000000"
    "e96c9a57fedade8cca7a7f3815fcc704faaab7e8bd92672bfd931028ad4de5a6c417076a"
    "4d7801183f82aeae";

// 951147 on the chain that took the upgrade.
constexpr std::string_view upgraded_951147 =
    "000000221d3d5fb5ed63e8008a1d74e2d12930ea375f0ae915c55c000000000000000000"
    "e64c3052df958c367505d067535d081b50bf7a216a19cc634c53c9f96bfeab97481c076a"
    "f5770118f26abf2f";

// 951147 on the chain that kept the previous rules: the block where they split.
constexpr std::string_view previous_rules_951147 =
    "00c0ab281d3d5fb5ed63e8008a1d74e2d12930ea375f0ae915c55c000000000000000000"
    "95e8f51de07d45582701498fcb5466b36a24135043cb3ed4aa1af137525859b1ae581d6a"
    "f5770118572b62a8";

} // namespace

TEST_CASE("mainnet checkpoints refuse the chain that kept the pre-May-2026 rules where it split",
          "[checkpoints]") {
    auto const shared = header_from(shared_951146);
    auto const upgraded = header_from(upgraded_951147);
    auto const previous_rules = header_from(previous_rules_951147);
    auto const shared_hash = domain::chain::hash(shared);
    REQUIRE(upgraded.previous_block_hash() == shared_hash);
    REQUIRE(previous_rules.previous_block_hash() == shared_hash);

    // Fast mode and full validation alike: a conflicting checkpoint is refused in both.
    for (bool const full_validation : {false, true}) {
        CAPTURE(full_validation);
        blockchain::settings settings(domain::config::network::mainnet);
        settings.full_validation = full_validation;
        validate_header const validator(settings, domain::config::network::mainnet);

        CHECK(validator.accept(shared, 951146, shared.previous_block_hash()) == error::success);
        CHECK(validator.accept(upgraded, 951147, shared_hash) == error::success);
        CHECK(validator.accept(previous_rules, 951147, shared_hash) == error::checkpoints_failed);
    }
}

TEST_CASE("mainnet checkpoints: the historical ones, every 15,000 blocks and both sides of each upgrade",
          "[checkpoints]") {
    auto const list = domain::config::default_checkpoints(domain::config::network::mainnet);
    auto has = [&](size_t height) {
        return std::ranges::any_of(list, [&](auto const& c) { return c.height() == height; });
    };

    REQUIRE(std::ranges::is_sorted(list, {}, [](auto const& c) { return c.height(); }));
    REQUIRE(std::ranges::adjacent_find(list, {}, [](auto const& c) { return c.height(); }) == list.end());

    // The ones inherited from before the 2017 split.
    for (size_t const height : {0, 11'111, 33'333, 68'555, 70'567, 74'000, 105'000, 118'000, 134'444,
                                140'700, 168'000, 193'000, 210'000, 216'116, 225'430, 250'000, 279'000,
                                295'000, 300'000, 325'000, 350'000, 375'000, 400'000, 425'000, 450'000,
                                475'000}) {
        CAPTURE(height);
        CHECK(has(height));
    }

    // Every 15,000 blocks after the split.
    auto const last = list.back().height();
    for (size_t height = 500'000; height <= last; height += 15'000) {
        CAPTURE(height);
        CHECK(has(height));
    }

    // The last block under the previous rules and the first under the new ones.
    for (size_t const first : {478'559, 504'032, 530'356, 556'767, 582'680, 609'136, 635'259,
                               661'648, 740'238, 792'773, 845'891, 898'374}) {
        CAPTURE(first);
        CHECK(has(first - 1));
        CHECK(has(first));
    }
    // May 2026: where the chain that kept the previous rules split off, two
    // blocks after the activation.
    CHECK(has(951'146));
    CHECK(has(951'147));
}

#endif // defined(KTH_CURRENCY_BCH)
