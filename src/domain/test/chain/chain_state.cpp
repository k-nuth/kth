// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <kth/domain/chain/chain_state.hpp>
#include <kth/domain/constants/bch.hpp>
#include <kth/domain/machine/script_flags.hpp>

using namespace kth;
using namespace kd;

namespace {

// `chain_state::activation` is protected; the test reaches it through a
// thin pass-through subclass instead of widening the production API.
struct chain_state_test_access : chain::chain_state {
    using chain::chain_state::activation;
};

// Helper: build a minimal `chain_state::data` whose median-time-past sits
// `offset` seconds above `anchor`. Eleven timestamps is what
// `median_time_past_interval` defaults to in the codebase; the median of
// `{anchor + offset, ..., anchor + offset + 10}` is `anchor + offset + 5`,
// which keeps the chosen MTP strictly above `anchor` for any non-negative
// offset, which is the property the test actually relies on.
chain::chain_state::data make_data_with_mtp(uint32_t mtp_anchor, int32_t offset = 5) {
    chain::chain_state::data values{};
    values.height = 951146;
    values.hash = null_hash;
    values.allow_collisions_hash = null_hash;

    values.bits.self = 0x1d00ffff;
    values.version.self = 4;
    values.timestamp.self = mtp_anchor + offset + 100;
    values.timestamp.retarget = mtp_anchor;

    constexpr size_t mtp_window = 11;
    for (size_t i = 0; i < mtp_window; ++i) {
        values.bits.ordered.push_back(0x1d00ffff);
        values.version.ordered.push_back(4);
        values.timestamp.ordered.push_back(mtp_anchor + offset + static_cast<int32_t>(i));
    }

    return values;
}

// Helper: minimal data at a given height (MTP irrelevant for height-gated rules).
chain::chain_state::data make_data_at_height(size_t height) {
    auto values = make_data_with_mtp(1500000000u);
    values.height = height;
    return values;
}

} // namespace

// Start Test Suite: chain_state tests

// Regression guard against the pre-fix `result.flags |= (to_flags(...) & flags)`
// pattern in `chain_state::activation()`. Before the fix (PR #340), the AND
// against the settings-derived `flags` mask silently stripped every feature
// bit that the upgrade introduced but the settings layer had not pre-listed.
// The Leibniz activation has since moved to a height gate, but the same
// failure mode would re-surface on the next MTP-gated activation if anyone
// reintroduces the `& flags` pattern there. Cantor (2027-May) is currently
// the last MTP-gated branch in `activation()`, so the test pins its
// behaviour: the upgrade's new bits must land in `result.flags` once MTP
// crosses the activation time, regardless of the settings mask.
TEST_CASE("chain_state activation enables Cantor feature bits once MTP passes the activation time", "[chain_state]") {
    using namespace kth::domain::machine;
    auto const cantor_t_value = std::to_underlying(bch_cantor_activation_time);

    // MTP comfortably past the activation time.
    auto const values = make_data_with_mtp(static_cast<uint32_t>(cantor_t_value));

    // Settings mask that lists every prior upgrade but not Cantor. This
    // mirrors the steady-state of `settings::enabled_flags()` while the
    // `bch_cantor` toggle stays commented out as a post-upgrade hardcode.
    // The Cantor-specific bits are deliberately absent — that is the input
    // the buggy pattern used to choke on.
    auto const settings_mask = domain::to_flags(upgrade::bch_leibniz);

    REQUIRE_FALSE(chain::script::is_enabled(settings_mask, script_flags::bch_2027_may));

    auto const result = chain_state_test_access::activation(
        values,
        settings_mask,
        domain::config::network::mainnet,
        bch_cantor_activation_time
    );

    // The feature bit the Cantor upgrade introduces must reach `result.flags`
    // once the MTP gate fires, regardless of what the settings mask did or
    // did not pre-list.
    REQUIRE(chain::script::is_enabled(result.flags, script_flags::bch_2027_may));
}

