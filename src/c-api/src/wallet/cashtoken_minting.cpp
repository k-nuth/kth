// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/wallet/cashtoken_minting.h>
#include "cashtoken_minting_private.hpp"

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

extern "C" {

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
    return kth::to_owned(ct::create_ft_output(dest_cpp, cat_cpp, ft_amount, satoshis));
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
    return kth::to_owned(ct::create_nft_output(
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
    return kth::to_owned(ct::create_combined_token_output(
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
    return kth::to_owned(ct::nft_spec{
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
    return kth::to_owned(ct::nft_mint_request{
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
    return kth::make_owned<std::vector<ct::nft_mint_request>>();
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
    return kth::to_owned(ct::nft_collection_item{
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
    return kth::make_owned<std::vector<ct::nft_collection_item>>();
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
// prepare_genesis_utxo results
// ---------------------------------------------------------------------------

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
// create_token_genesis results
// ---------------------------------------------------------------------------

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
// create_token_mint results
// ---------------------------------------------------------------------------

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
// create_nft_collection results
// ---------------------------------------------------------------------------

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
