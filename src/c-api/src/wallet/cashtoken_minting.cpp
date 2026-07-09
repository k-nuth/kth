// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/wallet/cashtoken_minting.h>

#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/chain/token_data.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/chain/utxo.hpp>
#include <kth/domain/wallet/cashtoken_minting.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/infrastructure/error.hpp>

namespace ct = kth::domain::wallet::cashtoken;

namespace {

// Shared helper to turn a (ptr, size) commitment pair into data_chunk.
//
// `(data == nullptr, n == 0)` is valid and means "empty commitment".
// `(data == nullptr, n >  0)` is a caller bug — silently folding it to
// an empty commitment would produce a valid-looking TX with the wrong
// NFT commitment, so we trip the precondition instead of dropping the
// bytes.
kth::data_chunk commitment_from(uint8_t const* data, kth_size_t n) {
    if (n == 0) return kth::data_chunk{};
    KTH_PRECONDITION(data != nullptr);
    return kth::data_chunk(data, data + kth::sz(n));
}

// Copies a `kth_utxo_list_const_t` handle into a fresh C++ vector.
// NULL is interpreted as "empty list" — the only way the C caller
// can express "no fee UTXOs" at this ABI.
std::vector<kth::domain::chain::utxo>
copy_utxo_list(kth_utxo_list_const_t list) {
    if (list == nullptr) return {};
    return kth::cpp_ref<std::vector<kth::domain::chain::utxo>>(list);
}

// Wraps a nullable address handle in `std::optional`. NULL → nullopt.
std::optional<kth::domain::wallet::payment_address>
optional_addr(kth_payment_address_const_t addr) {
    if (addr == nullptr) return std::nullopt;
    return kth::cpp_ref<kth::domain::wallet::payment_address>(addr);
}

// Wraps a nullable nft_spec handle in `std::optional`. NULL → nullopt.
std::optional<ct::nft_spec>
optional_nft_spec(kth_cashtoken_nft_spec_const_t spec) {
    if (spec == nullptr) return std::nullopt;
    return kth::cpp_ref<ct::nft_spec>(spec);
}

} // namespace

