// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/wallet/cashtoken_minting.h>
#include "cashtoken_minting_private.hpp"

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

extern "C" {

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
    *out = kth::to_owned(std::move(*r));
    return kth_ec_success;
}

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
    *out = kth::to_owned(std::move(*r));
    return kth_ec_success;
}

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
    *out = kth::to_owned(std::move(*r));
    return kth_ec_success;
}

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
    *out = kth::to_owned(std::move(*r));
    return kth_ec_success;
}

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
    *out = kth::to_owned(std::move(*r));
    return kth_ec_success;
}

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
    *out = kth::to_owned(std::move(*r));
    return kth_ec_success;
}

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
    *out = kth::to_owned(std::move(*r));
    return kth_ec_success;
}

} // extern "C"
