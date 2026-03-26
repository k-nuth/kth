// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <limits>

#include <kth/domain/wallet/coin_selection.hpp>

using namespace kth;
using namespace kth::domain::chain;
using namespace kth::domain::wallet;

// ---------------------------------------------------------------------------
// Helpers to create test UTXOs
// ---------------------------------------------------------------------------

namespace {

// A fixed token category for testing.
hash_digest const token_cat_a = {{
    0xAA, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01}};

hash_digest const token_cat_b = {{
    0xBB, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x02}};

// Create a BCH-only UTXO (no tokens).
utxo make_bch_utxo(uint64_t amount) {
    return utxo{output_point{}, amount, std::nullopt};
}

// Create a UTXO carrying a fungible token with dust BCH.
utxo make_token_utxo(hash_digest const& category, uint64_t token_amount, uint64_t bch_dust = 800) {
    token_data_t token{category, fungible{amount_t(token_amount)}};
    return utxo{output_point{}, bch_dust, token};
}

// Create a UTXO carrying a non-fungible token (NFT).
utxo make_nft_utxo(hash_digest const& category, uint64_t bch_dust = 800) {
    data_chunk commitment{0x01, 0x02, 0x03};
    token_data_t token{category, non_fungible{capability_t::none, commitment}};
    return utxo{output_point{}, bch_dust, token};
}

// Create a UTXO carrying both a fungible token and an NFT (both_kinds).
utxo make_both_kinds_utxo(hash_digest const& category, uint64_t token_amount, uint64_t bch_dust = 800) {
    data_chunk commitment{0x01, 0x02, 0x03};
    token_data_t token{category, both_kinds{
        fungible{amount_t(token_amount)},
        non_fungible{capability_t::none, commitment}
    }};
    return utxo{output_point{}, bch_dust, token};
}

// Approximate output size for 2 outputs (destination + change).
constexpr size_t two_outputs_size = 2 * approx_output_size;  // 68

} // anonymous namespace

// ===========================================================================
// select_utxos — BCH selection
// ===========================================================================

TEST_CASE("select_utxos skips token utxos in clean bch mode", "[coin_selection]") {
    // The token UTXO has 10000 sats — more than enough to cover the request.
    // In clean mode it must be skipped, forcing selection of the two smaller
    // BCH-only UTXOs instead. In mixed mode it would be selected first.
    std::vector<utxo> utxos = {
        make_bch_utxo(5000),
        make_token_utxo(token_cat_a, 100, 10000),  // skipped in clean despite having most BCH
        make_bch_utxo(3000),
    };

    auto result = select_utxos(utxos, 7000, two_outputs_size, bch_id, coin_selection_strategy::clean);
    REQUIRE(result.has_value());
    REQUIRE(result->total_selected_bch == 8000);  // 5000 + 3000 (not the 10000 token UTXO)
    REQUIRE(result->selected_indices.size() == 2);
    REQUIRE(result->selected_indices[0] == 0);
    REQUIRE(result->selected_indices[1] == 2);
    REQUIRE(result->collateral_fts.empty());
    REQUIRE(result->collateral_nfts.empty());
}

TEST_CASE("select_utxos fails when no bch-only utxos available in clean mode", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_token_utxo(token_cat_a, 100, 10000),
        make_token_utxo(token_cat_b, 200, 20000),
    };

    auto result = select_utxos(utxos, 5000, two_outputs_size, bch_id, coin_selection_strategy::clean);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::insufficient_amount);
}

TEST_CASE("select_utxos mixed mode reports fungible collateral tokens", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_bch_utxo(1000),
        make_token_utxo(token_cat_a, 100, 5000),
        make_bch_utxo(2000),
    };

    auto result = select_utxos(utxos, 7000, two_outputs_size, bch_id, coin_selection_strategy::mixed);
    REQUIRE(result.has_value());
    REQUIRE(result->total_selected_bch == 8000);  // 1000 + 5000 + 2000
    REQUIRE(result->selected_indices.size() == 3);
    REQUIRE(result->selected_indices[0] == 0);
    REQUIRE(result->selected_indices[1] == 1);
    REQUIRE(result->selected_indices[2] == 2);
    // Token A should appear as collateral
    REQUIRE(result->collateral_fts.count(token_cat_a) == 1);
    REQUIRE(result->collateral_fts.at(token_cat_a) == 100);
}

TEST_CASE("select_utxos mixed mode reports nft collateral tokens", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_nft_utxo(token_cat_a, 5000),
        make_bch_utxo(3000),
    };

    auto result = select_utxos(utxos, 7000, two_outputs_size, bch_id, coin_selection_strategy::mixed);
    REQUIRE(result.has_value());
    REQUIRE(result->collateral_nfts.size() == 1);
    REQUIRE(result->collateral_nfts[0].id == token_cat_a);
}

TEST_CASE("select_utxos returns error when bch is insufficient", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_bch_utxo(100),
        make_bch_utxo(200),
    };

    auto result = select_utxos(utxos, 50000, two_outputs_size, bch_id);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::insufficient_amount);
}

TEST_CASE("select_utxos fee estimation grows with number of inputs", "[coin_selection]") {
    // Many small UTXOs: fee grows with each input added
    std::vector<utxo> utxos;
    for (int i = 0; i < 10; ++i) {
        utxos.push_back(make_bch_utxo(500));
    }

    // 5000 total BCH, but fees eat into it
    auto result = select_utxos(utxos, 1000, two_outputs_size, bch_id);
    REQUIRE(result.has_value());
    // Verify fee was accounted for
    auto const fee = result->estimated_size * sats_per_byte;
    REQUIRE(result->total_selected_bch >= 1000 + fee);
}

TEST_CASE("select_utxos selects single utxo when it covers amount plus fee", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_bch_utxo(100000),
    };

    auto result = select_utxos(utxos, 50000, two_outputs_size, bch_id);
    REQUIRE(result.has_value());
    REQUIRE(result->selected_indices.size() == 1);
    REQUIRE(result->selected_indices[0] == 0);
    REQUIRE(result->total_selected_bch == 100000);
}

// ===========================================================================
// select_utxos — Token selection (delegates to select_utxos_both)
// ===========================================================================

TEST_CASE("select_utxos delegates token selection to select_utxos_both", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_bch_utxo(5000),
        make_token_utxo(token_cat_a, 500, 800),
        make_token_utxo(token_cat_a, 300, 800),
        make_bch_utxo(3000),
    };

    // Select 700 tokens of category A
    auto result = select_utxos(utxos, 700, two_outputs_size, token_cat_a);
    REQUIRE(result.has_value());
    REQUIRE(result->total_selected_token == 800);  // 500 + 300 (both needed to reach 700)
}

// ===========================================================================
// select_utxos_send_all — BCH
// ===========================================================================