extern "C" {

// ---------------------------------------------------------------------------
// encode_nft_number
// ---------------------------------------------------------------------------

kth_error_code_t kth_wallet_cashtoken_encode_nft_number(
    int64_t value,
    uint8_t** out_data,
    kth_size_t* out_size
) {
    KTH_PRECONDITION(out_data != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto r = ct::encode_nft_number(value);
    if ( ! r.has_value()) return kth::to_c_err(r.error());
    *out_data = kth::create_c_array(*r, *out_size);
    return kth_ec_success;
}


// ---------------------------------------------------------------------------
// Output factories
// ---------------------------------------------------------------------------

kth_output_mut_t kth_wallet_cashtoken_create_ft_output(
    kth_payment_address_const_t destination,
    uint8_t const* category_id,
    uint64_t ft_amount,
    uint64_t satoshis
) {
    KTH_PRECONDITION(destination != nullptr);
    KTH_PRECONDITION(category_id != nullptr);
    auto const& dest_cpp = kth::cpp_ref<kth::domain::wallet::payment_address>(destination);
    auto const cat_cpp = kth::hash_to_cpp(category_id);
    return kth::leak(ct::create_ft_output(dest_cpp, cat_cpp, ft_amount, satoshis));
}

kth_output_mut_t kth_wallet_cashtoken_create_nft_output(
    kth_payment_address_const_t destination,
    uint8_t const* category_id,
    kth_token_capability_t capability,
    uint8_t const* commitment,
    kth_size_t commitment_n,
    uint64_t satoshis
) {
    KTH_PRECONDITION(destination != nullptr);
    KTH_PRECONDITION(category_id != nullptr);
    auto const& dest_cpp = kth::cpp_ref<kth::domain::wallet::payment_address>(destination);
    auto const cat_cpp = kth::hash_to_cpp(category_id);
    auto const cap_cpp = static_cast<kth::domain::chain::capability_t>(capability);
    return kth::leak(ct::create_nft_output(
        dest_cpp, cat_cpp, cap_cpp,
        commitment_from(commitment, commitment_n),
        satoshis));
}

kth_output_mut_t kth_wallet_cashtoken_create_combined_token_output(
    kth_payment_address_const_t destination,
    uint8_t const* category_id,
    uint64_t ft_amount,
    kth_token_capability_t capability,
    uint8_t const* commitment,
    kth_size_t commitment_n,
    uint64_t satoshis
) {
    KTH_PRECONDITION(destination != nullptr);
    KTH_PRECONDITION(category_id != nullptr);
    auto const& dest_cpp = kth::cpp_ref<kth::domain::wallet::payment_address>(destination);
    auto const cat_cpp = kth::hash_to_cpp(category_id);
    auto const cap_cpp = static_cast<kth::domain::chain::capability_t>(capability);
    return kth::leak(ct::create_combined_token_output(
        dest_cpp, cat_cpp, ft_amount, cap_cpp,
        commitment_from(commitment, commitment_n),
        satoshis));
}


// ---------------------------------------------------------------------------
// nft_spec
// ---------------------------------------------------------------------------

kth_cashtoken_nft_spec_mut_t kth_wallet_cashtoken_nft_spec_construct(
    kth_token_capability_t capability,
    uint8_t const* commitment,
    kth_size_t commitment_n
) {
    return kth::leak(ct::nft_spec{
        .capability = static_cast<kth::domain::chain::capability_t>(capability),
        .commitment = commitment_from(commitment, commitment_n),
    });
}

void kth_wallet_cashtoken_nft_spec_destruct(kth_cashtoken_nft_spec_mut_t self) {
    kth::del<ct::nft_spec>(self);
}


// ---------------------------------------------------------------------------
// nft_mint_request (single element + list)
// ---------------------------------------------------------------------------

kth_cashtoken_nft_mint_request_mut_t kth_wallet_cashtoken_nft_mint_request_construct(
    kth_payment_address_const_t destination,
    uint8_t const* commitment,
    kth_size_t commitment_n,
    kth_token_capability_t capability,
    uint64_t satoshis
) {
    KTH_PRECONDITION(destination != nullptr);
    return kth::leak(ct::nft_mint_request{
        .destination = kth::cpp_ref<kth::domain::wallet::payment_address>(destination),
        .commitment = commitment_from(commitment, commitment_n),
        .capability = static_cast<kth::domain::chain::capability_t>(capability),
        .satoshis = satoshis,
    });
}

void kth_wallet_cashtoken_nft_mint_request_destruct(
    kth_cashtoken_nft_mint_request_mut_t self
) {
    kth::del<ct::nft_mint_request>(self);
}

kth_cashtoken_nft_mint_request_list_mut_t
kth_wallet_cashtoken_nft_mint_request_list_construct_default(void) {
    return kth::leak<std::vector<ct::nft_mint_request>>();
}

void kth_wallet_cashtoken_nft_mint_request_list_push_back(
    kth_cashtoken_nft_mint_request_list_mut_t list,
    kth_cashtoken_nft_mint_request_const_t elem
) {
    KTH_PRECONDITION(list != nullptr);
    KTH_PRECONDITION(elem != nullptr);
    auto& l = kth::cpp_ref<std::vector<ct::nft_mint_request>>(list);
    l.push_back(kth::cpp_ref<ct::nft_mint_request>(elem));
}

void kth_wallet_cashtoken_nft_mint_request_list_destruct(
    kth_cashtoken_nft_mint_request_list_mut_t list
) {
    kth::del<std::vector<ct::nft_mint_request>>(list);
}

kth_size_t kth_wallet_cashtoken_nft_mint_request_list_count(
    kth_cashtoken_nft_mint_request_list_const_t list
) {
    if (list == nullptr) return 0;
    return kth::cpp_ref<std::vector<ct::nft_mint_request>>(list).size();
}

kth_cashtoken_nft_mint_request_const_t kth_wallet_cashtoken_nft_mint_request_list_nth(
    kth_cashtoken_nft_mint_request_list_const_t list, kth_size_t index
) {
    KTH_PRECONDITION(list != nullptr);
    auto const& l = kth::cpp_ref<std::vector<ct::nft_mint_request>>(list);
    KTH_PRECONDITION(kth::sz(index) < l.size());
    return &l[kth::sz(index)];
}


// ---------------------------------------------------------------------------
// nft_collection_item (single element + list)
// ---------------------------------------------------------------------------

kth_cashtoken_nft_collection_item_mut_t kth_wallet_cashtoken_nft_collection_item_construct(
    uint8_t const* commitment,
    kth_size_t commitment_n,
    kth_payment_address_const_t destination
) {
    return kth::leak(ct::nft_collection_item{
        .commitment = commitment_from(commitment, commitment_n),
        .destination = optional_addr(destination),
    });
}

void kth_wallet_cashtoken_nft_collection_item_destruct(
    kth_cashtoken_nft_collection_item_mut_t self
) {
    kth::del<ct::nft_collection_item>(self);
}

kth_cashtoken_nft_collection_item_list_mut_t
kth_wallet_cashtoken_nft_collection_item_list_construct_default(void) {
    return kth::leak<std::vector<ct::nft_collection_item>>();
}

void kth_wallet_cashtoken_nft_collection_item_list_push_back(
    kth_cashtoken_nft_collection_item_list_mut_t list,
    kth_cashtoken_nft_collection_item_const_t elem
) {
    KTH_PRECONDITION(list != nullptr);
    KTH_PRECONDITION(elem != nullptr);
    auto& l = kth::cpp_ref<std::vector<ct::nft_collection_item>>(list);
    l.push_back(kth::cpp_ref<ct::nft_collection_item>(elem));
}

void kth_wallet_cashtoken_nft_collection_item_list_destruct(
    kth_cashtoken_nft_collection_item_list_mut_t list
) {
    kth::del<std::vector<ct::nft_collection_item>>(list);
}

kth_size_t kth_wallet_cashtoken_nft_collection_item_list_count(
    kth_cashtoken_nft_collection_item_list_const_t list
) {
    if (list == nullptr) return 0;
    return kth::cpp_ref<std::vector<ct::nft_collection_item>>(list).size();
}

kth_cashtoken_nft_collection_item_const_t kth_wallet_cashtoken_nft_collection_item_list_nth(
    kth_cashtoken_nft_collection_item_list_const_t list, kth_size_t index
) {
    KTH_PRECONDITION(list != nullptr);
    auto const& l = kth::cpp_ref<std::vector<ct::nft_collection_item>>(list);
    KTH_PRECONDITION(kth::sz(index) < l.size());
    return &l[kth::sz(index)];
}


// ---------------------------------------------------------------------------
// prepare_genesis_utxo
// ---------------------------------------------------------------------------

kth_error_code_t kth_wallet_cashtoken_prepare_genesis_utxo(
    kth_utxo_const_t utxo,
    kth_payment_address_const_t destination,
    uint64_t satoshis,
    kth_payment_address_const_t change_address,
    KTH_OUT_OWNED kth_cashtoken_prepare_genesis_result_mut_t* out
) {
    KTH_PRECONDITION(utxo != nullptr);
    KTH_PRECONDITION(destination != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);

    auto r = ct::prepare_genesis_utxo(ct::prepare_genesis_params{
        .utxo = kth::cpp_ref<kth::domain::chain::utxo>(utxo),
        .destination = kth::cpp_ref<kth::domain::wallet::payment_address>(destination),
        .satoshis = satoshis,
        .change_address = optional_addr(change_address),
    });
    if ( ! r.has_value()) return kth::to_c_err(r.error());
    *out = kth::leak(std::move(*r));
    return kth_ec_success;
}

void kth_wallet_cashtoken_prepare_genesis_result_destruct(
    kth_cashtoken_prepare_genesis_result_mut_t self
) {
    kth::del<ct::prepare_genesis_result>(self);
}

kth_transaction_const_t kth_wallet_cashtoken_prepare_genesis_result_transaction(
    kth_cashtoken_prepare_genesis_result_const_t self
) {
    KTH_PRECONDITION(self != nullptr);
    return &kth::cpp_ref<ct::prepare_genesis_result>(self).transaction;
}

kth_size_t kth_wallet_cashtoken_prepare_genesis_result_signing_indices_count(
    kth_cashtoken_prepare_genesis_result_const_t self
) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<ct::prepare_genesis_result>(self).signing_indices.size();
}

uint32_t kth_wallet_cashtoken_prepare_genesis_result_signing_index_nth(
    kth_cashtoken_prepare_genesis_result_const_t self, kth_size_t index
) {
    KTH_PRECONDITION(self != nullptr);
    auto const& v = kth::cpp_ref<ct::prepare_genesis_result>(self).signing_indices;
    KTH_PRECONDITION(kth::sz(index) < v.size());
    return v[kth::sz(index)];
}


// ---------------------------------------------------------------------------
// create_token_genesis
// ---------------------------------------------------------------------------

kth_error_code_t kth_wallet_cashtoken_create_token_genesis(
    kth_utxo_const_t genesis_utxo,
    kth_payment_address_const_t destination,
    kth_bool_t has_ft_amount, uint64_t ft_amount,
    kth_cashtoken_nft_spec_const_t nft,
    uint64_t satoshis,
    kth_utxo_list_const_t fee_utxos,
    kth_payment_address_const_t change_address,
    uint64_t script_flags,
    KTH_OUT_OWNED kth_cashtoken_token_genesis_result_mut_t* out
) {
    KTH_PRECONDITION(genesis_utxo != nullptr);
    KTH_PRECONDITION(destination != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);

    auto r = ct::create_token_genesis(ct::token_genesis_params{
        .genesis_utxo = kth::cpp_ref<kth::domain::chain::utxo>(genesis_utxo),
        .destination = kth::cpp_ref<kth::domain::wallet::payment_address>(destination),
        .ft_amount = kth::int_to_bool(has_ft_amount) ? std::optional<uint64_t>(ft_amount) : std::nullopt,
        .nft = optional_nft_spec(nft),
        .satoshis = satoshis,
        .fee_utxos = copy_utxo_list(fee_utxos),
        .change_address = optional_addr(change_address),
        .script_flags = script_flags,
    });
    if ( ! r.has_value()) return kth::to_c_err(r.error());
    *out = kth::leak(std::move(*r));
    return kth_ec_success;
}

void kth_wallet_cashtoken_token_genesis_result_destruct(
    kth_cashtoken_token_genesis_result_mut_t self
) {
    kth::del<ct::token_genesis_result>(self);
}

kth_transaction_const_t kth_wallet_cashtoken_token_genesis_result_transaction(
    kth_cashtoken_token_genesis_result_const_t self
) {
    KTH_PRECONDITION(self != nullptr);
    return &kth::cpp_ref<ct::token_genesis_result>(self).transaction;
}

void kth_wallet_cashtoken_token_genesis_result_category_id(
    kth_cashtoken_token_genesis_result_const_t self, uint8_t* out_hash
) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_hash != nullptr);
    auto const& id = kth::cpp_ref<ct::token_genesis_result>(self).category_id;
    std::memcpy(out_hash, id.data(), id.size());
}

