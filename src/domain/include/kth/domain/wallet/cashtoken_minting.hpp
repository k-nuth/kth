// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_WALLET_CASHTOKEN_MINTING_HPP
#define KTH_DOMAIN_WALLET_CASHTOKEN_MINTING_HPP

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <kth/domain/chain/output.hpp>
#include <kth/domain/chain/point.hpp>
#include <kth/domain/chain/token_data.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/chain/utxo.hpp>
#include <kth/domain/constants/bch.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/machine/script_flags.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/infrastructure/hash_define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::wallet::cashtoken {

// ---------------------------------------------------------------------------
// Protocol constants
// ---------------------------------------------------------------------------

// BCH standardness caps the OP_RETURN payload (the pushed bytes after
// `OP_RETURN`, not the whole output script) at 220 bytes. Transactions
// carrying a larger null-data payload are rejected by default relay
// policy.
inline constexpr size_t max_op_return_payload_size = 220;

// NFT commitment-size limits are scheduled to grow over time:
//   - 2023-May Descartes: 40  bytes (`max_token_commitment_length_descartes`)
//   - 2026-May Leibniz:  128 bytes (`max_token_commitment_length_leibniz`)
//
// Builders expose `script_flags_t script_flags` on the params structs
// that create or modify commitments. This field has NO default — it is
// required because a wrongly-defaulted flag is a silent way to produce
// either an under-constrained TX (accepted by a stricter ruleset that
// later rejects it at broadcast) or an over-constrained one (needlessly
// rejecting valid commitments). The impl calls
// `chain::max_token_commitment_length(script_flags)` to pick the cap:
// pass `0` to target current mainnet (Descartes) or a flags value with
// `script_flags::bch_loops` set to target the post-Leibniz ruleset.


// ---------------------------------------------------------------------------
// Commitment helpers
// ---------------------------------------------------------------------------

// Encode an integer as a minimally-serialised Bitcoin Script number
// (VM-number) suitable for use as an NFT commitment. This matches the
// on-chain convention used by collections that treat the commitment as
// a counter (`0`, `1`, ... `N`) and by mint covenants that advance the
// counter in the preserved minting NFT.
//
// Returns the encoded bytes on success; `std::unexpected` when `value`
// is outside the VM script-number range (the encoder rejects
// `INT64_MIN` because its two's-complement negation overflows, which
// the spec forbids — BCHN's `CScriptNum::serialize` has the same guard
// via `validRange`). Value `0` encodes as an empty `data_chunk`
// distinctly from the error case, so the success `.value()` is
// unambiguous.
//
// `int64_t` is the VM's native signed-number type; negative values
// round-trip correctly but are not expected for counter use.
KD_API
std::expected<data_chunk, std::error_code> encode_nft_number(int64_t value);


// ---------------------------------------------------------------------------
// Building blocks (Level 1): output factories
//
// These helpers build a single chain::output whose payload contains
// CashTokens data. They do NOT build transactions and do NOT perform coin
// selection — they are the primitive used by the higher-level transaction
// builders below.
// ---------------------------------------------------------------------------

// Build an output that carries a fungible token payload of `ft_amount`
// units of category `category_id`, sent to `destination`.
// The BCH `satoshis` default (1000) is above the token dust limit (800);
// see wallet::token_dust_limit.
KD_API
chain::output create_ft_output(
    payment_address const& destination,
    hash_digest const& category_id,
    uint64_t ft_amount,
    uint64_t satoshis = 1000
);

// Build an output that carries a non-fungible token (NFT) of category
// `category_id`. Used for both immutable NFTs (capability == none),
// mutable NFTs, and minting NFTs.
KD_API
chain::output create_nft_output(
    payment_address const& destination,
    hash_digest const& category_id,
    chain::capability_t capability,
    data_chunk const& commitment,
    uint64_t satoshis = 1000
);