TEST_CASE("select_utxos_send_all bch skips token utxos", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_bch_utxo(10000),
        make_token_utxo(token_cat_a, 100, 800),
        make_bch_utxo(5000),
    };

    auto result = select_utxos_send_all(utxos, two_outputs_size, bch_id);
    REQUIRE(result.has_value());
    REQUIRE(result->total_selected_bch == 15000);  // 10000 + 5000
    REQUIRE(result->selected_indices.size() == 2);
    // Token UTXO at index 1 should be skipped
    REQUIRE(result->selected_indices[0] == 0);
    REQUIRE(result->selected_indices[1] == 2);
}

TEST_CASE("select_utxos_send_all bch returns error on empty wallet", "[coin_selection]") {
    std::vector<utxo> utxos;
    auto result = select_utxos_send_all(utxos, two_outputs_size, bch_id);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::insufficient_amount);
}

TEST_CASE("select_utxos_send_all bch fails when balance cannot cover fee", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_bch_utxo(1),  // less than any possible fee
    };

    auto result = select_utxos_send_all(utxos, two_outputs_size, bch_id);
    // With 1 sat and fee > 1 sat, should fail
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::insufficient_amount);
}

// ===========================================================================
// select_utxos_send_all — Token
// ===========================================================================

TEST_CASE("select_utxos_send_all token collects all matching utxos and adds bch for fees", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_bch_utxo(5000),
        make_token_utxo(token_cat_a, 100, 800),
        make_token_utxo(token_cat_a, 250, 800),
        make_token_utxo(token_cat_b, 999, 800),  // different category, skipped
        make_bch_utxo(3000),
    };

    auto result = select_utxos_send_all(utxos, two_outputs_size, token_cat_a);
    REQUIRE(result.has_value());
    REQUIRE(result->total_selected_token == 350);  // 100 + 250
    // Should have selected token UTXOs + at least one BCH UTXO for fees
    REQUIRE(result->total_selected_bch >= token_dust_limit);  // at least dust
}

TEST_CASE("select_utxos_send_all token fails when no utxos match category", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_bch_utxo(5000),
        make_token_utxo(token_cat_b, 100, 800),
    };

    auto result = select_utxos_send_all(utxos, two_outputs_size, token_cat_a);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::insufficient_amount);
}

// ===========================================================================
// select_utxos_both — Clean mode (two-phase)
// ===========================================================================

TEST_CASE("select_utxos_both clean mode selects tokens then bch separately", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_bch_utxo(10000),                      // BCH-only
        make_token_utxo(token_cat_a, 500, 800),    // target token
        make_token_utxo(token_cat_a, 300, 800),    // target token
        make_token_utxo(token_cat_b, 999, 5000),   // different category
        make_bch_utxo(2000),                        // BCH-only
    };

    auto result = select_utxos_both(utxos, 5000, token_cat_a, 700,
                                     two_outputs_size, coin_selection_strategy::clean);
    REQUIRE(result.has_value());
    REQUIRE(result->total_selected_token == 800);  // 500 + 300
    REQUIRE(result->total_selected_bch == 11600);  // 800 + 800 (token dust) + 10000 (BCH UTXO)
    // Should NOT have selected token_cat_b UTXO (index 3)
    for (auto idx : result->selected_indices) {
        REQUIRE(idx != 3);
    }
    REQUIRE(result->collateral_fts.empty());
    REQUIRE(result->collateral_nfts.empty());
}

TEST_CASE("select_utxos_both clean mode fails when token supply is insufficient", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_bch_utxo(100000),
        make_token_utxo(token_cat_a, 100, 800),
    };

    auto result = select_utxos_both(utxos, 5000, token_cat_a, 500,
                                     two_outputs_size, coin_selection_strategy::clean);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::insufficient_amount);  // only 100 tokens available, need 500
}

TEST_CASE("select_utxos_both clean mode fails when bch is insufficient for fees", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_token_utxo(token_cat_a, 500, 800),
        make_token_utxo(token_cat_a, 300, 800),
        // No BCH-only UTXOs
    };

    auto result = select_utxos_both(utxos, 5000, token_cat_a, 700,
                                     two_outputs_size, coin_selection_strategy::clean);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::insufficient_amount);  // not enough BCH (only dust from tokens)
}

TEST_CASE("select_utxos_both rejects bch_id as token category", "[coin_selection]") {
    std::vector<utxo> utxos = { make_bch_utxo(10000) };

    auto result = select_utxos_both(utxos, 5000, bch_id, 100,
                                     two_outputs_size, coin_selection_strategy::clean);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::operation_failed);  // bch_id not allowed
}

// ===========================================================================
// select_utxos_both — Mixed mode (single-pass)
// ===========================================================================

TEST_CASE("select_utxos_both mixed mode may pick up collateral tokens", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_token_utxo(token_cat_a, 500, 800),    // target token
        make_token_utxo(token_cat_b, 200, 5000),   // other token (collateral)
        make_bch_utxo(10000),                       // BCH-only
        make_token_utxo(token_cat_a, 300, 800),    // target token
    };

    auto result = select_utxos_both(utxos, 5000, token_cat_a, 700,
                                     two_outputs_size, coin_selection_strategy::mixed);
    REQUIRE(result.has_value());
    REQUIRE(result->total_selected_token == 800);  // 500 + 300
    REQUIRE(result->total_selected_bch == 11600);  // 800 + 800 (token dust) + 10000 (BCH UTXO)
    // Mixed sort rank: target fungible (500,300) → BCH-only (10000) → other fungible (cat_b)
    // cat_b not reached because BCH+token needs are met after the first 3 UTXOs
}

TEST_CASE("select_utxos_both mixed mode fails when both assets are insufficient", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_bch_utxo(100),
        make_token_utxo(token_cat_a, 10, 100),
    };

    auto result = select_utxos_both(utxos, 50000, token_cat_a, 1000,
                                     two_outputs_size, coin_selection_strategy::mixed);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::insufficient_amount);
}

// ===========================================================================
// Edge cases
// ===========================================================================

TEST_CASE("all selection functions return error on empty utxo list", "[coin_selection]") {
    std::vector<utxo> utxos;

    auto r1 = select_utxos(utxos, 1000, two_outputs_size, bch_id);
    REQUIRE_FALSE(r1.has_value());
    REQUIRE(r1.error() == error::insufficient_amount);

    auto r2 = select_utxos_send_all(utxos, two_outputs_size, bch_id);
    REQUIRE_FALSE(r2.has_value());
    REQUIRE(r2.error() == error::insufficient_amount);

    auto r3 = select_utxos_both(utxos, 1000, token_cat_a, 100, two_outputs_size);
    REQUIRE_FALSE(r3.has_value());
    REQUIRE(r3.error() == error::insufficient_amount);
}

