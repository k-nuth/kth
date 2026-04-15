// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/coin_selection.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <numeric>
#include <random>
#include <system_error>
#include <vector>

#include <kth/domain/chain/input.hpp>
#include <kth/domain/chain/output.hpp>
#include <kth/domain/chain/output_point.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/token_data.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/chain/utxo.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/infrastructure/error.hpp>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

namespace kth::domain::wallet {

using namespace kth::domain::chain;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// -- UTXO classification helpers --

// Returns true if the UTXO carries no token data at all.
bool is_bch_only(utxo const& u) {
    return !u.token_data().has_value();
}

// Returns true if the UTXO carries fungible tokens matching `category`.
// Matches both pure `fungible` and `both_kinds` (fungible + NFT) variants.
bool has_target_fungible(utxo const& u, hash_digest const& category) {
    if ( !u.token_data().has_value()) return false;
    auto const& token = u.token_data().value();
    if (token.id != category) return false;
    return std::holds_alternative<fungible>(token.data)
        || std::holds_alternative<both_kinds>(token.data);
}

// Returns the fungible amount of a UTXO's token data.
// Precondition: has_target_fungible(u, category) == true.
uint64_t get_fungible_amount(utxo const& u) {
    return uint64_t(get_amount(u.token_data().value()));
}

// If the UTXO is both_kinds, report its NFT as collateral so the caller
// can create a change output for it. Without this, the NFT would be silently
// burned when the UTXO is consumed as an input.
// Only the NFT portion is stored — the fungible amount is tracked separately
// in collateral_fts or total_selected_token to avoid double-counting.
void track_nft_from_both_kinds(utxo const& u, coin_selection_result& result) {
    if ( !u.token_data().has_value()) return;
    auto const& token = u.token_data().value();
    if (std::holds_alternative<both_kinds>(token.data)) {
        auto const& bk = std::get<both_kinds>(token.data);
        result.collateral_nfts.emplace_back(token_data_t{token.id, bk.non_fungible_part});
    }
}

// Track collateral tokens from a UTXO that was selected for BCH (mixed mode).
// For both_kinds UTXOs, the fungible portion goes to collateral_fts and only
// the NFT portion goes to collateral_nfts (to avoid double-counting).
void track_collateral(utxo const& u, hash_digest const& target_category,
                      coin_selection_result& result) {
    if ( !u.token_data().has_value()) return;
    auto const& token = u.token_data().value();

    if (std::holds_alternative<fungible>(token.data)) {
        if (token.id != target_category) {
            result.collateral_fts[token.id] +=
                uint64_t(std::get<fungible>(token.data).amount);
        }
    } else if (std::holds_alternative<both_kinds>(token.data)) {
        auto const& bk = std::get<both_kinds>(token.data);
        if (token.id != target_category) {
            result.collateral_fts[token.id] += uint64_t(bk.fungible_part.amount);
        }
        result.collateral_nfts.emplace_back(token_data_t{token.id, bk.non_fungible_part});
    } else {
        // pure non_fungible
        result.collateral_nfts.emplace_back(token);
    }
}

// -- Fee estimation --

uint64_t estimate_fee(size_t num_inputs, size_t outputs_size) {
    auto const size = base_tx_size + (num_inputs * approx_input_size) + outputs_size;
    return size * sats_per_byte;
}

// -- Transaction building helpers --

constexpr uint32_t tx_version = 2;
constexpr uint32_t tx_locktime = 0;

chain::input make_placeholder_input(chain::output_point const& point) {
    auto const ops = chain::script::to_pay_public_key_hash_pattern_unlocking_placeholder(71, 33);
    chain::script placeholder_script;
    placeholder_script.from_operations(ops);
    return chain::input{point, std::move(placeholder_script), max_input_sequence};
}

chain::script make_p2pkh_script(payment_address const& addr) {
    auto ops = chain::script::to_pay_public_key_hash_pattern(addr.hash20());
    chain::script s;
    s.from_operations(ops);
    return s;
}

chain::output make_bch_output(uint64_t amount, payment_address const& addr) {
    return chain::output{amount, make_p2pkh_script(addr), std::nullopt};
}

chain::output make_token_output(uint64_t bch_amount, payment_address const& addr, token_data_t token) {
    return chain::output{bch_amount, make_p2pkh_script(addr), std::move(token)};
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// 1. select_utxos
// ---------------------------------------------------------------------------

std::expected<coin_selection_result, std::error_code>
select_utxos(
    std::vector<utxo> const& available_utxos,
    uint64_t amount,
    size_t outputs_size,
    hash_digest const& category,
    coin_selection_strategy strategy
) {
    bool const selecting_bch = (category == bch_id);

    if (selecting_bch) {
        // ----- BCH selection -----
        coin_selection_result result{};

        for (size_t i = 0; i < available_utxos.size(); ++i) {
            auto const& u = available_utxos[i];

            auto const fee = estimate_fee(result.selected_indices.size(), outputs_size);
            if (result.total_selected_bch >= amount + fee) {
                result.estimated_size = base_tx_size + (result.selected_indices.size() * approx_input_size) + outputs_size;
                return result;
            }

            if (strategy == coin_selection_strategy::clean) {
                // Clean mode: skip UTXOs that carry tokens.
                if ( !is_bch_only(u)) continue;
            }

            result.total_selected_bch += u.amount();
            result.selected_indices.push_back(i);

            if (strategy == coin_selection_strategy::mixed) {
                track_collateral(u, bch_id, result);
            }
        }

        // Final check after loop
        auto const fee = estimate_fee(result.selected_indices.size(), outputs_size);
        if (result.total_selected_bch >= amount + fee) {
            result.estimated_size = base_tx_size + (result.selected_indices.size() * approx_input_size) + outputs_size;
            return result;
        }
        return std::unexpected(error::insufficient_amount);
    }

    // ----- Token selection (two-phase in clean, single-pass in mixed) -----
    return select_utxos_both(available_utxos, 0, category, amount, outputs_size, strategy);
}

// ---------------------------------------------------------------------------
// 2. select_utxos_send_all
// ---------------------------------------------------------------------------

std::expected<coin_selection_result, std::error_code>
select_utxos_send_all(
    std::vector<utxo> const& available_utxos,
    size_t outputs_size,
    hash_digest const& category
) {
    bool const selecting_bch = (category == bch_id);
    coin_selection_result result{};

    if (selecting_bch) {
        // Send all BCH: select all BCH-only UTXOs (clean behavior).
        for (size_t i = 0; i < available_utxos.size(); ++i) {
            auto const& u = available_utxos[i];
            if ( !is_bch_only(u)) continue;

            result.total_selected_bch += u.amount();
            result.selected_indices.push_back(i);
        }

        if (result.selected_indices.empty()) {
            return std::unexpected(error::insufficient_amount);
        }

        auto const fee = estimate_fee(result.selected_indices.size(), outputs_size);

        if (result.total_selected_bch <= fee) {
            return std::unexpected(error::insufficient_amount);
        }

        // The effective amount is total - fee (caller uses total_selected_bch - fee).
        result.estimated_size = base_tx_size + (result.selected_indices.size() * approx_input_size) + outputs_size;
        return result;
    }

    // Send all of a specific token: select all UTXOs with that token.
    for (size_t i = 0; i < available_utxos.size(); ++i) {
        auto const& u = available_utxos[i];

        if (has_target_fungible(u, category)) {
            result.total_selected_token += get_fungible_amount(u);
            result.total_selected_bch += u.amount();  // dust BCH from token UTXOs
            result.selected_indices.push_back(i);
            track_nft_from_both_kinds(u, result);
        }
    }

    if (result.selected_indices.empty()) {
        return std::unexpected(error::insufficient_amount);
    }

    // We also need BCH-only UTXOs to cover the fee (token UTXOs usually
    // only carry dust amounts).
    for (size_t i = 0; i < available_utxos.size(); ++i) {
        // Skip already selected
        if (std::find(result.selected_indices.begin(),
                      result.selected_indices.end(), i) != result.selected_indices.end()) {
            continue;
        }

        auto const& u = available_utxos[i];
        if ( !is_bch_only(u)) continue;

        auto const fee = estimate_fee(result.selected_indices.size(), outputs_size);
        // We need enough BCH to cover fee + dust for the token output
        if (result.total_selected_bch >= fee + token_dust_limit) break;

        result.total_selected_bch += u.amount();
        result.selected_indices.push_back(i);
    }

    auto const fee = estimate_fee(result.selected_indices.size(), outputs_size);

    if (result.total_selected_bch < fee + token_dust_limit) {
        return std::unexpected(error::insufficient_amount);
    }

    result.estimated_size = base_tx_size + (result.selected_indices.size() * approx_input_size) + outputs_size;
    return result;
}

// ---------------------------------------------------------------------------
// 3. select_utxos_both
// ---------------------------------------------------------------------------

std::expected<coin_selection_result, std::error_code>
select_utxos_both(
    std::vector<utxo> const& available_utxos,
    uint64_t bch_amount,
    hash_digest const& token_category,
    uint64_t token_amount,
    size_t outputs_size,
    coin_selection_strategy strategy
) {
    // precondition: token_category must not be bch_id
    if (token_category == bch_id) {
        return std::unexpected(error::operation_failed);
    }

    coin_selection_result result{};

    if (strategy == coin_selection_strategy::clean) {
        // ----- CLEAN: two-phase selection -----

        // Phase 1: Select UTXOs with ONLY the target fungible token.
        size_t token_inputs_size = 0;

        for (size_t i = 0; i < available_utxos.size(); ++i) {
            if (result.total_selected_token >= token_amount) break;

            auto const& u = available_utxos[i];
            if ( !has_target_fungible(u, token_category)) continue;

            result.total_selected_token += get_fungible_amount(u);
            result.total_selected_bch += u.amount();  // dust BCH
            result.selected_indices.push_back(i);
            token_inputs_size += approx_input_size;
            track_nft_from_both_kinds(u, result);
        }

        if (result.total_selected_token < token_amount) {
            return std::unexpected(error::insufficient_amount);
        }

        // Phase 2: Select BCH-only UTXOs for remaining BCH needs + fees.
        size_t num_bch_inputs = 0;

        for (size_t i = 0; i < available_utxos.size(); ++i) {
            // Skip already selected
            if (std::find(result.selected_indices.begin(),
                          result.selected_indices.end(), i) != result.selected_indices.end()) {
                continue;
            }

            auto const& u = available_utxos[i];

            auto const bch_inputs_size = num_bch_inputs * approx_input_size;
            auto const estimated_size = base_tx_size + token_inputs_size + bch_inputs_size + outputs_size;
            auto const estimated_fee = estimated_size * sats_per_byte;
            // Reserve token_dust_limit for the token output (dust BCH that must accompany it).
            auto const total_bch_needed = bch_amount + estimated_fee + token_dust_limit;

            if (result.total_selected_bch >= total_bch_needed) break;

            // Clean mode: only BCH-only UTXOs
            if ( !is_bch_only(u)) continue;

            result.total_selected_bch += u.amount();
            result.selected_indices.push_back(i);
            ++num_bch_inputs;
        }

        // Final fee check
        auto const bch_inputs_size = num_bch_inputs * approx_input_size;
        auto const estimated_size = base_tx_size + token_inputs_size + bch_inputs_size + outputs_size;
        auto const estimated_fee = estimated_size * sats_per_byte;
        auto const total_bch_needed = bch_amount + estimated_fee + token_dust_limit;

        if (result.total_selected_bch < total_bch_needed) {
            return std::unexpected(error::insufficient_amount);
        }

        result.estimated_size = estimated_size;
        return result;
    }

    // ----- MIXED: single-pass selection -----
    // Sort a copy of indices by ranking:
    //   0: target category with fungible amount (pure fungible or both_kinds)
    //   1: BCH-only (by BCH amount descending)
    //   2: other-category fungible
    //   3: other-category both_kinds
    //   4: NFT (pure non-fungible)
    std::vector<size_t> indices(available_utxos.size());
    std::iota(indices.begin(), indices.end(), 0);

    std::sort(indices.begin(), indices.end(),
        [&](size_t a_idx, size_t b_idx) {
            auto const& a = available_utxos[a_idx];
            auto const& b = available_utxos[b_idx];

            auto rank = [&](utxo const& u) -> int {
                if ( !u.token_data().has_value()) return 1;
                auto const& token = u.token_data().value();
                if (std::holds_alternative<fungible>(token.data)) {
                    return (token.id == token_category) ? 0 : 2;
                }
                if (std::holds_alternative<both_kinds>(token.data)) {
                    return (token.id == token_category) ? 0 : 3;
                }
                return 4;  // pure NFT
            };

            auto const ra = rank(a);
            auto const rb = rank(b);
            if (ra != rb) return ra < rb;

            // Within target fungible, sort by token amount descending
            if (ra == 0) {
                return get_fungible_amount(a) > get_fungible_amount(b);
            }
            // Otherwise sort by BCH amount descending
            return a.amount() > b.amount();
        });

    for (auto const i : indices) {
        auto const& u = available_utxos[i];

        auto const estimated_size = base_tx_size
            + (result.selected_indices.size() * approx_input_size)
            + outputs_size;
        auto const estimated_fee = estimated_size * sats_per_byte;
        // Reserve token_dust_limit for the token output (dust BCH that must accompany it).
        auto const total_bch_needed = bch_amount + estimated_fee + token_dust_limit;

        bool const need_more_bch = result.total_selected_bch < total_bch_needed;
        bool const need_more_token = result.total_selected_token < token_amount;

        if ( !need_more_bch && !need_more_token) break;

        if (need_more_bch) {
            // Need BCH: take this UTXO regardless of what it carries
            result.total_selected_bch += u.amount();
            result.selected_indices.push_back(i);

            if (has_target_fungible(u, token_category)) {
                result.total_selected_token += get_fungible_amount(u);
                track_nft_from_both_kinds(u, result);
            } else {
                track_collateral(u, token_category, result);
            }
        } else if (need_more_token) {
            // Only need tokens: only take if it has the target token
            if (has_target_fungible(u, token_category)) {
                result.total_selected_token += get_fungible_amount(u);
                result.total_selected_bch += u.amount();
                result.selected_indices.push_back(i);
                track_nft_from_both_kinds(u, result);
            }
        }
    }

    // Final check
    auto const estimated_size = base_tx_size
        + (result.selected_indices.size() * approx_input_size)
        + outputs_size;
    auto const estimated_fee = estimated_size * sats_per_byte;
    auto const total_bch_needed = bch_amount + estimated_fee + token_dust_limit;

    if (result.total_selected_bch < total_bch_needed || result.total_selected_token < token_amount) {
        return std::unexpected(error::insufficient_amount);
    }

    result.estimated_size = estimated_size;
    return result;
}

// ---------------------------------------------------------------------------
// 4. create_token_split_tx_template
// ---------------------------------------------------------------------------

// Size estimates specific to token split transactions.
constexpr size_t split_approx_input_size = 149;
constexpr size_t split_approx_output_size_p2pkh = 34;
constexpr size_t split_approx_output_size_token = 73;

std::expected<token_split_result, std::error_code>
create_token_split_tx_template(
    chain::output_point::list const& outpoints_to_split,
    std::vector<chain::utxo> const& available_utxos,
    payment_address const& destination_address
) {
    // precondition: outpoints_to_split must not be empty
    if (outpoints_to_split.empty()) {
        return std::unexpected(error::empty_utxo_list);
    }

    // precondition: outpoints_to_split must not contain duplicates
    {
        boost::unordered_flat_set<chain::point> seen;
        for (auto const& outpoint : outpoints_to_split) {
            if ( !seen.insert(outpoint).second) {
                return std::unexpected(error::operation_failed);
            }
        }
    }

    // precondition: all outpoints in outpoints_to_split must be present in available_utxos (checked during processing)

    // Build a map of outpoints -> UTXOs for fast lookup.
    boost::unordered_flat_map<chain::point, utxo> utxo_map;
    for (auto const& u : available_utxos) {
        utxo_map[u.point()] = u;
    }

    // Collect the UTXOs to split and accumulate totals.
    std::vector<utxo> utxos_to_split;
    uint64_t total_bch = 0;
    std::unordered_map<hash_digest, uint64_t> token_amounts;  // category -> total amount

    for (auto const& outpoint : outpoints_to_split) {
        auto it = utxo_map.find(outpoint);
        if (it == utxo_map.end()) {
            return std::unexpected(error::utxo_not_found);
        }

        auto const& u = it->second;
        utxos_to_split.push_back(u);
        total_bch += u.amount();

        if (u.token_data()) {
            auto const& token = u.token_data().value();
            if (std::holds_alternative<fungible>(token.data)) {
                token_amounts[token.id] += uint64_t(std::get<fungible>(token.data).amount);
            } else {
                // NFT/both-kinds tokens are not supported yet.
                return std::unexpected(error::operation_failed);
            }
        }
    }

    // Create inputs with placeholder scripts.
    chain::transaction::ins inputs;
    for (auto const& u : utxos_to_split) {
        inputs.push_back(make_placeholder_input(u.point()));
    }

    // Estimate transaction size and fee.
    // First estimate without BCH change output, then add it if there's excess.
    size_t const base_estimated_size = base_tx_size
        + (inputs.size() * split_approx_input_size)
        + (token_amounts.size() * split_approx_output_size_token);

    uint64_t const bch_for_tokens = token_amounts.size() * token_dust_limit;

    // Try with BCH change output included.
    size_t const size_with_bch_output = base_estimated_size + split_approx_output_size_p2pkh;
    uint64_t const fee_with_bch_output = size_with_bch_output * sats_per_byte;

    uint64_t bch_remaining;
    uint64_t estimated_fee;

    if (total_bch >= bch_for_tokens + fee_with_bch_output &&
        total_bch - bch_for_tokens - fee_with_bch_output > 0) {
        // There's excess BCH — include the change output in the fee estimate.
        estimated_fee = fee_with_bch_output;
        bch_remaining = total_bch - bch_for_tokens - estimated_fee;
    } else {
        // No excess BCH — don't count the change output in the fee.
        estimated_fee = base_estimated_size * sats_per_byte;
        if (total_bch < bch_for_tokens + estimated_fee) {
            return std::unexpected(error::insufficient_fee);
        }
        bch_remaining = total_bch - bch_for_tokens - estimated_fee;
    }

    // Create outputs.
    chain::transaction::outs outputs;
    payment_address::list output_addresses;
    std::vector<uint64_t> output_amounts;

    // One output per token category: consolidated tokens + dust BCH.
    for (auto const& [category, amount] : token_amounts) {
        token_data_t td{category, fungible{amount_t(amount)}};
        outputs.push_back(make_token_output(token_dust_limit, destination_address, std::move(td)));
        output_addresses.push_back(destination_address);
        output_amounts.push_back(token_dust_limit);
    }

    // BCH-only output with the freed excess BCH.
    // If below dust threshold, absorb as extra fee instead of creating
    // a non-standard output that nodes would reject.
    if (bch_remaining >= bch_dust_limit) {
        outputs.push_back(make_bch_output(bch_remaining, destination_address));
        output_addresses.push_back(destination_address);
        output_amounts.push_back(bch_remaining);
    }

    if (outputs.empty()) {
        return std::unexpected(error::operation_failed);
    }

    return std::make_tuple(
        chain::transaction(tx_version, tx_locktime, std::move(inputs), std::move(outputs)),
        std::move(output_addresses),
        std::move(output_amounts)
    );
}

// ---------------------------------------------------------------------------
// 5. create_tx_template
// ---------------------------------------------------------------------------

std::vector<double> make_change_ratios(size_t change_count) {
    std::vector<double> ratios(change_count, 0.0);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0, 1);
    std::generate(ratios.begin(), ratios.end(), [&gen, &dis]() { return dis(gen); });
    double const sum = std::accumulate(ratios.begin(), ratios.end(), 0.0);
    std::transform(ratios.begin(), ratios.end(), ratios.begin(), [sum](double v) { return v / sum; });
    return ratios;
}

std::expected<tx_template_result, std::error_code>
create_tx_template(
    std::vector<chain::utxo> available_utxos,
    uint64_t amount_to_send,
    payment_address const& destination_address,
    std::vector<payment_address> const& change_addresses,
    std::vector<double> const& change_ratios,
    coin_selection_algorithm selection_algo
) {
    if (available_utxos.empty()) {
        return std::unexpected(error::empty_utxo_list);
    }
    if (change_addresses.empty()) {
        return std::unexpected(error::invalid_change);
    }
    if (change_ratios.size() != change_addresses.size()) {
        return std::unexpected(error::invalid_change);
    }
    for (auto const r : change_ratios) {
        if ( !std::isfinite(r) || r < 0.0) {
            return std::unexpected(error::invalid_change);
        }
    }
    auto const ratios_sum = std::accumulate(change_ratios.begin(), change_ratios.end(), 0.0);
    if (ratios_sum < 0.999 || ratios_sum > 1.001) {
        return std::unexpected(error::invalid_change);
    }

    bool const send_all = amount_to_send == 0 ||
        selection_algo == coin_selection_algorithm::send_all;

    if ( !send_all && amount_to_send < bch_dust_limit) {
        return std::unexpected(error::insufficient_amount);
    }

    if (send_all && change_addresses.size() > 1) {
        return std::unexpected(error::invalid_change);
    }

    // Remember original positions before sorting.
    boost::unordered_flat_map<chain::point, uint32_t> utxo_positions;
    for (size_t i = 0; i < available_utxos.size(); ++i) {
        utxo_positions[available_utxos[i].point()] = uint32_t(i);
    }

    // Sort UTXOs if needed.
    if (selection_algo == coin_selection_algorithm::smallest_first) {
        std::sort(available_utxos.begin(), available_utxos.end(),
            [](chain::utxo const& a, chain::utxo const& b) { return a.amount() < b.amount(); });
    } else if (selection_algo == coin_selection_algorithm::largest_first) {
        std::sort(available_utxos.begin(), available_utxos.end(),
            [](chain::utxo const& a, chain::utxo const& b) { return a.amount() > b.amount(); });
    }

    // In send_all mode, only the destination output is created (no change).
    size_t const output_count = send_all ? 1 : change_addresses.size() + 1;
    size_t const outputs_size = output_count * approx_output_size;

    // Select UTXOs for BCH only, using clean mode (never touches token UTXOs).
    // WARNING: if category is changed from bch_id to a token, or if strategy
    // is changed to mixed, collateral token outputs will be created but their
    // BCH cost (token_dust_limit each) is not deducted from the change
    // calculation, producing an invalid transaction.
    // See: https://github.com/k-nuth/kth/issues/206
    auto const selection = send_all
        ? select_utxos_send_all(available_utxos, outputs_size, bch_id)
        : select_utxos(available_utxos, amount_to_send, outputs_size, bch_id, coin_selection_strategy::clean);

    if ( !selection) {
        return std::unexpected(selection.error());
    }

    auto const& sel = *selection;
    auto const estimated_fee = sel.estimated_size * sats_per_byte;

    // In send_all mode, the effective amount is total - fee.
    uint64_t const effective_amount = send_all
        ? sel.total_selected_bch - estimated_fee
        : amount_to_send;

    if (effective_amount < bch_dust_limit) {
        return std::unexpected(error::insufficient_amount);
    }

    // Build inputs and track original indices.
    chain::transaction::ins inputs;
    std::vector<uint32_t> selected_utxo_indices;

    for (auto const idx : sel.selected_indices) {
        auto const& u = available_utxos[idx];
        inputs.push_back(make_placeholder_input(u.point()));

        auto const it = utxo_positions.find(u.point());
        if (it != utxo_positions.end()) {
            selected_utxo_indices.push_back(it->second);
        }
    }

    // Build outputs.
    chain::transaction::outs outputs;
    payment_address::list output_addresses;
    std::vector<uint64_t> output_amounts;

    // Destination output.
    outputs.push_back(make_bch_output(effective_amount, destination_address));
    output_addresses.push_back(destination_address);
    output_amounts.push_back(effective_amount);

    // Change outputs (only when not send_all).
    if ( !send_all) {
        uint64_t const total_change = sel.total_selected_bch - (effective_amount + estimated_fee);

        // If total change is below dust, absorb it as extra fee.
        if (total_change >= bch_dust_limit) {
            uint64_t accumulated = 0;
            for (size_t i = 0; i < change_addresses.size(); ++i) {
                uint64_t change_amount = uint64_t(total_change * change_ratios[i]);
                // Assign truncation remainder to the last change output.
                if (i == change_addresses.size() - 1) {
                    change_amount = total_change - accumulated;
                }
                // Skip sub-dust change outputs (absorbed as fee).
                if (change_amount >= bch_dust_limit) {
                    outputs.push_back(make_bch_output(change_amount, change_addresses[i]));
                    output_addresses.push_back(change_addresses[i]);
                    output_amounts.push_back(change_amount);
                }
                accumulated += change_amount;
            }
        }
    }

    // Collateral token change outputs (tokens picked up incidentally).
    if ( !sel.collateral_fts.empty() || !sel.collateral_nfts.empty()) {
        auto const& change_address = change_addresses[0];

        for (auto const& [category, amount] : sel.collateral_fts) {
            token_data_t td{category, fungible{amount_t(amount)}};
            outputs.push_back(make_token_output(token_dust_limit, change_address, std::move(td)));
            output_addresses.push_back(change_address);
            output_amounts.push_back(token_dust_limit);
        }

        for (auto const& token : sel.collateral_nfts) {
            outputs.push_back(make_token_output(token_dust_limit, change_address, token));
            output_addresses.push_back(change_address);
            output_amounts.push_back(token_dust_limit);
        }
    }

    return std::make_tuple(
        chain::transaction(tx_version, tx_locktime, std::move(inputs), std::move(outputs)),
        std::move(selected_utxo_indices),
        std::move(output_addresses),
        std::move(output_amounts)
    );
}

std::expected<tx_template_result, std::error_code>
create_tx_template(
    std::vector<chain::utxo> available_utxos,
    uint64_t amount_to_send,
    payment_address const& destination_address,
    std::vector<payment_address> const& change_addresses,
    coin_selection_algorithm selection_algo
) {
    return create_tx_template(
        std::move(available_utxos),
        amount_to_send,
        destination_address,
        change_addresses,
        make_change_ratios(change_addresses.size()),
        selection_algo
    );
}

} // namespace kth::domain::wallet
