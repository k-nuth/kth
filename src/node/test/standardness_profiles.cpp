// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(KTH_CURRENCY_BCH)
#include <catch2/generators/catch_generators.hpp>
#include "bchn_standardness_harness.hpp"

using namespace kth;
using namespace kth::test::standardness;

namespace {

// Funding output 4 has a 201-byte OP_NOP...OP_1 script. Its empty scriptSig
// avoids signature-length variation at the serialized-size boundary.
chain::transaction sized_spend(harness& h, size_t target) {
    chain::output::list outputs;
    for (size_t i = 0; i < (target - 100) / 34; ++i)
        outputs.emplace_back(1000, script_of(p2pkh()), chain::token_data_opt{});
    outputs.emplace_back(0, script_of({0x6a}), chain::token_data_opt{});
    auto const initial = h.spend(4, chain::output::list(outputs));
    REQUIRE(initial.serialized_size(true) <= target);
    data_chunk padding(1 + target - initial.serialized_size(true), 0x00);
    padding[0] = 0x6a;
    REQUIRE(padding.size() <= 223);
    outputs.back().set_script(script_of(padding));
    auto tx = h.spend(4, std::move(outputs));
    REQUIRE(tx.serialized_size(true) == target);
    tx.inputs()[0].previous_output().validation.cache = h.funding.outputs()[4];
    return tx;
}

} // namespace

TEST_CASE("Knuth admission and BCHN size diagnostics are independent", "[standardness_profiles][bchn_non_vm]") {
    auto const require_standard = GENERATE(true, false);
    for (auto size : {100000u, 100001u}) {
        DYNAMIC_SECTION("size " << size << " Knuth standardness " << require_standard) {
            harness h("dual_policy_sizes", require_standard);
            auto const tx = sized_spend(h, size);
            CHECK(domain::policy::check_transaction_standardness(tx) == error::success);
            CHECK(domain::policy::check_input_standardness_bchn(tx, h.flags()) == error::success);
            auto const bchn = size > 100000 ? code(error::standardness_transaction_size) : code(error::success);
            CHECK(domain::policy::check_transaction_standardness_bchn(tx, h.flags()) == bchn);
            auto const channel = h.subscribe();
            CHECK(h.organize(tx) == error::success);
            CHECK(h.pooled(tx));
            CHECK(harness::announced(channel, tx.hash()));
            h.fixture.chain().unsubscribe_transaction(channel);
        }
    }
}

TEST_CASE("BCHN rejection reasons can accompany transactions admitted by Knuth", "[standardness_profiles][bchn_non_vm]") {
    auto const require_standard = GENERATE(true, false);
    for (bool dust : {false, true}) {
        DYNAMIC_SECTION("dust " << dust << " Knuth standardness " << require_standard) {
            harness h("dual_policy_reasons", require_standard);
            auto const tx = h.spend(0, dust ? harness::pay(p2pkh(), 545) : harness::pay(null_data(224), 0));
            auto const reason = dust ? code(error::standardness_dust) : code(error::standardness_datacarrier_size);
            CHECK(domain::policy::check_transaction_standardness_bchn(tx, h.flags()) == reason);
            CHECK(h.validate(tx) == error::success);
            CHECK(h.organize(tx) == error::success);
            CHECK(h.pooled(tx));
        }
    }
}

TEST_CASE("BCHN phase diagnostics do not impose BCHN policy on Knuth", "[standardness_profiles][bchn_non_vm]") {
    auto const require_standard = GENERATE(true, false);
    harness h("dual_policy_phase", require_standard, phase::before_u12);
    auto const tx = h.spend(0, harness::pay(bare_one()));
    CHECK(domain::policy::check_transaction_standardness_bchn(tx, h.flags()) == error::standardness_scriptpubkey);
    CHECK(h.organize(tx) == error::success);
    CHECK(h.pooled(tx));
}
TEST_CASE("BCHN script policy flags remain independent of Knuth consensus flags", "[standardness_profiles][bchn_non_vm]") {
    harness h("dual_policy_script_flags");
    for (uint32_t index : {5u, 6u}) {
        DYNAMIC_SECTION("witness program " << index) {
            auto tx = h.spend_segwit_recovery(index);
            tx.inputs()[0].previous_output().validation.cache = h.funding.outputs()[index];
            CHECK(domain::policy::check_transaction_standardness_bchn(tx, h.flags()) == error::success);
            CHECK(domain::policy::check_input_standardness_bchn(tx, h.flags()) == error::success);
            CHECK(chain::script::verify(tx, 0, h.flags()) == error::success);
            auto const flags = domain::policy::get_standard_script_flags_bchn(h.flags());
            CHECK(chain::script::verify(tx, 0, flags) == error::cleanstack);
            CHECK(h.organize(tx) == error::success);
        }
    }
}
#endif