TEST_CASE("select_utxos succeeds when utxo exactly covers amount plus fee", "[coin_selection]") {
    // Create a UTXO that exactly covers amount + fee for 1 input + 2 outputs
    auto const size_1in = base_tx_size + approx_input_size + two_outputs_size;
    auto const fee_1in = size_1in * sats_per_byte;
    auto const amount = 1000ULL;
    auto const exact = amount + fee_1in;

    std::vector<utxo> utxos = {
        make_bch_utxo(exact),
    };

    auto result = select_utxos(utxos, amount, two_outputs_size, bch_id);
    REQUIRE(result.has_value());
    REQUIRE(result->selected_indices.size() == 1);
    REQUIRE(result->selected_indices[0] == 0);
    REQUIRE(result->total_selected_bch == exact);
}

TEST_CASE("select_utxos clean mode never selects token-carrying utxos for bch", "[coin_selection]") {
    // This test verifies the fix for the bug where select_utxos_simple
    // would silently consume token-carrying UTXOs and burn the tokens.
    std::vector<utxo> utxos = {
        make_token_utxo(token_cat_a, 1000, 50000),  // 50k sats with tokens
        make_bch_utxo(1000),                          // 1k sats BCH-only
    };

    // In clean mode, the 50k UTXO with tokens should NOT be selected
    auto result = select_utxos(utxos, 500, two_outputs_size, bch_id, coin_selection_strategy::clean);
    REQUIRE(result.has_value());
    // Only the BCH-only UTXO should be selected
    REQUIRE(result->selected_indices.size() == 1);
    REQUIRE(result->selected_indices[0] == 1);
    REQUIRE(result->total_selected_bch == 1000);
}

// ===========================================================================
// create_token_split_tx_template
// ===========================================================================