kth_size_t kth_wallet_cashtoken_token_genesis_result_signing_indices_count(
    kth_cashtoken_token_genesis_result_const_t self
) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<ct::token_genesis_result>(self).signing_indices.size();
}

uint32_t kth_wallet_cashtoken_token_genesis_result_signing_index_nth(
    kth_cashtoken_token_genesis_result_const_t self, kth_size_t index
) {
    KTH_PRECONDITION(self != nullptr);
    auto const& v = kth::cpp_ref<ct::token_genesis_result>(self).signing_indices;
    KTH_PRECONDITION(kth::sz(index) < v.size());
    return v[kth::sz(index)];
}


// ---------------------------------------------------------------------------
// create_token_mint
// ---------------------------------------------------------------------------

kth_error_code_t kth_wallet_cashtoken_create_token_mint(
    kth_utxo_const_t minting_utxo,
    kth_cashtoken_nft_mint_request_list_const_t nfts,
    uint8_t const* new_minting_commitment,
    kth_size_t new_minting_commitment_n,
    kth_payment_address_const_t minting_destination,
    kth_utxo_list_const_t fee_utxos,
    kth_payment_address_const_t change_address,
    uint64_t script_flags,
    KTH_OUT_OWNED kth_cashtoken_token_mint_result_mut_t* out
) {
    KTH_PRECONDITION(minting_utxo != nullptr);
    KTH_PRECONDITION(nfts != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);

    auto const new_commit_opt = new_minting_commitment == nullptr
        ? std::optional<kth::data_chunk>(std::nullopt)
        : std::optional<kth::data_chunk>(commitment_from(new_minting_commitment, new_minting_commitment_n));

    auto r = ct::create_token_mint(ct::token_mint_params{
        .minting_utxo = kth::cpp_ref<kth::domain::chain::utxo>(minting_utxo),
        .nfts = kth::cpp_ref<std::vector<ct::nft_mint_request>>(nfts),
        .new_minting_commitment = new_commit_opt,
        .minting_destination = optional_addr(minting_destination),
        .fee_utxos = copy_utxo_list(fee_utxos),
        .change_address = optional_addr(change_address),
        .script_flags = script_flags,
    });
    if ( ! r.has_value()) return kth::to_c_err(r.error());
    *out = kth::leak(std::move(*r));
    return kth_ec_success;
}