// Build an output that carries BOTH a fungible amount AND an NFT of the
// same category in a single output. Used for combined genesis (e.g. issue
// a FT supply and a minting NFT together in the same output).
KD_API
chain::output create_combined_token_output(
    payment_address const& destination,
    hash_digest const& category_id,
    uint64_t ft_amount,
    chain::capability_t capability,
    data_chunk const& commitment,
    uint64_t satoshis = 1000
);


// ---------------------------------------------------------------------------
// Common result types
// ---------------------------------------------------------------------------

// Generic unsigned-transaction result: the built transaction plus the
// indices of inputs that the caller must sign.
struct KD_API token_tx_result {
    chain::transaction transaction;
    std::vector<uint32_t> signing_indices;
};

// Result for genesis: also exposes the resulting `category_id` (the txid
// of the spent output-0 input that anchors the new token category).
struct KD_API token_genesis_result {
    chain::transaction transaction;
    hash_digest category_id;
    std::vector<uint32_t> signing_indices;
};

// Result for mint: also exposes which outputs hold the freshly minted
// NFTs so the caller can locate them without parsing the transaction.
struct KD_API token_mint_result {
    chain::transaction transaction;
    std::vector<uint32_t> signing_indices;
    std::vector<uint32_t> minted_output_indices;
};


// ---------------------------------------------------------------------------
// NFT specs (parameter payloads)
// ---------------------------------------------------------------------------

// Describes an NFT to create (genesis) or to use for identifying an
// existing NFT (transfer).
struct KD_API nft_spec {
    chain::capability_t capability;
    data_chunk commitment;
};

// Describes one NFT to mint from an existing minting NFT.
struct KD_API nft_mint_request {
    payment_address destination;
    data_chunk commitment;
    chain::capability_t capability = chain::capability_t::none;
    uint64_t satoshis = 1000;
};


// ---------------------------------------------------------------------------
// prepare_genesis_utxo
//
// Creates a transaction that produces a UTXO at output index 0. This
// output is the only legal input for a subsequent token genesis (the
// CashTokens CHIP mandates that the genesis input must spend output #0 of
// its parent transaction). Useful when no suitable output-0 UTXO is
// available in the wallet.
// ---------------------------------------------------------------------------

struct KD_API prepare_genesis_params {
    chain::utxo utxo;                // any spendable UTXO
    payment_address destination;     // recipient of the new output 0

    // Default sized to comfortably cover the fee of the subsequent
    // genesis transaction (1 genesis input + 1 token output + 1 BCH
    // change output ≈ 340 sats at 1 sat/byte) while staying small
    // enough to be a reasonable single-purpose carrier UTXO. Callers
    // that want a tighter dust-only output can lower this to the
    // token dust limit (~800 sats).
    uint64_t satoshis = 10000;
    std::optional<payment_address> change_address;
};

// The resulting transaction produces a UTXO at output index 0 that is
// the genesis input for `create_token_genesis`. The txid is only known
// once the transaction is signed, so the caller computes the outpoint
// as `{signed_tx.hash(), 0}` after signing.
struct KD_API prepare_genesis_result {
    chain::transaction transaction;
    std::vector<uint32_t> signing_indices;
};

KD_API
std::expected<prepare_genesis_result, std::error_code>
prepare_genesis_utxo(prepare_genesis_params const& params);


// ---------------------------------------------------------------------------
// create_token_genesis
//
// Creates a new CashToken category by spending `genesis_utxo` (which MUST
// have `outpoint.index() == 0`). The category ID equals the txid of the
// input's parent transaction.
//
// At least one of `ft_amount` or `nft` must be provided.
//   - `ft_amount` only   →  pure fungible token (fixed supply)
//   - `nft` only         →  pure NFT (single NFT of the category)
//   - both               →  combined output carrying FT supply + NFT
//                            (typical for minting NFT + FT supply in one
//                            genesis)
// ---------------------------------------------------------------------------

