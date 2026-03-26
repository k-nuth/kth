// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_WALLET_COIN_SELECTION_HPP
#define KTH_DOMAIN_WALLET_COIN_SELECTION_HPP

#include <cstdint>
#include <expected>
#include <optional>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <kth/domain/chain/output_point.hpp>
#include <kth/domain/chain/token_data.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/chain/utxo.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/infrastructure/hash_define.hpp>

namespace kth::domain::wallet {

// Alias for readability: use bch_id when you want to select BCH (not a token).
// This is the null_hash (all zeros) which is not a valid token category.
inline constexpr hash_digest bch_id = null_hash;

// Fee estimation constants for P2PKH transactions.
// These match the values used by BCHN (src/policy/policy.cpp, GetDustThreshold) and
// Electron Cash (electroncash/tests/test_consolidate.py line 230:
// "tx size is roughly 148 * n_in + 34 * n_out + 10").
//
// sats_per_byte: BCH minimum relay fee (1000 sats/kB = 1 sat/byte).
// approx_input_size: 32 (prev txid) + 4 (prev index) + 1 (script len varint)
//                    + 107 (P2PKH unlock: 1 + 72 sig + 1 + 33 pubkey) + 4 (sequence) = 148.
// approx_output_size: 8 (value) + 1 (script len varint) + 25 (P2PKH lock script) = 34.
// base_tx_size: 4 (version) + 1 (input count varint) + 1 (output count varint) + 4 (locktime) = 10.
inline constexpr uint64_t sats_per_byte = 1;
inline constexpr uint64_t approx_input_size = 148;
inline constexpr uint64_t approx_output_size = 34;
inline constexpr uint64_t base_tx_size = 10;

// Minimum BCH for token-carrying outputs.
// This is NOT a consensus rule. The protocol dust threshold depends on the
// serialized output size: for token-bearing P2PKH outputs, the real dust
// range is ~648-675 sats (depending on token data size), calculated as
// 3 * (output_size + 148) with dustRelayFee = 1000 sats/kB.
//
// The 800 sats value is a conservative wallet convention introduced by
// Electron Cash (heuristic_dust_limit_for_token_bearing_output in token.py,
// commit 177e17e6, May 2023). The EC code acknowledges that the value should
// ideally be calculated dynamically based on the serialized token data size,
// but opted for a hard-coded worst-case constant for simplicity. That
// "temporary" hard-coding has remained unchanged since its introduction.
// We adopt the same value for compatibility with the BCH wallet ecosystem.
//
// See also: https://github.com/k-nuth/kth/issues/203
inline constexpr uint64_t token_dust_limit = 800;

// Dust threshold for standard P2PKH outputs (no token data).
// Formula: 3 * (output_size + input_overhead) * dustRelayFee / 1000
//        = 3 * (34 + 148) * 1000 / 1000 = 3 * 182 = 546 sats.
// Confirmed in BCHN (src/policy/policy.cpp, GetDustThreshold) and
// Electron Cash (electroncash/wallet.py line 114:
// "return 546  # hard-coded Bitcoin Cash dust threshold").
// Outputs below this value are rejected by nodes as non-standard.
inline constexpr uint64_t bch_dust_limit = 546;

// ---------------------------------------------------------------------------
// Selection strategy
// ---------------------------------------------------------------------------

enum class coin_selection_strategy {
    // Only selects "clean" UTXOs:
    //   - For BCH needs: selects UTXOs that carry no tokens.
    //   - For token needs: selects UTXOs that carry only the target token.
    //     All UTXOs carry some BCH (the protocol requires it), but token
    //     UTXOs typically carry only the dust minimum (800 sats). Wallets
    //     treat a token output with dust BCH as "BCH = 0" for display
    //     purposes. An output is considered "clean" when it is either pure
    //     BCH (no tokens) or token + dust (no excess BCH).
    // This is the wallet-friendly default. Mixed outputs (tokens + excess
    // BCH, or multiple token categories in one output) confuse wallet
    // balance displays and make spending harder.
    clean,

