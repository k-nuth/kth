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
