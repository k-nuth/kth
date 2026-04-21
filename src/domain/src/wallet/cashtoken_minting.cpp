// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/wallet/cashtoken_minting.hpp>

#include <algorithm>
#include <utility>

#include <kth/domain/chain/input.hpp>
#include <kth/domain/chain/output.hpp>
#include <kth/domain/chain/output_point.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/token_data.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/wallet/coin_selection.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/machine/number.hpp>

namespace kth::domain::wallet::cashtoken {

// ---------------------------------------------------------------------------
// Commitment helpers
// ---------------------------------------------------------------------------

data_chunk encode_nft_number(int64_t value) {
    auto const n = infrastructure::machine::number::from_int(value);
    if ( ! n.has_value()) {
        return data_chunk{};
    }
    return n->data();
}


// ---------------------------------------------------------------------------
// Output factories
// ---------------------------------------------------------------------------

namespace {

// Build a P2PKH locking script for `addr`. CashTokens are carried in the
// token_data payload of the output, independent of the script.
chain::script p2pkh_script(payment_address const& addr) {
    return chain::script{chain::script::to_pay_public_key_hash_pattern(addr.hash20())};
}

// Reject any token-bearing UTXO that a caller passed in as a fee input.
// Consuming it would burn the tokens permanently because the builders
// never re-emit them to a change output; surfacing a hard error is
// safer than silently destroying value.
bool all_fee_utxos_are_bch_only(std::vector<chain::utxo> const& fee_utxos) {
    for (auto const& u : fee_utxos) {
        if (u.token_data().has_value()) return false;
    }
    return true;
}

// Estimated serialised size of a P2PKH output that optionally carries a
// CashTokens payload. Plain P2PKH outputs use `approx_output_size` (34
// bytes); token-bearing outputs include a PREFIX_BYTE + the encoded
// token payload (category + bitfield + optional commitment + optional
// amount) before the P2PKH script, so the naive approximation can
// under-estimate the fee by up to ~170 bytes at the post-Leibniz cap.
size_t token_bearing_output_size(chain::token_data_opt const& td) {
    if ( ! td.has_value()) {
        return approx_output_size;
    }
    // 1 byte PREFIX + token encoding + 25-byte P2PKH script.
    size_t const p2pkh_script_size = 25;
    size_t const token_payload_size = chain::token::encoding::serialized_size(td.value());
    size_t const script_len = 1 + token_payload_size + p2pkh_script_size;
    size_t const script_varint = script_len < 253 ? 1 : 3;
    return 8 /* value */ + script_varint + script_len;
}

} // namespace

chain::output create_ft_output(
    payment_address const& destination,
    hash_digest const& category_id,
    uint64_t ft_amount,
    uint64_t satoshis
) {
    auto token = chain::make_fungible(category_id, ft_amount);
    return chain::output{satoshis, p2pkh_script(destination), chain::token_data_opt{std::move(token)}};
}

chain::output create_nft_output(
    payment_address const& destination,
    hash_digest const& category_id,
    chain::capability_t capability,
    data_chunk const& commitment,
    uint64_t satoshis
) {
    auto token = chain::make_non_fungible(category_id, capability, commitment);
    return chain::output{satoshis, p2pkh_script(destination), chain::token_data_opt{std::move(token)}};
}

chain::output create_combined_token_output(
    payment_address const& destination,
    hash_digest const& category_id,
    uint64_t ft_amount,
    chain::capability_t capability,
    data_chunk const& commitment,
    uint64_t satoshis
) {
    auto token = chain::make_both(category_id, ft_amount, capability, commitment);
    return chain::output{satoshis, p2pkh_script(destination), chain::token_data_opt{std::move(token)}};
}

std::expected<prepare_genesis_result, std::error_code>
prepare_genesis_utxo(prepare_genesis_params const& params) {
    if ( ! bool(params.destination)) {
        return std::unexpected(kth::error::operation_failed);
    }

    // An explicit `change_address`, if set, must be a valid address —
    // otherwise the optional BCH change output would be unspendable.
    if (params.change_address.has_value() && ! bool(*params.change_address)) {
        return std::unexpected(kth::error::invalid_change);
    }

    // The funding UTXO is spent for its BCH only; a token-bearing
    // input here would be consumed without re-emitting its tokens,
    // silently burning them.
    if (params.utxo.token_data().has_value()) {
        return std::unexpected(kth::error::token_invalid_category);
    }

    // Validate the requested output amount is above the BCH dust limit so
    // the resulting UTXO is relayable by the network.
    if (params.satoshis < bch_dust_limit) {
        return std::unexpected(kth::error::insufficient_amount);
    }
    if (params.utxo.amount() < params.satoshis) {
        return std::unexpected(kth::error::insufficient_amount);
    }

    // Estimate the transaction size with 1 input and either 1 or 2 outputs
    // (genesis output + optional change). The upper estimate is used so
    // the fee reserve covers the worst case; if no change is produced the
    // extra bytes simply become miner fee.
    uint64_t const estimated_size_with_change = base_tx_size
        + approx_input_size
        + 2 * approx_output_size;
    uint64_t const fee = estimated_size_with_change * sats_per_byte;

    // Calculate the change. If the remainder after fee would be dust, fold
    // it into the miner fee instead of producing an unspendable output.
    uint64_t const required = params.satoshis + fee;
    if (params.utxo.amount() < required) {
        // Retry the math assuming we omit the change output altogether.
        uint64_t const no_change_fee =
            (base_tx_size + approx_input_size + approx_output_size) * sats_per_byte;
        if (params.utxo.amount() < params.satoshis + no_change_fee) {
            return std::unexpected(kth::error::insufficient_amount);
        }
        // Proceed without a change output.
    }

    uint64_t const change_amount = params.utxo.amount() > required
        ? params.utxo.amount() - required
        : 0;
    bool const emit_change = change_amount >= bch_dust_limit;

    // Build the genesis output (output index 0). This is a plain P2PKH
    // output with no token payload; the token category will be materialised
    // later when this output is spent by `create_token_genesis`.
    chain::output::list outputs;
    outputs.reserve(emit_change ? 2 : 1);
    outputs.emplace_back(
        params.satoshis,
        chain::script{chain::script::to_pay_public_key_hash_pattern(params.destination.hash20())},
        chain::token_data_opt{}
    );

    // Optional change output returned to either an explicitly provided
    // address or (as a fallback) the destination itself. The same address
    // is safe because `prepare_genesis_utxo` only targets single-wallet
    // flows — the caller can always override via `change_address`.
    if (emit_change) {
        auto const& change_addr = params.change_address.value_or(params.destination);
        outputs.emplace_back(
            change_amount,
            chain::script{chain::script::to_pay_public_key_hash_pattern(change_addr.hash20())},
            chain::token_data_opt{}
        );
    }

    // Build the single input referencing the provided UTXO. The unlocking
    // script is left empty: the caller signs the returned transaction
    // using the index reported in `signing_indices`.
    chain::input::list inputs;
    inputs.emplace_back(
        params.utxo.point(),
        chain::script{},
        0xffffffff        // default sequence
    );

    chain::transaction tx(
        /*version=*/2,
        /*locktime=*/0,
        std::move(inputs),
        std::move(outputs)
    );

    return prepare_genesis_result{
        std::move(tx),
        std::vector<uint32_t>{0}
    };
}

std::expected<token_genesis_result, std::error_code>
create_token_genesis(token_genesis_params const& params) {
    // Destination address must be valid — silently building a TX with a
    // default-constructed address would produce an unspendable output.
    if ( ! bool(params.destination)) {
        return std::unexpected(kth::error::operation_failed);
    }

    // An explicit `change_address`, if set, must also be valid.
    if (params.change_address.has_value() && ! bool(*params.change_address)) {
        return std::unexpected(kth::error::invalid_change);
    }

    // CashTokens CHIP: the token genesis input MUST spend output index 0
    // of its parent transaction. The resulting category ID equals the
    // hash of that parent transaction.
    if (params.genesis_utxo.point().index() != 0) {
        return std::unexpected(kth::error::token_invalid_category);
    }

    // The genesis input is spent for its BCH only; a token-bearing
    // input here would burn those existing tokens (the tx re-emits a
    // brand-new category on output 0).
    if (params.genesis_utxo.token_data().has_value()) {
        return std::unexpected(kth::error::token_invalid_category);
    }

    // Reject out-of-range NFT capability bytes; `capability_t` can hold
    // any `uint8_t` via casts, but the wire encoding only accepts
    // `none`/`mut`/`minting`.
    if (params.nft.has_value()
        && ! chain::is_valid_capability(params.nft->capability)) {
        return std::unexpected(kth::error::operation_failed);
    }

    // At least one payload kind (fungible or non-fungible) must be present
    // — otherwise this would be a no-op genesis.
    if ( ! params.ft_amount.has_value() && ! params.nft.has_value()) {
        return std::unexpected(kth::error::operation_failed);
    }

    // Validate fungible amount: the CashTokens spec caps supply at
    // 2^63 − 1 and requires strictly positive amounts (zero is the
    // "no fungible payload" sentinel).
    if (params.ft_amount.has_value()) {
        uint64_t const ft = *params.ft_amount;
        if (ft == 0 || ft > static_cast<uint64_t>(INT64_MAX)) {
            return std::unexpected(kth::error::token_amount_overflow);
        }
    }

    // Validate NFT commitment size against the cap that applies under
    // the active script flags. Capability byte legality is checked
    // implicitly by the encoding step via `is_valid_capability`.
    size_t const commitment_cap = chain::max_token_commitment_length(params.script_flags);
    if (params.nft.has_value() && params.nft->commitment.size() > commitment_cap) {
        return std::unexpected(kth::error::token_commitment_oversized);
    }

    // Fee UTXOs must be BCH-only — silently burning CashTokens fed in
    // as fees would be a catastrophic accounting bug.
    if ( ! all_fee_utxos_are_bch_only(params.fee_utxos)) {
        return std::unexpected(kth::error::token_invalid_category);
    }

    // Build the token payload (variant) according to which fields the
    // caller populated.
    hash_digest const category_id = params.genesis_utxo.point().hash();
    chain::token_data_t token_data = [&] {
        if (params.ft_amount.has_value() && params.nft.has_value()) {
            return chain::make_both(
                category_id,
                *params.ft_amount,
                params.nft->capability,
                params.nft->commitment
            );
        }
        if (params.ft_amount.has_value()) {
            return chain::make_fungible(category_id, *params.ft_amount);
        }
        return chain::make_non_fungible(
            category_id,
            params.nft->capability,
            params.nft->commitment
        );
    }();

    // Enforce the BCH dust policy on the token-carrying output.
    if (params.satoshis < token_dust_limit) {
        return std::unexpected(kth::error::insufficient_amount);
    }

    // Collect the full set of inputs. The genesis input is placed first
    // (index 0) for clarity and to match the convention used by the
    // mint/burn helpers below.
    std::vector<chain::utxo> all_inputs;
    all_inputs.reserve(1 + params.fee_utxos.size());
    all_inputs.push_back(params.genesis_utxo);
    for (auto const& u : params.fee_utxos) {
        all_inputs.push_back(u);
    }

    uint64_t total_input_bch = 0;
    for (auto const& u : all_inputs) {
        total_input_bch += u.amount();
    }

    // Fee estimate for the "with change" case (2 outputs). The optimistic
    // no-change path is handled below once the actual change amount is
    // known to be zero or dust. The token-bearing output 0 is sized
    // accurately via `token_bearing_output_size` — the token prefix +
    // category + commitment can add up to ~170 bytes over the naive
    // P2PKH approximation.
    uint64_t const n_inputs = all_inputs.size();
    uint64_t const token_output_size = token_bearing_output_size(
        chain::token_data_opt{token_data}
    );
    uint64_t const estimated_size_with_change = base_tx_size
        + n_inputs * approx_input_size
        + token_output_size
        + approx_output_size;
    uint64_t const estimated_size_no_change = base_tx_size
        + n_inputs * approx_input_size
        + token_output_size;

    uint64_t const fee_with_change = estimated_size_with_change * sats_per_byte;
    uint64_t const fee_no_change = estimated_size_no_change * sats_per_byte;

    uint64_t const required_with_change = params.satoshis + fee_with_change;

    if (total_input_bch < params.satoshis + fee_no_change) {
        return std::unexpected(kth::error::insufficient_amount);
    }

    uint64_t const change_amount = total_input_bch > required_with_change
        ? total_input_bch - required_with_change
        : 0;
    bool const emit_change = change_amount >= bch_dust_limit;

    // Assemble outputs: output 0 carries the genesis token payload;
    // output 1 (optional) returns BCH change to the caller-chosen
    // address (or the destination, used as a safe default).
    chain::output::list outputs;
    outputs.reserve(emit_change ? 2 : 1);
    outputs.emplace_back(
        params.satoshis,
        chain::script{chain::script::to_pay_public_key_hash_pattern(params.destination.hash20())},
        chain::token_data_opt{std::move(token_data)}
    );

    if (emit_change) {
        auto const& change_addr = params.change_address.value_or(params.destination);
        outputs.emplace_back(
            change_amount,
            chain::script{chain::script::to_pay_public_key_hash_pattern(change_addr.hash20())},
            chain::token_data_opt{}
        );
    }

    // Build the input list. All inputs start with an empty unlocking
    // script; the caller signs each one using the indices returned in
    // `signing_indices`.
    chain::input::list inputs;
    inputs.reserve(all_inputs.size());
    std::vector<uint32_t> signing_indices;
    signing_indices.reserve(all_inputs.size());
    for (uint32_t i = 0; i < all_inputs.size(); ++i) {
        inputs.emplace_back(all_inputs[i].point(), chain::script{}, 0xffffffff);
        signing_indices.push_back(i);
    }

    chain::transaction tx(
        /*version=*/2,
        /*locktime=*/0,
        std::move(inputs),
        std::move(outputs)
    );

    return token_genesis_result{
        std::move(tx),
        category_id,
        std::move(signing_indices)
    };
}

std::expected<token_mint_result, std::error_code>
create_token_mint(token_mint_params const& params) {
    // Surface the most-actionable caller mistake first: forgetting to
    // list any NFTs to mint.
    if (params.nfts.empty()) {
        return std::unexpected(kth::error::operation_failed);
    }

    // Destinations (one per requested NFT) must all be valid addresses.
    for (auto const& req : params.nfts) {
        if ( ! bool(req.destination)) {
            return std::unexpected(kth::error::operation_failed);
        }
    }

    // Each request's capability byte must be one of the three valid
    // values (none/mut/minting). `capability_t` can hold any `uint8_t`
    // via casts, so this is the first line of defence before the
    // output encoder, which only reinterprets the byte.
    for (auto const& req : params.nfts) {
        if ( ! chain::is_valid_capability(req.capability)) {
            return std::unexpected(kth::error::operation_failed);
        }
    }

    // An explicit `change_address`, if set, must be a valid address.
    if (params.change_address.has_value() && ! bool(*params.change_address)) {
        return std::unexpected(kth::error::invalid_change);
    }

    // Validate each NFT commitment against the cap that applies under
    // the active script flags.
    size_t const commitment_cap = chain::max_token_commitment_length(params.script_flags);
    for (auto const& req : params.nfts) {
        if (req.commitment.size() > commitment_cap) {
            return std::unexpected(kth::error::token_commitment_oversized);
        }
    }

    // `new_minting_commitment` is the preserved minting NFT's commitment;
    // it must also respect the same cap.
    if (params.new_minting_commitment.has_value()
        && params.new_minting_commitment->size() > commitment_cap) {
        return std::unexpected(kth::error::token_commitment_oversized);
    }

    // Fee UTXOs must be BCH-only — feeding token UTXOs here would burn
    // those tokens silently.
    if ( ! all_fee_utxos_are_bch_only(params.fee_utxos)) {
        return std::unexpected(kth::error::token_invalid_category);
    }

    // The source UTXO must carry a minting-capability NFT. This is what
    // the CashTokens CHIP requires to authorise creating new NFTs of the
    // same category.
    if ( ! params.minting_utxo.token_data().has_value()) {
        return std::unexpected(kth::error::token_invalid_category);
    }
    auto const& source_token = params.minting_utxo.token_data().value();
    if ( ! chain::is_minting_nft(source_token)) {
        return std::unexpected(kth::error::token_invalid_category);
    }

    // The minting NFT's BCH carrier must itself clear the token dust
    // floor; otherwise output 0 (the preserved minting NFT) would be
    // below dust and the transaction would not relay.
    if (params.minting_utxo.amount() < token_dust_limit) {
        return std::unexpected(kth::error::insufficient_amount);
    }

    // Preserve the minting NFT in output 0. If the caller provided a new
    // commitment (e.g. the category uses the commitment as a counter),
    // use it; otherwise keep the current one. The fungible side of a
    // both-kinds minting token is preserved as-is.
    hash_digest const category_id = source_token.id;
    chain::capability_t const preserved_capability = chain::get_nft(source_token).capability;
    data_chunk const preserved_commitment = params.new_minting_commitment.value_or(
        chain::get_nft(source_token).commitment
    );

    // `is_minting_nft` above already guarantees the source carries an
    // NFT with minting capability, so the variant is either
    // `non_fungible` or `both_kinds`. The only branch to decide here is
    // whether a fungible side exists that must be carried across.
    chain::token_data_t preserved_token;
    if (std::holds_alternative<chain::both_kinds>(source_token.data)) {
        // Use `get_amount` so a corrupted stored amount (> INT64_MAX)
        // surfaces as a negative value and is rejected instead of
        // silently re-emitted into the minting output.
        int64_t const preserved_ft = chain::get_amount(source_token);
        if (preserved_ft < 0) {
            return std::unexpected(kth::error::token_amount_overflow);
        }
        preserved_token = chain::make_both(
            category_id,
            static_cast<uint64_t>(preserved_ft),
            preserved_capability,
            preserved_commitment
        );
    } else {
        preserved_token = chain::make_non_fungible(
            category_id,
            preserved_capability,
            preserved_commitment
        );
    }

    // Gather all inputs. The minting UTXO MUST be input 0 so that the
    // preserved minting token re-emitted at output 0 keeps the convention
    // used elsewhere (consistent position between the consumed and the
    // re-created minting NFT).
    std::vector<chain::utxo> all_inputs;
    all_inputs.reserve(1 + params.fee_utxos.size());
    all_inputs.push_back(params.minting_utxo);
    for (auto const& u : params.fee_utxos) {
        all_inputs.push_back(u);
    }

    uint64_t total_input_bch = 0;
    for (auto const& u : all_inputs) {
        total_input_bch += u.amount();
    }

    // Outputs: 1 (preserved minting) + N (minted NFTs) + optional change.
    size_t const n_mint = params.nfts.size();

    // Preserved minting NFT output: reuse the already-materialised
    // `preserved_token` so the fee estimate matches what we actually
    // emit. Building a separate `make_non_fungible` sample here would
    // under-count when the minting UTXO is a `both_kinds` token — the
    // VLQ-encoded fungible amount (1–9 extra bytes) would be missed
    // and the estimated fee could fall below the relay floor.
    uint64_t const preserved_output_size = token_bearing_output_size(
        chain::token_data_opt{preserved_token}
    );

    // Each minted NFT output carries its own payload; sum them up.
    uint64_t minted_outputs_size = 0;
    for (auto const& req : params.nfts) {
        chain::token_data_t const sample = chain::make_non_fungible(
            category_id, req.capability, req.commitment
        );
        minted_outputs_size += token_bearing_output_size(
            chain::token_data_opt{sample}
        );
    }

    uint64_t const estimated_size_with_change = base_tx_size
        + all_inputs.size() * approx_input_size
        + preserved_output_size
        + minted_outputs_size
        + approx_output_size;                  // plain BCH change
    uint64_t const estimated_size_no_change = base_tx_size
        + all_inputs.size() * approx_input_size
        + preserved_output_size
        + minted_outputs_size;

    uint64_t const fee_with_change = estimated_size_with_change * sats_per_byte;
    uint64_t const fee_no_change = estimated_size_no_change * sats_per_byte;

    // The preserved minting output re-uses the incoming BCH value of the
    // minting UTXO (keeps the carrier dust stable); each minted NFT
    // output needs its own BCH value above the token dust limit.
    uint64_t const minting_carrier_bch = params.minting_utxo.amount();
    uint64_t total_mint_bch = 0;
    for (auto const& req : params.nfts) {
        if (req.satoshis < token_dust_limit) {
            return std::unexpected(kth::error::insufficient_amount);
        }
        total_mint_bch += req.satoshis;
    }

    uint64_t const required_base = minting_carrier_bch + total_mint_bch;
    if (total_input_bch < required_base + fee_no_change) {
        return std::unexpected(kth::error::insufficient_amount);
    }

    uint64_t const change_amount = total_input_bch > required_base + fee_with_change
        ? total_input_bch - required_base - fee_with_change
        : 0;
    bool const emit_change = change_amount >= bch_dust_limit;

    // The preserved minting NFT is valuable — whoever receives it can
    // continue minting the category. We deliberately refuse to fall
    // back to `change_address` (semantically "leftover BCH") because
    // quietly routing the minting capability to a BCH change wallet
    // would be a footgun. The caller MUST specify `minting_destination`.
    if ( ! params.minting_destination.has_value()) {
        return std::unexpected(kth::error::operation_failed);
    }
    payment_address const minting_dest = *params.minting_destination;
    if ( ! bool(minting_dest)) {
        return std::unexpected(kth::error::operation_failed);
    }

    // Assemble outputs: 1 preserved + N minted + optional BCH change.
    chain::output::list outputs;
    outputs.reserve(1 + n_mint + 1);

    // Output 0: preserved minting NFT.
    outputs.emplace_back(
        minting_carrier_bch,
        chain::script{chain::script::to_pay_public_key_hash_pattern(minting_dest.hash20())},
        chain::token_data_opt{std::move(preserved_token)}
    );

    // Outputs 1..N: minted NFTs. Each becomes an immutable (or whatever
    // capability the caller requested) NFT of the same category.
    std::vector<uint32_t> minted_output_indices;
    minted_output_indices.reserve(n_mint);
    for (uint32_t i = 0; i < n_mint; ++i) {
        auto const& req = params.nfts[i];
        outputs.emplace_back(
            req.satoshis,
            chain::script{chain::script::to_pay_public_key_hash_pattern(req.destination.hash20())},
            chain::token_data_opt{chain::make_non_fungible(
                category_id,
                req.capability,
                req.commitment
            )}
        );
        minted_output_indices.push_back(i + 1);
    }

    // Optional BCH change.
    if (emit_change) {
        auto const& change_addr = params.change_address.value_or(minting_dest);
        outputs.emplace_back(
            change_amount,
            chain::script{chain::script::to_pay_public_key_hash_pattern(change_addr.hash20())},
            chain::token_data_opt{}
        );
    }

    // Inputs.
    chain::input::list inputs;
    inputs.reserve(all_inputs.size());
    std::vector<uint32_t> signing_indices;
    signing_indices.reserve(all_inputs.size());
    for (uint32_t i = 0; i < all_inputs.size(); ++i) {
        inputs.emplace_back(all_inputs[i].point(), chain::script{}, 0xffffffff);
        signing_indices.push_back(i);
    }

    chain::transaction tx(
        /*version=*/2,
        /*locktime=*/0,
        std::move(inputs),
        std::move(outputs)
    );

    return token_mint_result{
        std::move(tx),
        std::move(signing_indices),
        std::move(minted_output_indices)
    };
}

std::expected<token_tx_result, std::error_code>
create_token_transfer(token_transfer_params const& params) {
    if ( ! bool(params.destination)) {
        return std::unexpected(kth::error::operation_failed);
    }

    // Exactly one of ft_amount / nft must be requested. Requesting both
    // would ambiguate whether the same output carries them together or
    // they split across outputs; requesting neither makes the call a
    // no-op.
    bool const want_ft = params.ft_amount.has_value();
    bool const want_nft = params.nft.has_value();
    if (want_ft == want_nft) {
        return std::unexpected(kth::error::operation_failed);
    }
    // An explicit zero fungible amount is treated as an out-of-range
    // request (matches `create_token_genesis`, which rejects `ft == 0`
    // with `token_amount_overflow`). This disambiguates "forgot what to
    // send" from "asked to send zero".
    if (want_ft && *params.ft_amount == 0) {
        return std::unexpected(kth::error::token_amount_overflow);
    }

    // The NFT spec, if provided, must carry a valid capability byte.
    if (want_nft && ! chain::is_valid_capability(params.nft->capability)) {
        return std::unexpected(kth::error::operation_failed);
    }

    if (params.token_utxos.empty()) {
        return std::unexpected(kth::error::empty_utxo_list);
    }

    // Fee UTXOs must be BCH-only — feeding token UTXOs here would burn
    // those tokens silently.
    if ( ! all_fee_utxos_are_bch_only(params.fee_utxos)) {
        return std::unexpected(kth::error::token_invalid_category);
    }

    // All provided token UTXOs are expected to share the same category —
    // the caller selects which inputs to spend. Picking up UTXOs from
    // other categories here would silently burn those tokens.
    hash_digest category_id{};
    bool category_set = false;
    for (auto const& u : params.token_utxos) {
        if ( ! u.token_data().has_value()) {
            return std::unexpected(kth::error::token_invalid_category);
        }
        if ( ! category_set) {
            category_id = u.token_data()->id;
            category_set = true;
        } else if (u.token_data()->id != category_id) {
            return std::unexpected(kth::error::token_invalid_category);
        }
    }

    // ---------------------------------------------------------------
    // Collect what the inputs bring: FT totals, NFTs carried, and BCH
    // available from the token UTXOs themselves (usually dust).
    // ---------------------------------------------------------------
    uint64_t total_ft_in = 0;
    std::vector<chain::non_fungible> carried_nfts;     // non-selected NFTs become change
    std::optional<size_t> selected_nft_idx;            // index in token_utxos
    uint64_t token_bch_in = 0;

    for (size_t i = 0; i < params.token_utxos.size(); ++i) {
        auto const& u = params.token_utxos[i];
        auto const& td = u.token_data().value();
        token_bch_in += u.amount();

        // `get_amount` returns `int64_t` on purpose: a corrupted UTXO
        // whose stored amount overflows `INT64_MAX` surfaces as a
        // negative number (consensus code relies on the same trick).
        // Treat that as a hard error instead of wrapping it back into
        // a huge unsigned total.
        int64_t const ft_here = chain::get_amount(td);
        if (ft_here < 0) {
            return std::unexpected(kth::error::token_amount_overflow);
        }
        total_ft_in += static_cast<uint64_t>(ft_here);

        if (chain::has_nft(td)) {
            auto const& nft = chain::get_nft(td);
            if (want_nft
                && ! selected_nft_idx.has_value()
                && nft.commitment == params.nft->commitment
                && nft.capability == params.nft->capability) {
                selected_nft_idx = i;
            } else {
                carried_nfts.push_back(nft);
            }
        }
    }

    if (want_nft && ! selected_nft_idx.has_value()) {
        return std::unexpected(kth::error::token_invalid_category);
    }
    if (want_ft && total_ft_in < *params.ft_amount) {
        return std::unexpected(kth::error::token_fungible_insufficient);
    }

    // Dust check on the destination output.
    if (params.satoshis < token_dust_limit) {
        return std::unexpected(kth::error::insufficient_amount);
    }

    // ---------------------------------------------------------------
    // Plan outputs
    //   0: destination (carries requested FT xor NFT)
    //   ... optional FT change
    //   ... optional carried NFTs change (one output per NFT)
    //   ... optional BCH change
    // ---------------------------------------------------------------
    uint64_t ft_change = 0;
    if (want_ft) {
        ft_change = total_ft_in - *params.ft_amount;
    } else {
        // NFT transfer: any FT in the inputs must also flow somewhere. By
        // convention we send the remaining FT to the token change
        // address (keeps the NFT output a pure single-NFT output).
        ft_change = total_ft_in;
    }

    bool const emit_ft_change = ft_change > 0;
    size_t const n_carried_nft_outs = carried_nfts.size();

    // Resolve change addresses. `token_change_address` is required when
    // there is any token change to emit. The resolved address must also
    // be valid — an optional wrapping a default-constructed address
    // would otherwise slip past `has_value()` and produce unspendable
    // token-carrying outputs, permanently burning those tokens.
    payment_address token_change_addr;
    if (emit_ft_change || n_carried_nft_outs > 0) {
        if ( ! params.token_change_address.has_value()) {
            return std::unexpected(kth::error::invalid_change);
        }
        token_change_addr = *params.token_change_address;
        if ( ! bool(token_change_addr)) {
            return std::unexpected(kth::error::invalid_change);
        }
    }

    // ---------------------------------------------------------------
    // Fees and BCH accounting
    // ---------------------------------------------------------------
    uint64_t total_input_bch = token_bch_in;
    for (auto const& u : params.fee_utxos) {
        total_input_bch += u.amount();
    }

    uint64_t const per_token_output_sats = token_dust_limit;
    uint64_t const outputs_bch_non_change =
        params.satoshis                                                    // destination
        + (emit_ft_change ? per_token_output_sats : 0)
        + n_carried_nft_outs * per_token_output_sats;

    size_t const n_inputs = params.token_utxos.size() + params.fee_utxos.size();

    // Estimate the actual serialised size of each token-bearing output
    // (destination + FT change + one per carried NFT). Using the naive
    // P2PKH size here would under-count by up to ~170 bytes per output
    // with a post-Leibniz commitment.
    uint64_t dest_output_size = 0;
    if (want_ft) {
        chain::token_data_t const sample =
            chain::make_fungible(category_id, *params.ft_amount);
        dest_output_size = token_bearing_output_size(chain::token_data_opt{sample});
    } else {
        chain::token_data_t const sample = chain::make_non_fungible(
            category_id, params.nft->capability, params.nft->commitment
        );
        dest_output_size = token_bearing_output_size(chain::token_data_opt{sample});
    }

    uint64_t ft_change_output_size = 0;
    if (emit_ft_change) {
        chain::token_data_t const sample = chain::make_fungible(category_id, ft_change);
        ft_change_output_size = token_bearing_output_size(chain::token_data_opt{sample});
    }

    uint64_t carried_nft_output_size = 0;
    for (auto const& nft : carried_nfts) {
        chain::token_data_t const sample = chain::make_non_fungible(
            category_id, nft.capability, nft.commitment
        );
        carried_nft_output_size += token_bearing_output_size(
            chain::token_data_opt{sample}
        );
    }

    uint64_t const size_no_change = base_tx_size
        + n_inputs * approx_input_size
        + dest_output_size
        + ft_change_output_size
        + carried_nft_output_size;
    uint64_t const size_with_change = size_no_change + approx_output_size;

    uint64_t const fee_no_change = size_no_change * sats_per_byte;
    uint64_t const fee_with_change = size_with_change * sats_per_byte;

    if (total_input_bch < outputs_bch_non_change + fee_no_change) {
        return std::unexpected(kth::error::insufficient_amount);
    }

    uint64_t const bch_change = total_input_bch > outputs_bch_non_change + fee_with_change
        ? total_input_bch - outputs_bch_non_change - fee_with_change
        : 0;
    bool const emit_bch_change = bch_change >= bch_dust_limit;

    // Prefer an explicit BCH change address; fall back to the token
    // change address if any, and ultimately to the destination as a
    // safe default so a valid transfer never fails on missing change
    // metadata. The resolved address must be valid for the same reason
    // explained above (avoid burning change to an unspendable output).
    payment_address bch_change_addr;
    if (emit_bch_change) {
        if (params.bch_change_address.has_value()) {
            bch_change_addr = *params.bch_change_address;
        } else if (params.token_change_address.has_value()) {
            bch_change_addr = *params.token_change_address;
        } else {
            bch_change_addr = params.destination;
        }
        if ( ! bool(bch_change_addr)) {
            return std::unexpected(kth::error::invalid_change);
        }
    }

    // ---------------------------------------------------------------
    // Assemble outputs
    // ---------------------------------------------------------------
    chain::output::list outputs;
    outputs.reserve(1 + (emit_ft_change ? 1 : 0) + carried_nfts.size() + (emit_bch_change ? 1 : 0));

    auto p2pkh = [](payment_address const& addr) {
        return chain::script{chain::script::to_pay_public_key_hash_pattern(addr.hash20())};
    };

    // Output 0: destination carries the requested token payload.
    if (want_ft) {
        outputs.emplace_back(
            params.satoshis,
            p2pkh(params.destination),
            chain::token_data_opt{chain::make_fungible(category_id, *params.ft_amount)}
        );
    } else {
        // NFT transfer: emit the matched NFT as an immutable pass-through
        // (capability preserved from the spec provided by the caller).
        outputs.emplace_back(
            params.satoshis,
            p2pkh(params.destination),
            chain::token_data_opt{chain::make_non_fungible(
                category_id,
                params.nft->capability,
                params.nft->commitment
            )}
        );
    }

    // FT change goes to the token change address, as a pure FT output.
    if (emit_ft_change) {
        outputs.emplace_back(
            per_token_output_sats,
            p2pkh(token_change_addr),
            chain::token_data_opt{chain::make_fungible(category_id, ft_change)}
        );
    }

    // Each carried NFT becomes its own output so the change outputs are
    // "clean" (one NFT per output, matching wallet conventions).
    for (auto const& nft : carried_nfts) {
        outputs.emplace_back(
            per_token_output_sats,
            p2pkh(token_change_addr),
            chain::token_data_opt{chain::make_non_fungible(
                category_id,
                nft.capability,
                nft.commitment
            )}
        );
    }

    // BCH change (plain, no token).
    if (emit_bch_change) {
        outputs.emplace_back(
            bch_change,
            p2pkh(bch_change_addr),
            chain::token_data_opt{}
        );
    }

    // ---------------------------------------------------------------
    // Inputs
    // ---------------------------------------------------------------
    chain::input::list inputs;
    inputs.reserve(n_inputs);
    std::vector<uint32_t> signing_indices;
    signing_indices.reserve(n_inputs);

    uint32_t idx = 0;
    for (auto const& u : params.token_utxos) {
        inputs.emplace_back(u.point(), chain::script{}, 0xffffffff);
        signing_indices.push_back(idx++);
    }
    for (auto const& u : params.fee_utxos) {
        inputs.emplace_back(u.point(), chain::script{}, 0xffffffff);
        signing_indices.push_back(idx++);
    }

    chain::transaction tx(
        /*version=*/2,
        /*locktime=*/0,
        std::move(inputs),
        std::move(outputs)
    );

    return token_tx_result{std::move(tx), std::move(signing_indices)};
}

std::expected<token_tx_result, std::error_code>
create_token_burn(token_burn_params const& params) {
    if ( ! bool(params.destination)) {
        return std::unexpected(kth::error::operation_failed);
    }

    // An explicit `change_address`, if set, must be a valid address —
    // otherwise `value_or(destination)` would happily emit BCH change
    // to an unspendable output.
    if (params.change_address.has_value() && ! bool(*params.change_address)) {
        return std::unexpected(kth::error::invalid_change);
    }

    if ( ! params.token_utxo.token_data().has_value()) {
        return std::unexpected(kth::error::token_invalid_category);
    }
    auto const& td = params.token_utxo.token_data().value();
    hash_digest const category_id = td.id;

    // Fee UTXOs must be BCH-only — feeding token UTXOs here would burn
    // those tokens silently.
    if ( ! all_fee_utxos_are_bch_only(params.fee_utxos)) {
        return std::unexpected(kth::error::token_invalid_category);
    }

    // Standard OP_RETURN relay policy caps the payload at 220 bytes;
    // larger messages produce non-standard transactions that nodes will
    // not relay, so reject them up front with a clear error instead of
    // handing the caller an unrelayable TX.
    if (params.message.has_value() && params.message->size() > max_op_return_payload_size) {
        return std::unexpected(kth::error::operation_failed);
    }

    // The caller must ask to burn something; a "burn zero FT and keep the
    // NFT" request would produce a plain BCH move that doesn't belong here.
    bool const has_burn_ft = params.burn_ft_amount.value_or(0) > 0;
    bool const has_burn_nft = params.burn_nft;
    if ( ! has_burn_ft && ! has_burn_nft) {
        return std::unexpected(kth::error::operation_failed);
    }

    // ---------------------------------------------------------------
    // Evaluate what the UTXO currently carries and what survives the burn.
    // ---------------------------------------------------------------
    // Reject a corrupted token UTXO up front — see the matching guard
    // in `create_token_transfer` for the rationale.
    int64_t const ft_here = chain::get_amount(td);
    if (ft_here < 0) {
        return std::unexpected(kth::error::token_amount_overflow);
    }
    uint64_t const current_ft = static_cast<uint64_t>(ft_here);
    bool const currently_has_nft = chain::has_nft(td);

    if (has_burn_ft && params.burn_ft_amount.value() > current_ft) {
        return std::unexpected(kth::error::token_fungible_insufficient);
    }
    if (has_burn_nft && ! currently_has_nft) {
        return std::unexpected(kth::error::operation_failed);
    }

    uint64_t const remaining_ft = current_ft - (has_burn_ft ? *params.burn_ft_amount : 0);
    bool const keep_nft = currently_has_nft && ! has_burn_nft;

    // ---------------------------------------------------------------
    // Assemble what becomes output 0: either a remaining-token output
    // (if anything of the token survives) or a plain BCH output. An
    // output that only carries the `satoshis` of the token UTXO itself
    // (with no token payload) is how the spec "destroys" tokens — by
    // simply not re-emitting them.
    // ---------------------------------------------------------------
    chain::output::list outputs;

    auto p2pkh = [](payment_address const& addr) {
        return chain::script{chain::script::to_pay_public_key_hash_pattern(addr.hash20())};
    };

    if (params.satoshis < (remaining_ft > 0 || keep_nft ? token_dust_limit : bch_dust_limit)) {
        return std::unexpected(kth::error::insufficient_amount);
    }

    chain::token_data_opt remaining_payload{};
    if (remaining_ft > 0 && keep_nft) {
        remaining_payload = chain::make_both(
            category_id,
            remaining_ft,
            chain::get_nft(td).capability,
            chain::get_nft(td).commitment
        );
    } else if (remaining_ft > 0) {
        remaining_payload = chain::make_fungible(category_id, remaining_ft);
    } else if (keep_nft) {
        remaining_payload = chain::make_non_fungible(
            category_id,
            chain::get_nft(td).capability,
            chain::get_nft(td).commitment
        );
    }

    // Snapshot the output-0 size before we move `remaining_payload`
    // into the transaction; the fee estimate below needs it.
    uint64_t const out0_size = token_bearing_output_size(remaining_payload);

    outputs.emplace_back(
        params.satoshis,
        p2pkh(params.destination),
        std::move(remaining_payload)
    );

    // ---------------------------------------------------------------
    // Optional OP_RETURN message. Attaches arbitrary caller data to the
    // TX (commonly used to record the reason for the burn). The data is
    // encoded as a standard null-data output with value 0.
    // ---------------------------------------------------------------
    bool const emit_op_return = params.message.has_value() && ! params.message->empty();
    if (emit_op_return) {
        data_chunk msg(params.message->begin(), params.message->end());
        outputs.emplace_back(
            0,
            chain::script{chain::script::to_null_data_pattern(msg)},
            chain::token_data_opt{}
        );
    }

    // ---------------------------------------------------------------
    // BCH accounting + change
    // ---------------------------------------------------------------
    uint64_t total_input_bch = params.token_utxo.amount();
    for (auto const& u : params.fee_utxos) {
        total_input_bch += u.amount();
    }

    size_t const n_inputs = 1 + params.fee_utxos.size();

    // Compute the OP_RETURN output's actual serialised size. The P2PKH
    // approximation (34 bytes) understates it materially when the
    // message is longer than ~23 bytes; at the 220-byte OP_RETURN limit
    // the difference is ~200 sats per transaction, which can push the
    // fee below the relay floor.
    //   serialized output = 8 (value) + varint(scriptlen) + scriptlen
    //   null-data script  = OP_RETURN (1) + push-prefix (1 for <=75 B,
    //                       2 for 76..255 B) + payload bytes
    size_t op_return_output_size = 0;
    if (emit_op_return) {
        size_t const payload = params.message->size();
        size_t const push_prefix = payload <= 75 ? 1 : 2;   // matches script::to_null_data_pattern
        size_t const script_len = 1 + push_prefix + payload;
        size_t const script_varint = script_len < 253 ? 1 : 3;
        op_return_output_size = 8 + script_varint + script_len;
    }

    // Output 0 carries either the remaining token payload or plain BCH.
    // `out0_size` was snapshot above before moving `remaining_payload`
    // into the transaction; use it here so a partial burn of a
    // post-Leibniz commitment doesn't under-pay the fee.
    uint64_t const size_no_change = base_tx_size
        + n_inputs * approx_input_size
        + out0_size
        + op_return_output_size;
    uint64_t const size_with_change = size_no_change + approx_output_size;

    uint64_t const fee_no_change = size_no_change * sats_per_byte;
    uint64_t const fee_with_change = size_with_change * sats_per_byte;

    if (total_input_bch < params.satoshis + fee_no_change) {
        return std::unexpected(kth::error::insufficient_amount);
    }

    uint64_t const bch_change = total_input_bch > params.satoshis + fee_with_change
        ? total_input_bch - params.satoshis - fee_with_change
        : 0;
    bool const emit_change = bch_change >= bch_dust_limit;

    if (emit_change) {
        auto const& change_addr = params.change_address.value_or(params.destination);
        outputs.emplace_back(
            bch_change,
            p2pkh(change_addr),
            chain::token_data_opt{}
        );
    }

    // ---------------------------------------------------------------
    // Inputs
    // ---------------------------------------------------------------
    chain::input::list inputs;
    inputs.reserve(n_inputs);
    std::vector<uint32_t> signing_indices;
    signing_indices.reserve(n_inputs);

    inputs.emplace_back(params.token_utxo.point(), chain::script{}, 0xffffffff);
    signing_indices.push_back(0);

    uint32_t idx = 1;
    for (auto const& u : params.fee_utxos) {
        inputs.emplace_back(u.point(), chain::script{}, 0xffffffff);
        signing_indices.push_back(idx++);
    }

    chain::transaction tx(
        /*version=*/2,
        /*locktime=*/0,
        std::move(inputs),
        std::move(outputs)
    );

    return token_tx_result{std::move(tx), std::move(signing_indices)};
}

std::expected<token_genesis_result, std::error_code>
create_ft(ft_params const& params) {
    // Thin adapter over `create_token_genesis`: convenience entry point
    // for the two most common flows — creating a pure FT with fixed
    // supply, or a FT paired with a minting NFT so the creator can
    // later mint NFTs of the same category.
    token_genesis_params gp{};
    gp.genesis_utxo = params.genesis_utxo;
    gp.destination = params.destination;
    gp.ft_amount = params.total_supply;
    if (params.with_minting_nft) {
        gp.nft = nft_spec{
            chain::capability_t::minting,
            data_chunk{0x00}
        };
    }
    gp.fee_utxos = params.fee_utxos;
    gp.change_address = params.change_address;
    gp.script_flags = params.script_flags;
    return create_token_genesis(gp);
}

std::expected<nft_collection_result, std::error_code>
create_nft_collection(nft_collection_params const& params) {
    if (params.nfts.empty()) {
        return std::unexpected(kth::error::operation_failed);
    }
    if (params.batch_size == 0) {
        return std::unexpected(kth::error::operation_failed);
    }

    // Validate every NFT commitment and destination up front — under
    // the active script flags' cap — so a later batch failure doesn't
    // leave the caller with a half-executed plan on-chain. Destinations
    // are checked for the same reason: an explicit optional wrapping a
    // default-constructed address would pass `has_value()`, survive
    // `value_or`, and only blow up inside `create_token_mint` for that
    // batch — after earlier batches may have been broadcast.
    size_t const commitment_cap = chain::max_token_commitment_length(params.script_flags);
    for (auto const& item : params.nfts) {
        if (item.commitment.size() > commitment_cap) {
            return std::unexpected(kth::error::token_commitment_oversized);
        }
        if (item.destination.has_value() && ! bool(*item.destination)) {
            return std::unexpected(kth::error::operation_failed);
        }
    }
    // The fallback for per-item destinations is `creator_address`; if
    // any item omits its destination, `creator_address` must be valid
    // so it produces spendable mint outputs. (Also re-validated by
    // `create_token_genesis` below for its own use.)
    if ( ! bool(params.creator_address)) {
        return std::unexpected(kth::error::operation_failed);
    }

    // Step 1: build the genesis TX. The minting NFT starts with
    // commitment `{0x00}` which doubles as a "next NFT index = 0"
    // counter — callers who want a meaningful counter can reinterpret
    // the commitment in subsequent mints via `new_minting_commitment`.
    token_genesis_params gp{};
    gp.genesis_utxo = params.genesis_utxo;
    gp.destination = params.creator_address;
    gp.ft_amount = params.ft_amount;
    gp.nft = nft_spec{
        chain::capability_t::minting,
        data_chunk{0x00}
    };
    gp.fee_utxos = params.fee_utxos;
    gp.change_address = params.change_address;
    gp.script_flags = params.script_flags;

    auto genesis = create_token_genesis(gp);
    if ( ! genesis.has_value()) {
        return std::unexpected(genesis.error());
    }

    // Step 2: partition the NFT list into batches. Each batch becomes
    // one `create_token_mint` call-site; the caller iterates in order.
    std::vector<nft_collection_batch> batches;
    batches.reserve((params.nfts.size() + params.batch_size - 1) / params.batch_size);

    for (size_t i = 0; i < params.nfts.size(); i += params.batch_size) {
        size_t const end = std::min(i + params.batch_size, params.nfts.size());
        nft_collection_batch batch{};
        batch.mint_requests.reserve(end - i);
        for (size_t j = i; j < end; ++j) {
            nft_mint_request req{};
            req.destination = params.nfts[j].destination.value_or(params.creator_address);
            req.commitment = params.nfts[j].commitment;
            req.capability = chain::capability_t::none;   // minted NFTs are immutable
            req.satoshis = token_dust_limit;
            batch.mint_requests.push_back(std::move(req));
        }
        batches.push_back(std::move(batch));
    }

    return nft_collection_result{
        std::move(genesis->transaction),
        std::move(genesis->signing_indices),
        genesis->category_id,
        std::move(batches),
        /*final_burn=*/ ! params.keep_minting_token
    };
}

} // namespace kth::domain::wallet::cashtoken