    // Allows selecting any UTXO regardless of what it carries.
    // When a selected UTXO carries unrelated tokens, those tokens are reported
    // in collateral_fts (fungible tokens) / collateral_nfts (non-fungible tokens) 
    // so the caller can create proper change outputs to preserve them.
    //
    // NOTE: This mode is experimental. Most BCH wallets do not handle mixed
    // outputs correctly. Use only if you understand the implications and your
    // application properly handles collateral token change.
    // Discussion with wallet developers and BCH protocol (CHIP) developers
    // did not reach a conclusive answer on whether mixed mode is ever
    // preferable to clean mode for general wallet use cases.
    // The clean mode exists specifically because
    // mixed outputs created by earlier implementations caused tokens to become
    // "stuck" in outputs that wallets couldn't spend properly. The split
    // template (create_token_split_tx_template) was created to recover
    // tokens from such dirty outputs.
    mixed
};

// ---------------------------------------------------------------------------
// Result
// ---------------------------------------------------------------------------

struct KD_API coin_selection_result {
    uint64_t total_selected_bch = 0;
    uint64_t total_selected_token = 0;   // 0 when selecting BCH
    uint64_t estimated_size = 0;
    std::vector<size_t> selected_indices;

    // Collateral tokens: tokens that were picked up incidentally because
    // a selected UTXO carried them. Only populated in mixed mode.
    // The caller MUST create change outputs for these to avoid burning them.
    std::unordered_map<hash_digest, uint64_t> collateral_fts;
    std::vector<chain::token_data_t> collateral_nfts;
};

// ---------------------------------------------------------------------------
// 1. select_utxos — select UTXOs to send a specific amount of ONE asset
// ---------------------------------------------------------------------------
//
// Selects UTXOs to cover `amount` of a single asset.
//
//   category == bch_id (null_hash)  →  select BCH
//   category == <real hash>         →  select that fungible token
//                                      (also selects BCH-only UTXOs for fees)
//
// When selecting a token, the function uses a two-phase approach in clean mode:
//   Phase 1: select token-carrying UTXOs (target category only)
//   Phase 2: select BCH-only UTXOs for fee coverage
//
// In mixed mode, any UTXO can be selected; collateral tokens are reported.
//
KD_API
std::expected<coin_selection_result, std::error_code>
select_utxos(
    std::vector<chain::utxo> const& available_utxos,
    uint64_t amount,
    size_t outputs_size,
    hash_digest const& category = bch_id,
    coin_selection_strategy strategy = coin_selection_strategy::clean
);

// ---------------------------------------------------------------------------
// 2. select_utxos_send_all — select ALL UTXOs for one asset (send-all mode)
// ---------------------------------------------------------------------------
//
// Selects all UTXOs that carry the specified asset.
// The effective amount is computed as total_input - fees.
//
//   category == bch_id   →  send all BCH (clean: skips token UTXOs)
//   category == <hash>   →  send all of that token
//
KD_API
std::expected<coin_selection_result, std::error_code>
select_utxos_send_all(
    std::vector<chain::utxo> const& available_utxos,
    size_t outputs_size,
    hash_digest const& category = bch_id
);

// ---------------------------------------------------------------------------
// 3. select_utxos_both — select UTXOs for BCH AND a token simultaneously
// ---------------------------------------------------------------------------
//
// Selects UTXOs to cover both `bch_amount` of BCH and `token_amount` of a
// specific fungible token in a single transaction. Used for operations that
// require both assets (e.g., liquidity pool deposits).
//
// token_category MUST NOT be bch_id (all zeros). Use select_utxos for
// BCH-only selection.
//
// Clean mode (default): two-phase selection
//   Phase 1: select UTXOs with only the target token
//   Phase 2: select BCH-only UTXOs for remaining BCH + fees
//
// Mixed mode: single-pass, may pick up unrelated tokens (reported as collateral)
//
KD_API
std::expected<coin_selection_result, std::error_code>
select_utxos_both(
    std::vector<chain::utxo> const& available_utxos,
    uint64_t bch_amount,
    hash_digest const& token_category,
    uint64_t token_amount,
    size_t outputs_size,
    coin_selection_strategy strategy = coin_selection_strategy::clean
);

// ---------------------------------------------------------------------------
// 4. create_token_split_tx_template — split mixed/dirty UTXOs into clean ones
// ---------------------------------------------------------------------------
//
// Creates an unsigned transaction that separates tokens from excess BCH in
// "dirty" UTXOs (outputs where tokens and non-dust BCH are mixed together).
//
// Background:
// BCH CashTokens are carried inside regular transaction outputs. Every output
// must hold some BCH (the protocol requires it). By convention, token-only
// outputs carry the minimum "dust" amount (800 sats). Wallets display such
// outputs as having zero BCH balance — the BCH is just a protocol carrier.
//
// A "dirty" or "mixed" output is one where a token sits alongside more BCH
// than the dust minimum. This can happen when:
//   - An earlier coin selection used the "mixed" strategy
//   - A third-party wallet or tool created the output without separating
//   - A swap or contract deposited change into a token-carrying output
//
// Most BCH wallets cannot correctly spend dirty outputs because they see the
// BCH and the token as a single indivisible unit. The excess BCH becomes
// effectively "stuck" alongside the token.
//
// What this function does:
//   1. Takes a list of specific outpoints (the dirty UTXOs to clean up)
//   2. Looks them up in the available UTXO set
//   3. Creates a transaction with:
//      - One input per dirty UTXO
//      - One output per token category: carries the consolidated fungible
//        amount with exactly dust (800 sats) of BCH
//      - One BCH-only output: carries all the excess BCH minus the tx fee
//   4. All outputs are sent to destination_address
//
// The result is a set of "clean" UTXOs: each token output has minimal BCH,
// and the freed BCH is in its own separate output.
//
// Limitations:
//   - Only fungible tokens are supported. NFT/both-kinds UTXOs will cause
//     an error (the splitting logic for NFTs is more complex and not yet
//     implemented).
//   - The transaction is unsigned (a template). The caller must sign it
//     before broadcasting.
//
// Returns: (transaction, output_addresses, output_amounts)
//   - transaction: the unsigned transaction template
//   - output_addresses: the payment address for each output
//   - output_amounts: the BCH amount for each output
//
using token_split_result = std::tuple<
    chain::transaction,
    payment_address::list,
    std::vector<uint64_t>
>;

KD_API
std::expected<token_split_result, std::error_code>
create_token_split_tx_template(
    chain::output_point::list const& outpoints_to_split,
    std::vector<chain::utxo> const& available_utxos,
    payment_address const& destination_address
);

// ---------------------------------------------------------------------------
// 5. create_tx_template — create an unsigned transaction from selected UTXOs
// ---------------------------------------------------------------------------
//
// Creates an unsigned transaction template for sending BCH.
// Performs UTXO selection, creates inputs with placeholder scripts,
// creates the destination output, and distributes change across
// the provided change addresses using the given ratios.
//
// Parameters:
//   - available_utxos: the UTXO set to select from
//   - amount_to_send: the amount to send (0 = send all)
//   - destination_address: where to send the BCH
//   - change_addresses: one or more addresses for change outputs
//   - change_ratios: how to split change across change_addresses (must sum to 1.0)
//   - selection_algo: which UTXO ordering to use
//
// Returns: (transaction, selected_utxo_indices, output_addresses, output_amounts)
//

// UTXO ordering strategy for coin selection.
enum class coin_selection_algorithm {
    smallest_first,
    largest_first,
    manual,             // keeps the original UTXO order
    send_all            // uses all available UTXOs