// Symmetric guard: when MTP is below the activation time the upgrade must
// stay dormant. Catches a hypothetical over-zealous fix that removes the
// MTP gate altogether and forces the bits on always.
TEST_CASE("chain_state activation leaves Cantor bits off while MTP is below the activation time", "[chain_state]") {
    using namespace kth::domain::machine;
    auto const cantor_t_value = std::to_underlying(bch_cantor_activation_time);

    // MTP a comfortable hour below the activation time.
    auto const values = make_data_with_mtp(static_cast<uint32_t>(cantor_t_value - 3600));

    auto const settings_mask = domain::to_flags(upgrade::bch_leibniz);

    auto const result = chain_state_test_access::activation(
        values,
        settings_mask,
        domain::config::network::mainnet,
        bch_cantor_activation_time
    );

    REQUIRE_FALSE(chain::script::is_enabled(result.flags, script_flags::bch_2027_may));
}

#if defined(KTH_CURRENCY_BCH)
// P2SH (BIP16) must activate by height, matching BCHN (consensus.BIP16Height,
// mainnet 173805), not by block timestamp. The date-based path enforced P2SH
// ~7000 blocks early (from ~166832), which wrongly rejected pre-enforcement
// P2SH-format spends (e.g. block 170060). Block 173804 has no P2SH; 173805 does.
TEST_CASE("chain_state activation gates BIP16 (P2SH) by height, matching BCHN", "[chain_state]") {
    using namespace kth::domain::machine;
    auto const mask = static_cast<domain::script_flags_t>(script_flags::bip16_rule);

    auto const below = chain_state_test_access::activation(
        make_data_at_height(173804), mask, domain::config::network::mainnet,
        bch_cantor_activation_time);
    REQUIRE_FALSE(chain::script::is_enabled(below.flags, script_flags::bip16_rule));

    auto const at = chain_state_test_access::activation(
        make_data_at_height(173805), mask, domain::config::network::mainnet,
        bch_cantor_activation_time);
    REQUIRE(chain::script::is_enabled(at.flags, script_flags::bip16_rule));
}
#endif // KTH_CURRENCY_BCH

#if defined(KTH_CURRENCY_BCH)
namespace {
chain::chain_state block_limit_state(domain::config::network network, bool activated,
                                     uint64_t control = 0, uint64_t buffer = 0) {
    constexpr uint32_t activation = 1810382400;
    auto values = make_data_with_mtp(activated ? activation : activation - 3600);
    values.height = 1'000'000;
    auto const cfg = chain::abla::default_config(static_max_block_size(network), false);
    values.abla_state = chain::abla::state(cfg, 0);
    if (control != 0) {
        values.abla_state.control_block_size = control;
        values.abla_state.elastic_buffer_size = buffer;
    }
#if __has_include(<kth/domain/chain/block_ticks.hpp>)
    // Harness adaptation: the old constructor has no branch schedule input.
    // All behavioral observations below use the existing public size API.
    values.faster_blocks_activation_height = activated ? 999'999 : max_size_t;
#endif
    static chain::chain_state::checkpoints const none{};
    return chain::chain_state(std::move(values),
        static_cast<domain::script_flags_t>(domain::machine::script_flags::retarget), none,
        network, {mainnet_asert_anchor_block_height, mainnet_asert_anchor_block_ancestor_time,
                  mainnet_asert_anchor_block_bits},
        2u * 24 * 60 * 60, cfg, cantor_t(activation)
    );
}
} // namespace

TEST_CASE("chain_state scales the chain-level floor with the candidate interval", "[chain_state][faster_blocks][block_limit]") {
    for (auto const network : {domain::config::network::mainnet, domain::config::network::chipnet}) {
        auto const before = block_limit_state(network, false);
        auto const after = block_limit_state(network, true);
        auto const floor = static_max_block_size(network);
        CHECK(before.dynamic_max_block_size() == floor);
        CHECK(after.dynamic_max_block_size() == floor / 10);
        CHECK(after.dynamic_max_block_sigchecks() == (floor / 10) / block_maxbytes_maxsigchecks_ratio);
    }
}

TEST_CASE("chain_state preserves grown ABLA state while scaling its candidate limit", "[chain_state][faster_blocks][block_limit]") {
    // A non-floor state must retain its throughput-equivalent size. This
    // would also catch resetting ABLA on activation or keeping an unscaled floor.
    auto const before = block_limit_state(domain::config::network::mainnet, false, 64'000'000, 64'000'000);
    auto const after = block_limit_state(domain::config::network::mainnet, true, 64'000'000, 64'000'000);
    CHECK(before.dynamic_max_block_size() == 128'000'000);
    CHECK(after.dynamic_max_block_size() == 12'800'000);
}