namespace {

// Create a UTXO with a specific outpoint hash for identification in tests.
utxo make_utxo_with_point(hash_digest const& txhash, uint32_t index,
                           uint64_t bch_amount, std::optional<token_data_t> token = std::nullopt) {
    output_point point{txhash, index};
    return utxo{point, bch_amount, std::move(token)};
}

hash_digest const tx_hash_1 = {{
    0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

hash_digest const tx_hash_2 = {{
    0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

hash_digest const tx_hash_3 = {{
    0x03, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

// A dummy payment address for test outputs.
// Using a function with local static to avoid static initialization order issues
// (payment_address constructor parses cashaddr, which may depend on other statics).
payment_address const& test_address() {
    static payment_address const addr{"bitcoincash:qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq5dc09yc9"};
    return addr;
}

} // anonymous namespace

TEST_CASE("create_token_split_tx_template separates token from excess bch", "[coin_selection]") {
    // A token UTXO with 50000 sats (way more than dust)
    token_data_t token{token_cat_a, fungible{amount_t(1000)}};
    auto dirty_utxo = make_utxo_with_point(tx_hash_1, 0, 50000, token);

    std::vector<utxo> available = { dirty_utxo };
    output_point::list outpoints = { output_point{tx_hash_1, 0} };

    auto result = create_token_split_tx_template(outpoints, available, test_address());
    REQUIRE(result.has_value());

    auto const& [tx, addresses, amounts] = *result;

    // Should have 1 input
    REQUIRE(tx.inputs().size() == 1);
    // Should have 2 outputs: token + BCH
    REQUIRE(tx.outputs().size() == 2);
    // Token output should have dust BCH
    REQUIRE(amounts[0] == token_dust_limit);
    // BCH output should have the rest minus fee
    REQUIRE(amounts[1] > 0);
    REQUIRE(amounts[0] + amounts[1] < 50000);  // fee was deducted
    // All addresses should be destination
    REQUIRE(addresses.size() == 2);
}

TEST_CASE("create_token_split_tx_template creates one output per token category", "[coin_selection]") {
    token_data_t token_a{token_cat_a, fungible{amount_t(500)}};
    token_data_t token_b{token_cat_b, fungible{amount_t(200)}};

    auto utxo1 = make_utxo_with_point(tx_hash_1, 0, 30000, token_a);
    auto utxo2 = make_utxo_with_point(tx_hash_2, 0, 20000, token_b);

    std::vector<utxo> available = { utxo1, utxo2 };
    output_point::list outpoints = {
        output_point{tx_hash_1, 0},
        output_point{tx_hash_2, 0}
    };

    auto result = create_token_split_tx_template(outpoints, available, test_address());
    REQUIRE(result.has_value());

    auto const& [tx, addresses, amounts] = *result;

    // 2 inputs, 3 outputs (token_a + token_b + BCH remainder)
    REQUIRE(tx.inputs().size() == 2);
    REQUIRE(tx.outputs().size() == 3);
    // Two token outputs at dust, one BCH output
    size_t dust_count = 0;
    for (auto a : amounts) {
        if (a == token_dust_limit) ++dust_count;
    }
    REQUIRE(dust_count == 2);
}

TEST_CASE("create_token_split_tx_template consolidates same-category tokens", "[coin_selection]") {
    token_data_t token1{token_cat_a, fungible{amount_t(300)}};
    token_data_t token2{token_cat_a, fungible{amount_t(700)}};

    auto utxo1 = make_utxo_with_point(tx_hash_1, 0, 10000, token1);
    auto utxo2 = make_utxo_with_point(tx_hash_2, 0, 10000, token2);

    std::vector<utxo> available = { utxo1, utxo2 };
    output_point::list outpoints = {
        output_point{tx_hash_1, 0},
        output_point{tx_hash_2, 0}
    };

    auto result = create_token_split_tx_template(outpoints, available, test_address());
    REQUIRE(result.has_value());

    auto const& [tx, addresses, amounts] = *result;

    // 2 inputs, 2 outputs (one consolidated token_a + BCH remainder)
    REQUIRE(tx.inputs().size() == 2);
    REQUIRE(tx.outputs().size() == 2);

    // Token output: 1000 tokens (300 + 700) with dust BCH
    bool found_token = false;
    for (auto const& out : tx.outputs()) {
        if (out.token_data().has_value()) {
            auto const& td = out.token_data().value();
            REQUIRE(td.id == token_cat_a);
            REQUIRE(std::get<fungible>(td.data).amount == amount_t(1000));
            found_token = true;
        }
    }
    REQUIRE(found_token);
}

TEST_CASE("create_token_split_tx_template fails on empty outpoints", "[coin_selection]") {
    std::vector<utxo> available = { make_bch_utxo(10000) };
    output_point::list outpoints;

    auto result = create_token_split_tx_template(outpoints, available, test_address());
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::empty_utxo_list);
}

TEST_CASE("create_token_split_tx_template fails when outpoint is not found", "[coin_selection]") {
    auto utxo1 = make_utxo_with_point(tx_hash_1, 0, 10000, std::nullopt);
    std::vector<utxo> available = { utxo1 };

    // Request an outpoint that doesn't exist
    output_point::list outpoints = { output_point{tx_hash_2, 0} };

    auto result = create_token_split_tx_template(outpoints, available, test_address());
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::utxo_not_found);
}

TEST_CASE("create_token_split_tx_template rejects nft utxos", "[coin_selection]") {
    data_chunk commitment{0x01, 0x02};
    token_data_t nft{token_cat_a, non_fungible{capability_t::none, commitment}};
    auto utxo1 = make_utxo_with_point(tx_hash_1, 0, 10000, nft);

    std::vector<utxo> available = { utxo1 };
    output_point::list outpoints = { output_point{tx_hash_1, 0} };

    auto result = create_token_split_tx_template(outpoints, available, test_address());
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::operation_failed);  // NFTs not supported
}

TEST_CASE("create_token_split_tx_template fails when bch cannot cover dust plus fee", "[coin_selection]") {
    // Token UTXO with barely any BCH — not enough for dust + fee
    token_data_t token{token_cat_a, fungible{amount_t(100)}};
    auto utxo1 = make_utxo_with_point(tx_hash_1, 0, 100, token);  // only 100 sats

    std::vector<utxo> available = { utxo1 };
    output_point::list outpoints = { output_point{tx_hash_1, 0} };

    auto result = create_token_split_tx_template(outpoints, available, test_address());
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::insufficient_fee);  // 100 sats < 800 dust + fee
}

TEST_CASE("create_token_split_tx_template handles bch-only utxo with single output", "[coin_selection]") {
    // A "dirty" UTXO that has no tokens — just BCH
    auto utxo1 = make_utxo_with_point(tx_hash_1, 0, 50000, std::nullopt);

    std::vector<utxo> available = { utxo1 };
    output_point::list outpoints = { output_point{tx_hash_1, 0} };

    auto result = create_token_split_tx_template(outpoints, available, test_address());
    REQUIRE(result.has_value());

    auto const& [tx, addresses, amounts] = *result;

    // No token categories, so just 1 BCH output
    REQUIRE(tx.outputs().size() == 1);
    REQUIRE(amounts[0] > 0);
    REQUIRE(amounts[0] < 50000);  // fee deducted
}

TEST_CASE("create_token_split_tx_template does not overcharge fee when bch output is not created", "[coin_selection]") {
    // Token UTXO with just enough BCH for dust + fee (without BCH change output).
    // base_size = base(10) + 1*input(149) + 1*token_output(73) = 232
    // fee_without_bch_output = 232
    // bch needed = 800 (dust) + 232 (fee) = 1032
    // With the old code, fee was 232 + 34 = 266, needing 1066 — this would fail.
    token_data_t token{token_cat_a, fungible{amount_t(500)}};
    auto utxo1 = make_utxo_with_point(tx_hash_1, 0, 1032, token);

    std::vector<utxo> available = { utxo1 };
    output_point::list outpoints = { output_point{tx_hash_1, 0} };

    auto result = create_token_split_tx_template(outpoints, available, test_address());
    REQUIRE(result.has_value());
    auto const& [tx, addresses, amounts] = *result;
    // Only the token output, no BCH change (all BCH goes to dust + fee)
    REQUIRE(tx.outputs().size() == 1);
    REQUIRE(amounts[0] == token_dust_limit);
}

TEST_CASE("create_token_split_tx_template fails when bch-only input exactly covers fee", "[coin_selection]") {
    // BCH-only UTXO where total_bch == fee (no token outputs, no BCH change).
    // base_size = base(10) + 1*input(149) = 159 (no outputs at all)
    // fee = 159, bch_remaining = 0 → empty outputs → error
    auto utxo1 = make_utxo_with_point(tx_hash_1, 0, 159, std::nullopt);

    std::vector<utxo> available = { utxo1 };
    output_point::list outpoints = { output_point{tx_hash_1, 0} };

    auto result = create_token_split_tx_template(outpoints, available, test_address());
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::operation_failed);
}

TEST_CASE("create_token_split_tx_template absorbs sub-dust bch remainder as fee", "[coin_selection]") {
    // Token UTXO with enough for dust + fee + a small BCH remainder below bch_dust_limit.
    // base_size with BCH output = base(10) + 1*input(149) + 1*token(73) + 1*p2pkh(34) = 266
    // fee_with_bch_output = 266
    // bch_remaining = 1200 - 800 - 266 = 134 (below bch_dust_limit of 546)
    // So the BCH output should NOT be created — 134 sats absorbed as extra fee.
    token_data_t token{token_cat_a, fungible{amount_t(500)}};
    auto utxo1 = make_utxo_with_point(tx_hash_1, 0, 1200, token);

    std::vector<utxo> available = { utxo1 };
    output_point::list outpoints = { output_point{tx_hash_1, 0} };

    auto result = create_token_split_tx_template(outpoints, available, test_address());
    REQUIRE(result.has_value());
    auto const& [tx, addresses, amounts] = *result;
    // Only the token output, sub-dust remainder absorbed as fee
    REQUIRE(tx.outputs().size() == 1);
    REQUIRE(amounts[0] == token_dust_limit);
}

TEST_CASE("create_token_split_tx_template creates bch output when remainder is above dust", "[coin_selection]") {
    // Token UTXO with plenty of BCH — remainder well above bch_dust_limit.
    token_data_t token{token_cat_a, fungible{amount_t(500)}};
    auto utxo1 = make_utxo_with_point(tx_hash_1, 0, 5000, token);

    std::vector<utxo> available = { utxo1 };
    output_point::list outpoints = { output_point{tx_hash_1, 0} };

    auto result = create_token_split_tx_template(outpoints, available, test_address());
    REQUIRE(result.has_value());
    auto const& [tx, addresses, amounts] = *result;
    // Token output + BCH output
    REQUIRE(tx.outputs().size() == 2);
    REQUIRE(amounts[0] == token_dust_limit);
    REQUIRE(amounts[1] >= bch_dust_limit);
}

// ===========================================================================
// select_utxos_both — token dust BCH reservation
// ===========================================================================

TEST_CASE("select_utxos_both clean mode fails when token utxo bch only covers dust but not fee", "[coin_selection]") {
    // One token UTXO with exactly dust BCH (800 sats), no BCH-only UTXOs.
    // fee for 1 input + 2 outputs = (10 + 148 + 68) * 1 = 226
    // total BCH = 800, needs: token_dust(800) + fee(226) = 1026 → insufficient
    std::vector<utxo> utxos = {
        make_token_utxo(token_cat_a, 1000, 800),
    };

    auto result = select_utxos_both(utxos, 0, token_cat_a, 500,
                                     two_outputs_size, coin_selection_strategy::clean);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::insufficient_amount);
}

TEST_CASE("select_utxos for token fails when token utxo bch only covers dust but not fee", "[coin_selection]") {
    // Same scenario via select_utxos (delegates to select_utxos_both with bch_amount = 0).
    std::vector<utxo> utxos = {
        make_token_utxo(token_cat_a, 1000, 800),
    };

    auto result = select_utxos(utxos, 500, two_outputs_size, token_cat_a);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::insufficient_amount);
}