    // knapsack,          // classic Bitcoin Core algorithm, minimizes change
    // fifo,              // first-in first-out, spends oldest UTXOs first
    // branch_and_bound,  // exact-match search to avoid change output entirely
    // privacy,           // selects similar-sized UTXOs to hinder chain analysis
};

using tx_template_result = std::tuple<
    chain::transaction,
    std::vector<uint32_t>,
    payment_address::list,
    std::vector<uint64_t>
>;

KD_API
std::expected<tx_template_result, std::error_code>
create_tx_template(
    std::vector<chain::utxo> available_utxos,
    uint64_t amount_to_send,
    payment_address const& destination_address,
    std::vector<payment_address> const& change_addresses,
    std::vector<double> const& change_ratios,
    coin_selection_algorithm selection_algo
);

// Overload that generates random change ratios.
KD_API
std::expected<tx_template_result, std::error_code>
create_tx_template(
    std::vector<chain::utxo> available_utxos,
    uint64_t amount_to_send,
    payment_address const& destination_address,
    std::vector<payment_address> const& change_addresses,
    coin_selection_algorithm selection_algo
);

// Generate random ratios that sum to 1.0 for distributing change.
KD_API
std::vector<double> make_change_ratios(size_t change_count);

} // namespace kth::domain::wallet

#endif // KTH_DOMAIN_WALLET_COIN_SELECTION_HPP