TEST_CASE("chain_state applies the two gigabyte cap after interval scaling", "[chain_state][faster_blocks][block_limit]") {
    auto const before = block_limit_state(domain::config::network::mainnet, false, 1'500'000'000, 1'500'000'000);
    auto const after = block_limit_state(domain::config::network::mainnet, true, 1'500'000'000, 1'500'000'000);
    CHECK(before.dynamic_max_block_size() == 2'000'000'000);
    CHECK(after.dynamic_max_block_size() == 300'000'000);
}
#endif

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("testnet minimum difficulty follows the candidate's shorter interval", "[chain_state][faster_blocks][easy_work]") {
    constexpr uint32_t parent_time = 2'000'000'000;
    chain::chain_state::data data{};
    data.height = 300001;
    data.hash = null_hash;
    data.bits.self = 0x1c00ffff;
    data.bits.ordered.push_back(0x1c00ffff);
    data.version.self = 4;
    data.timestamp.self = parent_time + 1;
    for (uint32_t i = 0; i < 11; ++i) {
        data.timestamp.ordered.push_back(parent_time - 600 + i * 60);
    }
    chain::chain_state::checkpoints const checkpoints;
    auto const flags = std::to_underlying(domain::machine::script_flags::retarget)
        | std::to_underlying(domain::machine::script_flags::easy_blocks);
#if __has_include(<kth/domain/chain/block_ticks.hpp>)
    data.faster_blocks_activation_height = 300000;
    chain::chain_state state(std::move(data), flags, checkpoints, domain::config::network::chipnet,
        {300000, parent_time - 600, 0x1c00ffff}, 3600,
        chain::abla::default_config(2'000'000), cantor_t{parent_time - 300});
#else
    chain::chain_state state(std::move(data), flags, checkpoints, domain::config::network::chipnet,
        {300000, parent_time - 600, 0x1c00ffff}, 3600,
        chain::abla::default_config(2'000'000), bch_cantor_activation_time);
#endif
    CHECK(state.get_next_work_required(parent_time + 120) != retarget_proof_of_work_limit);
    CHECK(state.get_next_work_required(parent_time + 121) == retarget_proof_of_work_limit);
    CHECK(state.get_next_work_required(parent_time + 1201) == retarget_proof_of_work_limit);
}
#endif

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("BCH CSV activation enables relative sequence locks and MTP finality together", "[chain_state][faster_blocks][csv]") {
    using namespace domain::machine;
    auto const mask = script_flags::bip68_rule | script_flags::bip112_rule | script_flags::bip113_rule;
    auto const before = chain_state_test_access::activation(
        make_data_at_height(mainnet_csv_activation_height - 1), mask,
        domain::config::network::mainnet, bch_cantor_activation_time);
    auto const at = chain_state_test_access::activation(
        make_data_at_height(mainnet_csv_activation_height), mask,
        domain::config::network::mainnet, bch_cantor_activation_time);
    for (auto rule : {script_flags::bip68_rule, script_flags::bip112_rule, script_flags::bip113_rule}) {
        CAPTURE(rule);
        CHECK_FALSE(chain::script::is_enabled(before.flags, rule));
        CHECK(chain::script::is_enabled(at.flags, rule));
    }
}
#endif

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("Cantor never activation stays disabled at the largest header timestamp", "[chain_state][faster_blocks][cantor_never]") {
    auto values = make_data_with_mtp(0);
    values.timestamp.self = UINT32_MAX;
    values.timestamp.ordered.assign(11, UINT32_MAX);
    chain::chain_state::checkpoints const checkpoints;
    auto const cfg = chain::abla::default_config(32'000'000, false);
    chain::chain_state state(std::move(values),
        std::to_underlying(domain::machine::script_flags::retarget), checkpoints,
        domain::config::network::mainnet,
        {mainnet_asert_anchor_block_height, mainnet_asert_anchor_block_ancestor_time,
            mainnet_asert_anchor_block_bits}, 2u * 24 * 60 * 60, cfg, cantor_t{UINT64_MAX});
    CHECK_FALSE(state.is_cantor_enabled());
    CHECK_FALSE(chain::script::is_enabled(state.enabled_flags(), domain::machine::script_flags::bch_2027_may));
#if __has_include(<kth/domain/chain/block_ticks.hpp>)
    CHECK_FALSE(state.is_faster_blocks_active());
    CHECK(state.ticks().schedule().empty());
    CHECK(state.block_interval() == 600);
#endif
}
#endif