void kth_wallet_cashtoken_token_mint_result_destruct(
    kth_cashtoken_token_mint_result_mut_t self
) {
    kth::del<ct::token_mint_result>(self);
}

kth_transaction_const_t kth_wallet_cashtoken_token_mint_result_transaction(
    kth_cashtoken_token_mint_result_const_t self
) {
    KTH_PRECONDITION(self != nullptr);
    return &kth::cpp_ref<ct::token_mint_result>(self).transaction;
}

kth_size_t kth_wallet_cashtoken_token_mint_result_signing_indices_count(
    kth_cashtoken_token_mint_result_const_t self
) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<ct::token_mint_result>(self).signing_indices.size();
}

uint32_t kth_wallet_cashtoken_token_mint_result_signing_index_nth(
    kth_cashtoken_token_mint_result_const_t self, kth_size_t index
) {
    KTH_PRECONDITION(self != nullptr);
    auto const& v = kth::cpp_ref<ct::token_mint_result>(self).signing_indices;
    KTH_PRECONDITION(kth::sz(index) < v.size());
    return v[kth::sz(index)];
}

kth_size_t kth_wallet_cashtoken_token_mint_result_minted_output_indices_count(
    kth_cashtoken_token_mint_result_const_t self
) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<ct::token_mint_result>(self).minted_output_indices.size();
}