struct KD_API token_genesis_params {
    chain::utxo genesis_utxo;             // outpoint.index() MUST be 0
    payment_address destination;

    std::optional<uint64_t> ft_amount;
    std::optional<nft_spec> nft;

    uint64_t satoshis = 1000;
    std::vector<chain::utxo> fee_utxos;   // used to cover miner fees
    std::optional<payment_address> change_address;

    // Active script flags for commitment-size validation (see header comment
    // on `max_op_return_payload_size` / `max_token_commitment_length`).
    script_flags_t script_flags;
};

KD_API
std::expected<token_genesis_result, std::error_code>
create_token_genesis(token_genesis_params const& params);


// ---------------------------------------------------------------------------
// create_token_mint
//
// Mints one or more new NFTs from an existing minting NFT (capability ==
// minting). All created NFTs share the category of `minting_utxo`. The
// minting NFT is preserved in output 0 (its commitment may be advanced by
// setting `new_minting_commitment`, which is typical when the commitment
// is used as an on-chain counter). Subsequent outputs (1..N) carry the
// newly minted NFTs in the order given by `nfts`.
// ---------------------------------------------------------------------------

struct KD_API token_mint_params {
    chain::utxo minting_utxo;               // must hold an NFT with capability == minting

    std::vector<nft_mint_request> nfts;

    // If present, the preserved minting NFT's commitment will be replaced
    // by this value in output 0. Useful for on-chain counters.
    std::optional<data_chunk> new_minting_commitment;

    // Where the preserved minting NFT is sent. Required: `chain::utxo`
    // does not expose the address of its source output, so the builder
    // cannot infer a safe default. Rather than silently fall back to
    // `change_address` (which is semantically "leftover BCH"), the call
    // is rejected if this is unset — the minting NFT is the minting
    // capability for the category and must be routed deliberately.
    std::optional<payment_address> minting_destination;

    std::vector<chain::utxo> fee_utxos;
    std::optional<payment_address> change_address;

    // Active script flags for commitment-size validation.
    script_flags_t script_flags;
};

KD_API
std::expected<token_mint_result, std::error_code>
create_token_mint(token_mint_params const& params);


// ---------------------------------------------------------------------------
// create_token_transfer
//
// Sends either fungible tokens or a specific NFT to `destination`.
// Exactly one of `ft_amount` or `nft` must be set.
//
// For FT: `token_utxos` should carry the target fungible category; excess
// tokens are sent to `token_change_address` (defaults to the maker's
// address inferred from inputs).
// For NFT: `nft.commitment` identifies which NFT to send (among the
// provided `token_utxos`).
// ---------------------------------------------------------------------------

struct KD_API token_transfer_params {
    std::vector<chain::utxo> token_utxos;
    payment_address destination;

    std::optional<uint64_t> ft_amount;
    std::optional<nft_spec> nft;

    std::vector<chain::utxo> fee_utxos;
    std::optional<payment_address> token_change_address;
    std::optional<payment_address> bch_change_address;
    uint64_t satoshis = 1000;
};

KD_API
std::expected<token_tx_result, std::error_code>
create_token_transfer(token_transfer_params const& params);


// ---------------------------------------------------------------------------
// create_token_burn
//
// Burns tokens by consuming a token UTXO and NOT recreating an output
// that carries the tokens. Any remaining tokens (if only a partial burn
// is requested via `burn_ft_amount` < UTXO amount) go back to the
// destination as a token output; burned FT is removed from circulation.
//
// To burn only an NFT portion from a both-kinds UTXO, set `burn_nft =
// true`. To burn a fungible amount, set `burn_ft_amount`. An optional
// `message` is encoded as an OP_RETURN in the transaction.
// ---------------------------------------------------------------------------

struct KD_API token_burn_params {
    chain::utxo token_utxo;
    std::optional<uint64_t> burn_ft_amount;
    bool burn_nft = false;
    std::optional<std::string> message;     // attached as OP_RETURN