TEST_CASE("select_utxos_both clean mode succeeds when bch covers dust plus fee", "[coin_selection]") {
    // Token UTXO with dust + BCH-only UTXO that covers fees.
    // fee for 2 inputs + 2 outputs = (10 + 2*148 + 68) * 1 = 374
    // total BCH = 800 + 5000 = 5800, needs: token_dust(800) + fee(374) = 1174 → ok
    std::vector<utxo> utxos = {
        make_token_utxo(token_cat_a, 1000, 800),
        make_bch_utxo(5000),
    };

    auto result = select_utxos_both(utxos, 0, token_cat_a, 500,
                                     two_outputs_size, coin_selection_strategy::clean);
    REQUIRE(result.has_value());
    auto const fee = result->estimated_size * sats_per_byte;
    REQUIRE(result->total_selected_bch >= fee + token_dust_limit);
}

// ===========================================================================
// Multiple token UTXOs / categories
// ===========================================================================

TEST_CASE("select_utxos_both clean mode selects multiple token utxos to reach target amount", "[coin_selection]") {
    // Three token UTXOs of category A (200+300+400=900 tokens, 800 sats each = 2400 BCH).
    // Plus a BCH-only UTXO for fees.
    // Request 800 tokens → needs at least 3 token UTXOs (200+300+400=900 >= 800).
    std::vector<utxo> utxos = {
        make_token_utxo(token_cat_a, 200, 800),
        make_token_utxo(token_cat_a, 300, 800),
        make_token_utxo(token_cat_a, 400, 800),
        make_bch_utxo(5000),
    };

    auto result = select_utxos_both(utxos, 0, token_cat_a, 800,
                                     two_outputs_size, coin_selection_strategy::clean);
    REQUIRE(result.has_value());
    REQUIRE(result->total_selected_token == 900);  // 200 + 300 + 400 (all 3 needed)
    auto const fee = result->estimated_size * sats_per_byte;
    REQUIRE(result->total_selected_bch >= fee + token_dust_limit);
}

TEST_CASE("select_utxos_send_all token collects multiple utxos of same category", "[coin_selection]") {
    // Four token UTXOs of category A, scattered among other UTXOs.
    std::vector<utxo> utxos = {
        make_bch_utxo(3000),
        make_token_utxo(token_cat_a, 100, 800),
        make_token_utxo(token_cat_b, 500, 800),  // different category, skipped
        make_token_utxo(token_cat_a, 200, 800),
        make_bch_utxo(2000),
        make_token_utxo(token_cat_a, 300, 800),
        make_token_utxo(token_cat_a, 150, 800),
    };

    auto result = select_utxos_send_all(utxos, two_outputs_size, token_cat_a);
    REQUIRE(result.has_value());
    REQUIRE(result->total_selected_token == 750);  // 100 + 200 + 300 + 150
}

TEST_CASE("select_utxos_both clean mode ignores other token categories", "[coin_selection]") {
    // UTXOs with two different token categories. Selecting category A should
    // only pick up A tokens, not B.
    std::vector<utxo> utxos = {
        make_token_utxo(token_cat_a, 500, 800),
        make_token_utxo(token_cat_b, 999, 800),
        make_token_utxo(token_cat_a, 400, 800),
        make_token_utxo(token_cat_b, 888, 800),
        make_bch_utxo(10000),
    };

    auto result = select_utxos_both(utxos, 0, token_cat_a, 700,
                                     two_outputs_size, coin_selection_strategy::clean);
    REQUIRE(result.has_value());
    REQUIRE(result->total_selected_token == 900);  // 500 + 400
    // Should NOT have selected category B UTXOs (indices 1, 3)
    for (auto idx : result->selected_indices) {
        REQUIRE(idx != 1);
        REQUIRE(idx != 3);
    }
    REQUIRE(result->collateral_fts.empty());
}

// ===========================================================================
// Edge cases — fee and balance boundaries
// ===========================================================================

TEST_CASE("select_utxos fails when balance equals amount with no room for fee", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_bch_utxo(1000),
    };

    auto result = select_utxos(utxos, 1000, two_outputs_size, bch_id);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::insufficient_amount);
}

TEST_CASE("select_utxos fails when many small utxos cannot cover growing fee", "[coin_selection]") {
    // 50 UTXOs of 100 sats each = 5000 total.
    // But 50 inputs × 148 bytes = 7400 + base(10) + outputs(68) = 7478 bytes fee.
    // Need: 2000 + 7478 = 9478, but only have 5000.
    std::vector<utxo> utxos;
    for (int i = 0; i < 50; ++i) {
        utxos.push_back(make_bch_utxo(100));
    }

    auto result = select_utxos(utxos, 2000, two_outputs_size, bch_id);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::insufficient_amount);
}

// ===========================================================================
// Edge cases — token dust boundaries
// ===========================================================================

TEST_CASE("select_utxos_both fails when token utxo has sub-dust bch", "[coin_selection]") {
    // Token UTXO with 799 sats (below token_dust_limit of 800).
    // Even with a BCH UTXO for fees, the token output needs 800 sats dust
    // and we only have 799 from the token UTXO. The BCH UTXO goes to fees,
    // but the 799 sats from the token UTXO aren't enough for the token output dust.
    // total BCH = 799 + 5000 = 5799
    // needs: token_dust(800) + fee ≈ 1174 → 5799 >= 1174 so it succeeds.
    // Actually 799 < 800 is not a problem for selection — the selection just
    // counts total BCH. The dust is the OUTPUT amount, not the input amount.
    // So this should succeed — the 799 from the token UTXO plus BCH from the
    // other UTXO provides enough total BCH.
    std::vector<utxo> utxos = {
        make_token_utxo(token_cat_a, 1000, 799),
        make_bch_utxo(5000),
    };

    auto result = select_utxos_both(utxos, 0, token_cat_a, 500,
                                     two_outputs_size, coin_selection_strategy::clean);
    REQUIRE(result.has_value());
    REQUIRE(result->total_selected_token == 1000);
    auto const fee = result->estimated_size * sats_per_byte;
    REQUIRE(result->total_selected_bch >= fee + token_dust_limit);
}

TEST_CASE("select_utxos_both fails when sub-dust token utxo is only bch source", "[coin_selection]") {
    // Token UTXO with 799 sats, no BCH-only UTXOs.
    // total BCH = 799, needs: token_dust(800) + fee ≈ 1026 → fails.
    std::vector<utxo> utxos = {
        make_token_utxo(token_cat_a, 1000, 799),
    };

    auto result = select_utxos_both(utxos, 0, token_cat_a, 500,
                                     two_outputs_size, coin_selection_strategy::clean);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::insufficient_amount);
}

// ===========================================================================
// Edge cases — mixed mode collateral with multiple categories
// ===========================================================================