uint32_t kth_wallet_cashtoken_token_mint_result_minted_output_index_nth(
    kth_cashtoken_token_mint_result_const_t self, kth_size_t index
) {
    KTH_PRECONDITION(self != nullptr);
    auto const& v = kth::cpp_ref<ct::token_mint_result>(self).minted_output_indices;
    KTH_PRECONDITION(kth::sz(index) < v.size());
    return v[kth::sz(index)];
}


// ---------------------------------------------------------------------------
// create_token_transfer
// ---------------------------------------------------------------------------

kth_error_code_t kth_wallet_cashtoken_create_token_transfer(
    kth_utxo_list_const_t token_utxos,
    kth_payment_address_const_t destination,
    kth_bool_t has_ft_amount, uint64_t ft_amount,
    kth_cashtoken_nft_spec_const_t nft,
    kth_utxo_list_const_t fee_utxos,
    kth_payment_address_const_t token_change_address,
    kth_payment_address_const_t bch_change_address,
    uint64_t satoshis,
    KTH_OUT_OWNED kth_cashtoken_token_tx_result_mut_t* out
) {
    KTH_PRECONDITION(destination != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);

    auto r = ct::create_token_transfer(ct::token_transfer_params{
        .token_utxos = copy_utxo_list(token_utxos),
        .destination = kth::cpp_ref<kth::domain::wallet::payment_address>(destination),
        .ft_amount = kth::int_to_bool(has_ft_amount) ? std::optional<uint64_t>(ft_amount) : std::nullopt,
        .nft = optional_nft_spec(nft),
        .fee_utxos = copy_utxo_list(fee_utxos),
        .token_change_address = optional_addr(token_change_address),
        .bch_change_address = optional_addr(bch_change_address),
        .satoshis = satoshis,
    });
    if ( ! r.has_value()) return kth::to_c_err(r.error());
    *out = kth::leak(std::move(*r));
    return kth_ec_success;
}


// ---------------------------------------------------------------------------
// create_token_burn
// ---------------------------------------------------------------------------

