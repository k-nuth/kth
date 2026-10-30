// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstdlib>
#include <exception>
#include <fstream>

#include <catch2/internal/catch_test_failure_exception.hpp>
#include <test_helpers.hpp>

#include "bchn_vectors.hpp"
#include "vmb_tests.hpp"
#include "vmb_vectors.hpp"

namespace kth::test {
namespace {

// Optional execution evidence, one record per completed vector, including failed
// vectors. Use a fresh file and a single test process when comparing inventories.
class vector_trace {
public:
    vector_trace() {
        if (auto const* path = std::getenv("KTH_IMPORTED_VECTOR_TRACE")) {
            output_.open(path, std::ios::app);
            REQUIRE_MESSAGE(output_.is_open(), "Cannot open imported vector trace: " << path);
            enabled_ = true;
        }
    }

    void completed(char const* source, size_t index) {
        if (enabled_) {
            output_ << source << ':' << index << '\n';
            REQUIRE_MESSAGE(bool(output_), "Cannot write imported vector trace");
        }
    }

    void finish() {
        if (enabled_) {
            output_.flush();
            REQUIRE_MESSAGE(bool(output_), "Cannot flush imported vector trace");
        }
    }

private:
    std::ofstream output_;
    bool enabled_{false};
};

// A fatal assertion aborts only its vector. Catch has already recorded the
// failure; independent vectors must still run. Unexpected exceptions are also
// reported in the current vector's diagnostic scope before continuing.
template <typename Run>
void isolated_vector(Run&& run) {
    try {
        run();
    } catch (Catch::TestFailureException const&) {
    } catch (std::exception const& error) {
        FAIL_CHECK("Unexpected exception: " << error.what());
    } catch (...) {
        FAIL_CHECK("Unexpected non-standard exception");
    }
}

void run_bchn_vectors() {
    vector_trace trace;
    size_t executed = 0;
    for (auto const& group : bchn_vectors()) {
        for (auto const& e : group.rows) {
            INFO("BCHN script_tests / " << group.source << " / VM-AUTO #" << e.index);
            INFO("flags=" << e.flags << " amount=" << e.amount
                 << " expected_error=" << static_cast<int>(e.expected_error));
            INFO("scriptSig=" << e.script_sig << " scriptPubKey=" << e.script_pub_key);
            INFO("comment=" << e.comment);
            isolated_vector([&] {
                run_bchn_test({e.script_sig, e.script_pub_key, e.flags,
                               e.expected_error, e.comment, e.amount});
            });
            trace.completed(group.source, e.index);
            ++executed;
        }
    }
    trace.finish();
    CHECK(executed == 1758);
}

void run_vmb_vectors(std::span<vmb_vector_group const> groups, size_t expected_count) {
    vector_trace trace;
    size_t executed = 0;
    for (auto const& group : groups) {
        for (size_t index = 0; index < group.rows.size(); ++index) {
            auto const& e = group.rows[index];
            INFO("libauth/VMB / " << group.source << " / index=" << index << " id=" << e.ident);
            INFO("flags=" << group.flags << " expect_pass=" << group.expect_pass
                 << " input_num=" << e.input_num << " script_only=" << e.script_only);
            INFO("tx_hex=" << e.tx_hex << " utxos_hex=" << e.utxos_hex);
            INFO("description=" << e.description);
            isolated_vector([&] {
                run_vmb_test({e.ident, e.description, e.tx_hex, e.utxos_hex,
                              e.input_num, group.flags, group.expect_pass, e.script_only});
            });
            trace.completed(group.source, index);
            ++executed;
        }
    }
    trace.finish();
    CHECK(executed == expected_count);
}

} // namespace

TEST_CASE("BCHN script vectors", "[vm][auto]") {
    run_bchn_vectors();
}

TEST_CASE("VMB 2022 invalid vectors", "[vmb][2022][invalid]") {
    run_vmb_vectors(vmb_2022_invalid_vectors(), 1876);
}

TEST_CASE("VMB 2022 nonstandard vectors", "[vmb][2022][nonstandard]") {
    run_vmb_vectors(vmb_2022_nonstandard_vectors(), 454);
}

TEST_CASE("VMB 2022 standard vectors", "[vmb][2022][standard]") {
    run_vmb_vectors(vmb_2022_standard_vectors(), 156);
}

TEST_CASE("VMB 2023 invalid vectors", "[vmb][2023][invalid]") {
    run_vmb_vectors(vmb_2023_invalid_vectors(), 20348);
}

TEST_CASE("VMB 2023 nonstandard vectors", "[vmb][2023][nonstandard]") {
    run_vmb_vectors(vmb_2023_nonstandard_vectors(), 2428);
}

TEST_CASE("VMB 2023 standard vectors", "[vmb][2023][standard]") {
    run_vmb_vectors(vmb_2023_standard_vectors(), 5217);
}

TEST_CASE("VMB 2025 invalid vectors", "[vmb][2025][invalid]") {
    run_vmb_vectors(vmb_2025_invalid_vectors(), 13840);
}

TEST_CASE("VMB 2025 nonstandard vectors", "[vmb][2025][nonstandard]") {
    run_vmb_vectors(vmb_2025_nonstandard_vectors(), 7960);
}

TEST_CASE("VMB 2025 standard vectors", "[vmb][2025][standard]") {
    run_vmb_vectors(vmb_2025_standard_vectors(), 7871);
}

TEST_CASE("VMB 2026 invalid vectors", "[vmb][2026][invalid]") {
    run_vmb_vectors(vmb_2026_invalid_vectors(), 13159);
}

TEST_CASE("VMB 2026 nonstandard vectors", "[vmb][2026][nonstandard]") {
    run_vmb_vectors(vmb_2026_nonstandard_vectors(), 2289);
}

TEST_CASE("VMB 2026 standard vectors", "[vmb][2026][standard]") {
    run_vmb_vectors(vmb_2026_standard_vectors(), 14223);
}

} // namespace kth::test