TEST_CASE("select_utxos mixed mode reports collateral from multiple token categories", "[coin_selection]") {
    // Request BCH in mixed mode. The wallet has BCH spread across token UTXOs
    // of different categories. All get selected as collateral.
    std::vector<utxo> utxos = {
        make_bch_utxo(1000),
        make_token_utxo(token_cat_a, 100, 3000),
        make_token_utxo(token_cat_b, 200, 4000),
        make_nft_utxo(token_cat_a, 2000),
    };

    auto result = select_utxos(utxos, 9000, two_outputs_size, bch_id, coin_selection_strategy::mixed);
    REQUIRE(result.has_value());
    REQUIRE(result->total_selected_bch == 10000);  // 1000 + 3000 + 4000 + 2000
    REQUIRE(result->selected_indices.size() == 4);
    REQUIRE(result->selected_indices[0] == 0);
    REQUIRE(result->selected_indices[1] == 1);
    REQUIRE(result->selected_indices[2] == 2);
    REQUIRE(result->selected_indices[3] == 3);
    // Fungible collateral: cat_a(100) and cat_b(200)
    REQUIRE(result->collateral_fts.size() == 2);
    REQUIRE(result->collateral_fts.at(token_cat_a) == 100);
    REQUIRE(result->collateral_fts.at(token_cat_b) == 200);
    // NFT collateral: cat_a NFT
    REQUIRE(result->collateral_nfts.size() == 1);
    REQUIRE(result->collateral_nfts[0].id == token_cat_a);
}

// ===========================================================================
// Edge cases — send_all with heterogeneous UTXO set
// ===========================================================================

TEST_CASE("select_utxos_send_all bch only selects bch-only utxos from mixed set", "[coin_selection]") {
    // Wallet has BCH, fungible tokens, NFTs — send_all BCH should only take pure BCH.
    std::vector<utxo> utxos = {
        make_bch_utxo(10000),
        make_token_utxo(token_cat_a, 500, 800),
        make_bch_utxo(5000),
        make_nft_utxo(token_cat_b, 800),
        make_token_utxo(token_cat_b, 300, 800),
        make_bch_utxo(3000),
        make_token_utxo(token_cat_a, 100, 5000),
    };

    auto result = select_utxos_send_all(utxos, two_outputs_size, bch_id);
    REQUIRE(result.has_value());
    REQUIRE(result->total_selected_bch == 18000);  // 10000 + 5000 + 3000
    REQUIRE(result->selected_indices.size() == 3);
    REQUIRE(result->selected_indices[0] == 0);
    REQUIRE(result->selected_indices[1] == 2);
    REQUIRE(result->selected_indices[2] == 5);
}

// ===========================================================================
// both_kinds UTXOs (fungible + NFT in same UTXO)
// ===========================================================================

TEST_CASE("select_utxos_both includes both_kinds utxo fungible amount in token selection", "[coin_selection]") {
    // A both_kinds UTXO carries 500 fungible tokens + an NFT.
    // Requesting 400 tokens should select this UTXO and count the 500 fungible tokens.
    std::vector<utxo> utxos = {
        make_both_kinds_utxo(token_cat_a, 500, 800),
        make_bch_utxo(5000),
    };

    auto result = select_utxos_both(utxos, 0, token_cat_a, 400,
                                     two_outputs_size, coin_selection_strategy::clean);
    REQUIRE(result.has_value());
    REQUIRE(result->total_selected_token == 500);
}

TEST_CASE("select_utxos_send_all token includes both_kinds utxos", "[coin_selection]") {
    // send_all for token_cat_a should collect fungible amounts from both
    // pure fungible and both_kinds UTXOs.
    std::vector<utxo> utxos = {
        make_token_utxo(token_cat_a, 300, 800),
        make_both_kinds_utxo(token_cat_a, 200, 800),
        make_bch_utxo(5000),
    };

    auto result = select_utxos_send_all(utxos, two_outputs_size, token_cat_a);
    REQUIRE(result.has_value());
    REQUIRE(result->total_selected_token == 500);  // 300 + 200
    // The NFT from the both_kinds UTXO must be reported as collateral
    REQUIRE(result->collateral_nfts.size() == 1);
    REQUIRE(result->collateral_nfts[0].id == token_cat_a);
    REQUIRE(std::holds_alternative<non_fungible>(result->collateral_nfts[0].data));
}

TEST_CASE("track_collateral records fungible portion of both_kinds utxo", "[coin_selection]") {
    // In mixed BCH selection, a both_kinds UTXO of a different category
    // gets selected for its BCH. The collateral should report BOTH the
    // fungible amount and the NFT.
    std::vector<utxo> utxos = {
        make_bch_utxo(1000),
        make_both_kinds_utxo(token_cat_a, 100, 5000),
    };

    auto result = select_utxos(utxos, 5000, two_outputs_size, bch_id, coin_selection_strategy::mixed);
    REQUIRE(result.has_value());
    REQUIRE(result->total_selected_bch == 6000);  // 1000 + 5000
    // Fungible portion of both_kinds should appear in collateral_fts
    REQUIRE(result->collateral_fts.count(token_cat_a) == 1);
    REQUIRE(result->collateral_fts.at(token_cat_a) == 100);
    // NFT portion should appear in collateral_nfts as non_fungible (not both_kinds)
    REQUIRE(result->collateral_nfts.size() == 1);
    REQUIRE(result->collateral_nfts[0].id == token_cat_a);
    REQUIRE(std::holds_alternative<non_fungible>(result->collateral_nfts[0].data));
}

TEST_CASE("select_utxos_both mixed mode counts both_kinds fungible for target category", "[coin_selection]") {
    // Mixed mode: a both_kinds UTXO of the target category should have its
    // fungible amount counted toward total_selected_token.
    std::vector<utxo> utxos = {
        make_both_kinds_utxo(token_cat_a, 600, 800),
        make_bch_utxo(10000),
    };

    auto result = select_utxos_both(utxos, 0, token_cat_a, 500,
                                     two_outputs_size, coin_selection_strategy::mixed);
    REQUIRE(result.has_value());
    REQUIRE(result->total_selected_token == 600);
}

// ===========================================================================
// create_tx_template — change distribution
// ===========================================================================

TEST_CASE("create_tx_template fails with multiple change addresses due to truncation", "[coin_selection]") {
    // 3 change addresses with equal ratios (1/3 each).
    // total_change that is NOT divisible by 3 triggers truncation:
    // uint64_t(1000 * 0.333...) = 333, accumulated = 333*3 = 999 != 1000.
    //
    // We need to engineer a specific total_change value.
    // With 1 input of 12000, sending 10000:
    // fee = (10 + 148 + 4*34) * 1 = 294 (base + 1 input + 4 outputs: dest + 3 change)
    // total_change = 12000 - 10000 - 294 = 1706
    // uint64_t(1706 * 0.333...) = 568, 568*3 = 1704 != 1706
    std::vector<utxo> utxos = {
        make_bch_utxo(12000),
    };

    std::vector<payment_address> change_addrs = {test_address(), test_address(), test_address()};
    std::vector<double> ratios = {1.0/3.0, 1.0/3.0, 1.0/3.0};

    auto result = create_tx_template(
        utxos, 10000, test_address(), change_addrs, ratios,
        coin_selection_algorithm::largest_first);

    // With the fix, truncation remainder goes to the last change output.
    REQUIRE(result.has_value());
    auto const& [tx, indices, addrs, amounts] = *result;
    // 4 outputs: 1 dest + 3 change
    REQUIRE(amounts.size() == 4);
    REQUIRE(amounts[0] == 10000);  // destination
    // Change amounts: first two get truncated value, last gets remainder
    // total_change = 12000 - 10000 - 294 = 1706
    // amounts[1] = uint64_t(1706 * 1/3) = 568
    // amounts[2] = uint64_t(1706 * 1/3) = 568
    // amounts[3] = 1706 - 568 - 568 = 570 (remainder)
    REQUIRE(amounts[1] + amounts[2] + amounts[3] == 1706);
}