kth_error_code_t kth_wallet_cashtoken_create_token_burn(
    kth_utxo_const_t token_utxo,
    kth_bool_t has_burn_ft_amount, uint64_t burn_ft_amount,
    kth_bool_t burn_nft,
    char const* message,
    kth_payment_address_const_t destination,
    kth_utxo_list_const_t fee_utxos,
    kth_payment_address_const_t change_address,
    uint64_t satoshis,
    KTH_OUT_OWNED kth_cashtoken_token_tx_result_mut_t* out
) {
    KTH_PRECONDITION(token_utxo != nullptr);
    KTH_PRECONDITION(destination != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);

    auto r = ct::create_token_burn(ct::token_burn_params{
        .token_utxo = kth::cpp_ref<kth::domain::chain::utxo>(token_utxo),
        .burn_ft_amount = kth::int_to_bool(has_burn_ft_amount) ? std::optional<uint64_t>(burn_ft_amount) : std::nullopt,
        .burn_nft = kth::int_to_bool(burn_nft),
        .message = message == nullptr ? std::optional<std::string>(std::nullopt) : std::optional<std::string>(message),
        .destination = kth::cpp_ref<kth::domain::wallet::payment_address>(destination),
        .fee_utxos = copy_utxo_list(fee_utxos),
        .change_address = optional_addr(change_address),
        .satoshis = satoshis,
    });
    if ( ! r.has_value()) return kth::to_c_err(r.error());
    *out = kth::leak(std::move(*r));
    return kth_ec_success;
}


// ---------------------------------------------------------------------------
// token_tx_result — shared by transfer / burn
// ---------------------------------------------------------------------------

void kth_wallet_cashtoken_token_tx_result_destruct(
    kth_cashtoken_token_tx_result_mut_t self
) {
    kth::del<ct::token_tx_result>(self);
}

kth_transaction_const_t kth_wallet_cashtoken_token_tx_result_transaction(
    kth_cashtoken_token_tx_result_const_t self
) {
    KTH_PRECONDITION(self != nullptr);
    return &kth::cpp_ref<ct::token_tx_result>(self).transaction;
}

kth_size_t kth_wallet_cashtoken_token_tx_result_signing_indices_count(
    kth_cashtoken_token_tx_result_const_t self
) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<ct::token_tx_result>(self).signing_indices.size();
}

uint32_t kth_wallet_cashtoken_token_tx_result_signing_index_nth(
    kth_cashtoken_token_tx_result_const_t self, kth_size_t index
) {
    KTH_PRECONDITION(self != nullptr);
    auto const& v = kth::cpp_ref<ct::token_tx_result>(self).signing_indices;
    KTH_PRECONDITION(kth::sz(index) < v.size());
    return v[kth::sz(index)];
}


// ---------------------------------------------------------------------------
// create_ft
// ---------------------------------------------------------------------------

kth_error_code_t kth_wallet_cashtoken_create_ft(
    kth_utxo_const_t genesis_utxo,
    kth_payment_address_const_t destination,
    uint64_t total_supply,
    kth_bool_t with_minting_nft,
    kth_utxo_list_const_t fee_utxos,
    kth_payment_address_const_t change_address,
    uint64_t script_flags,
    KTH_OUT_OWNED kth_cashtoken_token_genesis_result_mut_t* out
) {
    KTH_PRECONDITION(genesis_utxo != nullptr);
    KTH_PRECONDITION(destination != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);

    auto r = ct::create_ft(ct::ft_params{
        .genesis_utxo = kth::cpp_ref<kth::domain::chain::utxo>(genesis_utxo),
        .destination = kth::cpp_ref<kth::domain::wallet::payment_address>(destination),
        .total_supply = total_supply,
        .with_minting_nft = kth::int_to_bool(with_minting_nft),
        .fee_utxos = copy_utxo_list(fee_utxos),
        .change_address = optional_addr(change_address),
        .script_flags = script_flags,
    });
    if ( ! r.has_value()) return kth::to_c_err(r.error());
    *out = kth::leak(std::move(*r));
    return kth_ec_success;
}


// ---------------------------------------------------------------------------
// create_nft_collection
// ---------------------------------------------------------------------------