    payment_address destination;
    std::vector<chain::utxo> fee_utxos;
    std::optional<payment_address> change_address;
    uint64_t satoshis = 1000;
};

KD_API
std::expected<token_tx_result, std::error_code>
create_token_burn(token_burn_params const& params);


// ---------------------------------------------------------------------------
// Level 2: high-level convenience builders
// ---------------------------------------------------------------------------

// Create a fungible token with a fixed total supply.
// When `with_minting_nft` is true, the genesis also attaches a minting
// NFT (commitment = {0x00}) so the creator can mint NFTs of the same
// category later.
struct KD_API ft_params {
    chain::utxo genesis_utxo;
    payment_address destination;
    uint64_t total_supply;
    bool with_minting_nft = false;

    std::vector<chain::utxo> fee_utxos;
    std::optional<payment_address> change_address;

    // Active script flags for commitment-size validation (only relevant
    // when `with_minting_nft == true`).
    script_flags_t script_flags;
};

KD_API
std::expected<token_genesis_result, std::error_code>
create_ft(ft_params const& params);


// Create an NFT collection via a planned sequence of transactions:
//   1. genesis         → creates the minting NFT (+ optional FT supply)
//   2. mint-batch-N    → one TX per batch, each minting up to `batch_size`
//                        NFTs from the preserved minting NFT
//   3. burn-minting    → optional final TX that burns the minting NFT to
//                        cap the supply
//
// The caller signs and broadcasts each step in order. Each step's first
// input consumes the minting NFT from the previous step's first output.
struct KD_API nft_collection_item {
    data_chunk commitment;
    std::optional<payment_address> destination;    // default: creator_address
};

struct KD_API nft_collection_params {
    chain::utxo genesis_utxo;
    std::vector<nft_collection_item> nfts;
    payment_address creator_address;
    bool keep_minting_token = false;              // false → burn at end
    std::optional<uint64_t> ft_amount;            // optional FT side-supply
    std::vector<chain::utxo> fee_utxos;
    std::optional<payment_address> change_address;
    size_t batch_size = 500;                       // NFTs per mint TX; must be > 0

    // Active script flags for commitment-size validation.
    script_flags_t script_flags;
};

// A batch is a ready-to-execute call-site for `create_token_mint`. The
// caller iterates over `batches`, feeding each one plus the current
// minting UTXO into `create_token_mint`, signs the resulting transaction
// and broadcasts it before moving to the next batch.
//
// The collection builder does NOT return signed transactions past the
// genesis because each subsequent TX's outpoints depend on the txid of
// the previous TX, which is only known after signing. The caller is
// responsible for threading the output-0 of each signed TX into the
// next call.
struct KD_API nft_collection_batch {
    std::vector<nft_mint_request> mint_requests;
};

struct KD_API nft_collection_result {
    // First transaction of the plan: the genesis that creates the
    // minting NFT (and optional FT side-supply). Sign this and
    // broadcast it; then take output 0 of the signed TX as the
    // `minting_utxo` input for the first batch.
    chain::transaction genesis_transaction;
    std::vector<uint32_t> genesis_signing_indices;

    // The category ID assigned to the collection (= txid of the input
    // that anchors the genesis, i.e. `genesis_utxo.point().hash()`).
    hash_digest category_id;

    // Mint requests grouped into batches of at most `batch_size` NFTs.
    // One `create_token_mint` call per batch.
    std::vector<nft_collection_batch> batches;

    // Whether the caller should burn the minting NFT after the last
    // batch (mirrors `keep_minting_token == false`).
    bool final_burn = false;
};

KD_API
std::expected<nft_collection_result, std::error_code>
create_nft_collection(nft_collection_params const& params);


} // namespace kth::domain::wallet::cashtoken

#endif // KTH_DOMAIN_WALLET_CASHTOKEN_MINTING_HPP