// ===========================================================================
// create_tx_template — send_all fee overestimate
// ===========================================================================

TEST_CASE("create_tx_template send_all overestimates fee by counting phantom change output", "[coin_selection]") {
    // Send-all with 1 change address. In send_all mode, no change output is
    // created — only the destination output. But outputs_size is calculated as
    // (1 change + 1 dest) * 34 = 68, counting a phantom change output.
    // Correct outputs_size should be 1 * 34 = 34.
    //
    // With 1 UTXO of 10000:
    //   Correct fee:  (10 + 148 + 34) * 1 = 192 → effective_amount = 9808
    //   Buggy fee:    (10 + 148 + 68) * 1 = 226 → effective_amount = 9774
    //   Difference: 34 sats overpaid as fee
    std::vector<utxo> utxos = {
        make_bch_utxo(10000),
    };

    std::vector<payment_address> change_addrs = {test_address()};
    std::vector<double> ratios = {1.0};

    auto result = create_tx_template(
        utxos, 0, test_address(), change_addrs, ratios,
        coin_selection_algorithm::send_all);

    REQUIRE(result.has_value());
    auto const& [tx, indices, addrs, amounts] = *result;

    // Only 1 output (destination, no change in send_all)
    REQUIRE(tx.outputs().size() == 1);
    // Fee should only count 1 output (destination), not 2.
    uint64_t const expected_fee = (base_tx_size + approx_input_size + approx_output_size) * sats_per_byte;  // 192
    uint64_t const expected_amount = 10000 - expected_fee;  // 9808
    REQUIRE(amounts[0] == expected_amount);
}

// ===========================================================================
// both_kinds of target category — NFT must not be silently burned
// ===========================================================================

TEST_CASE("select_utxos_both clean mode reports nft from target-category both_kinds utxo", "[coin_selection]") {
    // A both_kinds UTXO of the target category carries 500 fungible + an NFT.
    // The fungible amount should be counted in total_selected_token.
    // The NFT must be reported in collateral_nfts so the
    // caller can create a change output for it (otherwise it gets burned).
    std::vector<utxo> utxos = {
        make_both_kinds_utxo(token_cat_a, 500, 800),
        make_bch_utxo(5000),
    };

    auto result = select_utxos_both(utxos, 0, token_cat_a, 400,
                                     two_outputs_size, coin_selection_strategy::clean);
    REQUIRE(result.has_value());
    REQUIRE(result->total_selected_token == 500);
    // The NFT from the target-category both_kinds UTXO must appear in
    // collateral_nfts so the caller can create a change output to preserve it.
    REQUIRE(result->collateral_nfts.size() == 1);
    REQUIRE(result->collateral_nfts[0].id == token_cat_a);
}

TEST_CASE("select_utxos_both mixed mode reports nft from target-category both_kinds utxo", "[coin_selection]") {
    // Same scenario in mixed mode.
    std::vector<utxo> utxos = {
        make_both_kinds_utxo(token_cat_a, 600, 800),
        make_bch_utxo(10000),
    };

    auto result = select_utxos_both(utxos, 0, token_cat_a, 500,
                                     two_outputs_size, coin_selection_strategy::mixed);
    REQUIRE(result.has_value());
    REQUIRE(result->total_selected_token == 600);
    // The NFT must be reported even though the UTXO matched the target category.
    REQUIRE(result->collateral_nfts.size() == 1);
    REQUIRE(result->collateral_nfts[0].id == token_cat_a);
}

// ===========================================================================
// Mixed mode ranking — both_kinds of target category
// ===========================================================================

TEST_CASE("select_utxos_both mixed mode prioritizes target-category both_kinds over other-category fungible", "[coin_selection]") {
    // Request 500 tokens of cat_a + 5000 BCH.
    //
    // Buggy ranking (both_kinds always rank 3):
    //   rank 0: (none — no pure fungible of cat_a)
    //   rank 1: BCH-only (4000 sats)
    //   rank 2: other-category cat_b (200 tokens, 3000 sats)
    //   rank 3: target both_kinds cat_a (600 tokens, 2000 sats)
    //
    //   Iteration: BCH(4000) need_bch → select. cat_b(3000) need_bch → select
    //   (collateral!). both_kinds(cat_a, 2000) need_token → select.
    //   Result: 3 UTXOs, cat_b as collateral.
    //
    // Correct ranking (both_kinds of target = rank 0):
    //   rank 0: both_kinds cat_a (600 tokens, 2000 sats)
    //   rank 1: BCH-only (4000 sats)
    //   rank 2: other-category cat_b (200 tokens, 3000 sats)
    //
    //   Iteration: both_kinds(cat_a, 2000) need_bch+token → select (BCH=2000, token=600).
    //   BCH(4000) need_bch → select (BCH=6000).
    //   fee(2 inputs) = (10 + 2*148 + 68) = 374. need = 5000 + 374 + 800 = 6174.
    //   6000 < 6174 → need more. cat_b(3000) need_bch → select (BCH=9000, collateral).
    //   Actually still needs cat_b...
    //
    // Let's use bigger BCH-only so cat_b isn't needed with correct ranking:
    //   BCH-only = 6000, both_kinds cat_a = 2000 sats.
    //   Correct: both_kinds(2000) + BCH(6000) = 8000. need = 5000 + 374 + 800 = 6174. OK.
    //   Buggy: BCH(6000), need_bch 6000 < 6174 → cat_b(3000) = 9000. Then both_kinds.
    //   3 UTXOs with collateral vs 2 without.
    std::vector<utxo> utxos = {
        make_bch_utxo(6000),
        make_token_utxo(token_cat_b, 200, 3000),
        make_both_kinds_utxo(token_cat_a, 600, 2000),
    };

    auto result = select_utxos_both(utxos, 5000, token_cat_a, 500,
                                     two_outputs_size, coin_selection_strategy::mixed);
    REQUIRE(result.has_value());
    REQUIRE(result->total_selected_token == 600);
    // With correct ranking, both_kinds(cat_a) is rank 0, BCH-only is rank 1.
    // cat_b is not needed → no collateral.
    REQUIRE(result->selected_indices.size() == 2);
    REQUIRE(result->selected_indices[0] == 2);  // both_kinds cat_a (rank 0)
    REQUIRE(result->selected_indices[1] == 0);  // BCH-only (rank 1)
    REQUIRE(result->collateral_fts.empty());
}