kth_error_code_t kth_wallet_cashtoken_create_nft_collection(
    kth_utxo_const_t genesis_utxo,
    kth_cashtoken_nft_collection_item_list_const_t nfts,
    kth_payment_address_const_t creator_address,
    kth_bool_t keep_minting_token,
    kth_bool_t has_ft_amount, uint64_t ft_amount,
    kth_utxo_list_const_t fee_utxos,
    kth_payment_address_const_t change_address,
    kth_size_t batch_size,
    uint64_t script_flags,
    KTH_OUT_OWNED kth_cashtoken_nft_collection_result_mut_t* out
) {
    KTH_PRECONDITION(genesis_utxo != nullptr);
    KTH_PRECONDITION(nfts != nullptr);
    KTH_PRECONDITION(creator_address != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);

    auto r = ct::create_nft_collection(ct::nft_collection_params{
        .genesis_utxo = kth::cpp_ref<kth::domain::chain::utxo>(genesis_utxo),
        .nfts = kth::cpp_ref<std::vector<ct::nft_collection_item>>(nfts),
        .creator_address = kth::cpp_ref<kth::domain::wallet::payment_address>(creator_address),
        .keep_minting_token = kth::int_to_bool(keep_minting_token),
        .ft_amount = kth::int_to_bool(has_ft_amount) ? std::optional<uint64_t>(ft_amount) : std::nullopt,
        .fee_utxos = copy_utxo_list(fee_utxos),
        .change_address = optional_addr(change_address),
        .batch_size = kth::sz(batch_size),
        .script_flags = script_flags,
    });
    if ( ! r.has_value()) return kth::to_c_err(r.error());
    *out = kth::leak(std::move(*r));
    return kth_ec_success;
}

void kth_wallet_cashtoken_nft_collection_result_destruct(
    kth_cashtoken_nft_collection_result_mut_t self
) {
    kth::del<ct::nft_collection_result>(self);
}

kth_transaction_const_t kth_wallet_cashtoken_nft_collection_result_genesis_transaction(
    kth_cashtoken_nft_collection_result_const_t self
) {
    KTH_PRECONDITION(self != nullptr);
    return &kth::cpp_ref<ct::nft_collection_result>(self).genesis_transaction;
}

kth_size_t kth_wallet_cashtoken_nft_collection_result_genesis_signing_indices_count(
    kth_cashtoken_nft_collection_result_const_t self
) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<ct::nft_collection_result>(self).genesis_signing_indices.size();
}

uint32_t kth_wallet_cashtoken_nft_collection_result_genesis_signing_index_nth(
    kth_cashtoken_nft_collection_result_const_t self, kth_size_t index
) {
    KTH_PRECONDITION(self != nullptr);
    auto const& v = kth::cpp_ref<ct::nft_collection_result>(self).genesis_signing_indices;
    KTH_PRECONDITION(kth::sz(index) < v.size());
    return v[kth::sz(index)];
}

void kth_wallet_cashtoken_nft_collection_result_category_id(
    kth_cashtoken_nft_collection_result_const_t self, uint8_t* out_hash
) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_hash != nullptr);
    auto const& id = kth::cpp_ref<ct::nft_collection_result>(self).category_id;
    std::memcpy(out_hash, id.data(), id.size());
}

kth_bool_t kth_wallet_cashtoken_nft_collection_result_final_burn(
    kth_cashtoken_nft_collection_result_const_t self
) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<ct::nft_collection_result>(self).final_burn);
}

kth_size_t kth_wallet_cashtoken_nft_collection_result_batches_count(
    kth_cashtoken_nft_collection_result_const_t self
) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<ct::nft_collection_result>(self).batches.size();
}

kth_cashtoken_nft_mint_request_list_const_t
kth_wallet_cashtoken_nft_collection_result_batch_mint_requests(
    kth_cashtoken_nft_collection_result_const_t self, kth_size_t batch_index
) {
    KTH_PRECONDITION(self != nullptr);
    auto const& r = kth::cpp_ref<ct::nft_collection_result>(self);
    KTH_PRECONDITION(kth::sz(batch_index) < r.batches.size());
    return &r.batches[kth::sz(batch_index)].mint_requests;
}

} // extern "C"