// ===========================================================================
// create_tx_template — change_ratios validation
// ===========================================================================

TEST_CASE("create_tx_template rejects change ratios summing above 1.0", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_bch_utxo(100000),
    };

    std::vector<payment_address> change_addrs = {test_address(), test_address(), test_address()};
    std::vector<double> ratios = {0.6, 0.6, 0.6};  // sum = 1.8

    auto result = create_tx_template(
        utxos, 10000, test_address(), change_addrs, ratios,
        coin_selection_algorithm::largest_first);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::invalid_change);
}

TEST_CASE("create_tx_template rejects change ratios summing below 1.0", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_bch_utxo(100000),
    };

    std::vector<payment_address> change_addrs = {test_address(), test_address()};
    std::vector<double> ratios = {0.2, 0.3};  // sum = 0.5

    auto result = create_tx_template(
        utxos, 10000, test_address(), change_addrs, ratios,
        coin_selection_algorithm::largest_first);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::invalid_change);
}

// ===========================================================================
// create_token_split_tx_template — duplicate outpoints
// ===========================================================================

TEST_CASE("create_token_split_tx_template rejects duplicate outpoints", "[coin_selection]") {
    token_data_t token{token_cat_a, fungible{amount_t(500)}};
    auto utxo1 = make_utxo_with_point(tx_hash_1, 0, 5000, token);

    std::vector<utxo> available = { utxo1 };
    output_point::list outpoints = {
        output_point{tx_hash_1, 0},
        output_point{tx_hash_1, 0},  // duplicate
    };

    auto result = create_token_split_tx_template(outpoints, available, test_address());
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::operation_failed);
}

TEST_CASE("create_tx_template rejects negative change ratios", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_bch_utxo(100000),
    };

    std::vector<payment_address> change_addrs = {test_address(), test_address()};
    std::vector<double> ratios = {1.2, -0.2};  // sum = 1.0 but contains negative

    auto result = create_tx_template(
        utxos, 10000, test_address(), change_addrs, ratios,
        coin_selection_algorithm::largest_first);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::invalid_change);
}

TEST_CASE("create_tx_template rejects NaN change ratio", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_bch_utxo(100000),
    };

    std::vector<payment_address> change_addrs = {test_address()};
    std::vector<double> ratios = {std::numeric_limits<double>::quiet_NaN()};

    auto result = create_tx_template(
        utxos, 10000, test_address(), change_addrs, ratios,
        coin_selection_algorithm::largest_first);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::invalid_change);
}

TEST_CASE("create_tx_template rejects infinity change ratio", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_bch_utxo(100000),
    };

    std::vector<payment_address> change_addrs = {test_address()};
    std::vector<double> ratios = {std::numeric_limits<double>::infinity()};

    auto result = create_tx_template(
        utxos, 10000, test_address(), change_addrs, ratios,
        coin_selection_algorithm::largest_first);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::invalid_change);
}

// ===========================================================================
// create_tx_template — manual mode
// ===========================================================================

TEST_CASE("create_tx_template manual mode sends requested amount with change", "[coin_selection]") {
    // Manual mode keeps UTXO order and respects amount_to_send.
    std::vector<utxo> utxos = {
        make_bch_utxo(50000),
    };

    std::vector<payment_address> change_addrs = {test_address()};
    std::vector<double> ratios = {1.0};

    auto result = create_tx_template(
        utxos, 5000, test_address(), change_addrs, ratios,
        coin_selection_algorithm::manual);

    REQUIRE(result.has_value());
    auto const& [tx, indices, addrs, amounts] = *result;
    // Destination gets exactly 5000, rest goes to change minus fee
    REQUIRE(amounts[0] == 5000);
    REQUIRE(amounts.size() == 2);  // dest + change
    // fee = (10 + 148 + 2*34) * 1 = 226
    REQUIRE(amounts[1] == 50000 - 5000 - 226);
}

// ===========================================================================
// create_tx_template — dust output validation
// ===========================================================================

TEST_CASE("create_tx_template rejects sub-dust destination amount", "[coin_selection]") {
    std::vector<utxo> utxos = {
        make_bch_utxo(10000),
    };

    std::vector<payment_address> change_addrs = {test_address()};
    std::vector<double> ratios = {1.0};

    auto result = create_tx_template(
        utxos, 100, test_address(), change_addrs, ratios,
        coin_selection_algorithm::largest_first);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::insufficient_amount);
}

TEST_CASE("create_tx_template absorbs sub-dust change as fee", "[coin_selection]") {
    // UTXO = 1770, send = 1000.
    // fee = (10 + 148 + 2*34) * 1 = 226
    // change = 1770 - 1000 - 226 = 544 (below bch_dust_limit 546)
    // Sub-dust change is absorbed as fee → only destination output.
    std::vector<utxo> utxos = {
        make_bch_utxo(1770),
    };

    std::vector<payment_address> change_addrs = {test_address()};
    std::vector<double> ratios = {1.0};

    auto result = create_tx_template(
        utxos, 1000, test_address(), change_addrs, ratios,
        coin_selection_algorithm::largest_first);

    REQUIRE(result.has_value());
    auto const& [tx, indices, addrs, amounts] = *result;
    // Only destination, no change (544 sats absorbed as fee)
    REQUIRE(amounts.size() == 1);
    REQUIRE(amounts[0] == 1000);
}

TEST_CASE("create_tx_template keeps change when above dust", "[coin_selection]") {
    // UTXO = 1800, send = 1000.
    // fee = (10 + 148 + 2*34) * 1 = 226
    // change = 1800 - 1000 - 226 = 574 (above bch_dust_limit 546)
    std::vector<utxo> utxos = {
        make_bch_utxo(1800),
    };

    std::vector<payment_address> change_addrs = {test_address()};
    std::vector<double> ratios = {1.0};

    auto result = create_tx_template(
        utxos, 1000, test_address(), change_addrs, ratios,
        coin_selection_algorithm::largest_first);

    REQUIRE(result.has_value());
    auto const& [tx, indices, addrs, amounts] = *result;
    REQUIRE(amounts.size() == 2);
    REQUIRE(amounts[0] == 1000);
    REQUIRE(amounts[1] == 574);
}

TEST_CASE("create_tx_template send_all rejects when effective amount is sub-dust", "[coin_selection]") {
    // UTXO of 700 sats. fee = (10 + 148 + 34) * 1 = 192.
    // effective_amount = 700 - 192 = 508 (below bch_dust_limit 546).
    std::vector<utxo> utxos = {
        make_bch_utxo(700),
    };

    std::vector<payment_address> change_addrs = {test_address()};
    std::vector<double> ratios = {1.0};

    auto result = create_tx_template(
        utxos, 0, test_address(), change_addrs, ratios,
        coin_selection_algorithm::send_all);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == error::insufficient_amount);
}

// End Test Suite
