// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/wallet/cashtoken_minting.h>

#include <cstring>
#include <utility>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/chain/token_data.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/chain/utxo.hpp>
#include <kth/domain/wallet/cashtoken_minting.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/infrastructure/error.hpp>

namespace ct = kth::domain::wallet::cashtoken;

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

// Copies a kth_utxo_list_const_t handle into a fresh C++ vector. NULL
// list is interpreted as "empty vector" — the only way the C caller
// can express "no fee UTXOs" at this ABI.
std::vector<kth::domain::chain::utxo>
copy_utxo_list(kth_utxo_list_const_t list) {
    if (list == nullptr) return {};
    return kth::cpp_ref<std::vector<kth::domain::chain::utxo>>(list);
}

// Wraps a nullable address handle in std::optional. NULL → nullopt.
std::optional<kth::domain::wallet::payment_address>
optional_addr(kth_payment_address_const_t addr) {
    if (addr == nullptr) return std::nullopt;
    return kth::cpp_ref<kth::domain::wallet::payment_address>(addr);
}

} // namespace

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
    ct::nft_spec s;
    s.capability = static_cast<kth::domain::chain::capability_t>(capability);
    s.commitment = commitment_from(commitment, commitment_n);
    return kth::leak(std::move(s));
}

void kth_wallet_cashtoken_nft_spec_destruct(kth_cashtoken_nft_spec_mut_t self) {
    kth::del<ct::nft_spec>(self);
}

// ---------------------------------------------------------------------------
// nft_mint_request
// ---------------------------------------------------------------------------

kth_cashtoken_nft_mint_request_mut_t kth_wallet_cashtoken_nft_mint_request_construct(
    kth_payment_address_const_t destination,
    uint8_t const* commitment,
    kth_size_t commitment_n,
    kth_token_capability_t capability,
    uint64_t satoshis
) {
    KTH_PRECONDITION(destination != nullptr);
    ct::nft_mint_request r;
    r.destination = kth::cpp_ref<kth::domain::wallet::payment_address>(destination);
    r.commitment = commitment_from(commitment, commitment_n);
    r.capability = static_cast<kth::domain::chain::capability_t>(capability);
    r.satoshis = satoshis;
    return kth::leak(std::move(r));
}

void kth_wallet_cashtoken_nft_mint_request_destruct(
    kth_cashtoken_nft_mint_request_mut_t self
) {
    kth::del<ct::nft_mint_request>(self);
}

// --- list ---

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
// nft_collection_item
// ---------------------------------------------------------------------------

kth_cashtoken_nft_collection_item_mut_t kth_wallet_cashtoken_nft_collection_item_construct(
    uint8_t const* commitment,
    kth_size_t commitment_n,
    kth_payment_address_const_t destination
) {
    ct::nft_collection_item item;
    item.commitment = commitment_from(commitment, commitment_n);
    if (destination != nullptr) {
        item.destination = kth::cpp_ref<kth::domain::wallet::payment_address>(destination);
    }
    return kth::leak(std::move(item));
}

void kth_wallet_cashtoken_nft_collection_item_destruct(
    kth_cashtoken_nft_collection_item_mut_t self
) {
    kth::del<ct::nft_collection_item>(self);
}

// --- list ---

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
// prepare_genesis_params / prepare_genesis_result
// ---------------------------------------------------------------------------

kth_cashtoken_prepare_genesis_params_mut_t
kth_wallet_cashtoken_prepare_genesis_params_construct_default(void) {
    return kth::leak<ct::prepare_genesis_params>();
}

void kth_wallet_cashtoken_prepare_genesis_params_destruct(
    kth_cashtoken_prepare_genesis_params_mut_t self
) {
    kth::del<ct::prepare_genesis_params>(self);
}

void kth_wallet_cashtoken_prepare_genesis_params_set_utxo(
    kth_cashtoken_prepare_genesis_params_mut_t self, kth_utxo_const_t utxo
) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(utxo != nullptr);
    kth::cpp_ref<ct::prepare_genesis_params>(self).utxo =
        kth::cpp_ref<kth::domain::chain::utxo>(utxo);
}

void kth_wallet_cashtoken_prepare_genesis_params_set_destination(
    kth_cashtoken_prepare_genesis_params_mut_t self, kth_payment_address_const_t destination
) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(destination != nullptr);
    kth::cpp_ref<ct::prepare_genesis_params>(self).destination =
        kth::cpp_ref<kth::domain::wallet::payment_address>(destination);
}

void kth_wallet_cashtoken_prepare_genesis_params_set_satoshis(
    kth_cashtoken_prepare_genesis_params_mut_t self, uint64_t satoshis
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::prepare_genesis_params>(self).satoshis = satoshis;
}

void kth_wallet_cashtoken_prepare_genesis_params_set_change_address(
    kth_cashtoken_prepare_genesis_params_mut_t self, kth_payment_address_const_t change_address
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::prepare_genesis_params>(self).change_address = optional_addr(change_address);
}

kth_error_code_t kth_wallet_cashtoken_prepare_genesis_utxo(
    kth_cashtoken_prepare_genesis_params_const_t params,
    KTH_OUT_OWNED kth_cashtoken_prepare_genesis_result_mut_t* out
) {
    KTH_PRECONDITION(params != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto r = ct::prepare_genesis_utxo(kth::cpp_ref<ct::prepare_genesis_params>(params));
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
// token_genesis_params / token_genesis_result / create_token_genesis
// ---------------------------------------------------------------------------

kth_cashtoken_token_genesis_params_mut_t
kth_wallet_cashtoken_token_genesis_params_construct_default(void) {
    return kth::leak<ct::token_genesis_params>();
}

void kth_wallet_cashtoken_token_genesis_params_destruct(
    kth_cashtoken_token_genesis_params_mut_t self
) {
    kth::del<ct::token_genesis_params>(self);
}

void kth_wallet_cashtoken_token_genesis_params_set_genesis_utxo(
    kth_cashtoken_token_genesis_params_mut_t self, kth_utxo_const_t genesis_utxo
) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(genesis_utxo != nullptr);
    kth::cpp_ref<ct::token_genesis_params>(self).genesis_utxo =
        kth::cpp_ref<kth::domain::chain::utxo>(genesis_utxo);
}

void kth_wallet_cashtoken_token_genesis_params_set_destination(
    kth_cashtoken_token_genesis_params_mut_t self, kth_payment_address_const_t destination
) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(destination != nullptr);
    kth::cpp_ref<ct::token_genesis_params>(self).destination =
        kth::cpp_ref<kth::domain::wallet::payment_address>(destination);
}

void kth_wallet_cashtoken_token_genesis_params_set_ft_amount(
    kth_cashtoken_token_genesis_params_mut_t self, kth_bool_t has_value, uint64_t ft_amount
) {
    KTH_PRECONDITION(self != nullptr);
    auto& p = kth::cpp_ref<ct::token_genesis_params>(self);
    p.ft_amount = kth::int_to_bool(has_value) ? std::optional<uint64_t>(ft_amount) : std::nullopt;
}

void kth_wallet_cashtoken_token_genesis_params_set_nft(
    kth_cashtoken_token_genesis_params_mut_t self, kth_cashtoken_nft_spec_const_t nft
) {
    KTH_PRECONDITION(self != nullptr);
    auto& p = kth::cpp_ref<ct::token_genesis_params>(self);
    p.nft = nft == nullptr
        ? std::nullopt
        : std::optional<ct::nft_spec>(kth::cpp_ref<ct::nft_spec>(nft));
}

void kth_wallet_cashtoken_token_genesis_params_set_satoshis(
    kth_cashtoken_token_genesis_params_mut_t self, uint64_t satoshis
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::token_genesis_params>(self).satoshis = satoshis;
}

void kth_wallet_cashtoken_token_genesis_params_set_fee_utxos(
    kth_cashtoken_token_genesis_params_mut_t self, kth_utxo_list_const_t fee_utxos
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::token_genesis_params>(self).fee_utxos = copy_utxo_list(fee_utxos);
}

void kth_wallet_cashtoken_token_genesis_params_set_change_address(
    kth_cashtoken_token_genesis_params_mut_t self, kth_payment_address_const_t change_address
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::token_genesis_params>(self).change_address = optional_addr(change_address);
}

void kth_wallet_cashtoken_token_genesis_params_set_script_flags(
    kth_cashtoken_token_genesis_params_mut_t self, uint64_t script_flags
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::token_genesis_params>(self).script_flags = script_flags;
}

kth_error_code_t kth_wallet_cashtoken_create_token_genesis(
    kth_cashtoken_token_genesis_params_const_t params,
    KTH_OUT_OWNED kth_cashtoken_token_genesis_result_mut_t* out
) {
    KTH_PRECONDITION(params != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto r = ct::create_token_genesis(kth::cpp_ref<ct::token_genesis_params>(params));
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
// token_mint_params / token_mint_result / create_token_mint
// ---------------------------------------------------------------------------

kth_cashtoken_token_mint_params_mut_t
kth_wallet_cashtoken_token_mint_params_construct_default(void) {
    return kth::leak<ct::token_mint_params>();
}

void kth_wallet_cashtoken_token_mint_params_destruct(
    kth_cashtoken_token_mint_params_mut_t self
) {
    kth::del<ct::token_mint_params>(self);
}

void kth_wallet_cashtoken_token_mint_params_set_minting_utxo(
    kth_cashtoken_token_mint_params_mut_t self, kth_utxo_const_t minting_utxo
) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(minting_utxo != nullptr);
    kth::cpp_ref<ct::token_mint_params>(self).minting_utxo =
        kth::cpp_ref<kth::domain::chain::utxo>(minting_utxo);
}

void kth_wallet_cashtoken_token_mint_params_set_nfts(
    kth_cashtoken_token_mint_params_mut_t self,
    kth_cashtoken_nft_mint_request_list_const_t nfts
) {
    KTH_PRECONDITION(self != nullptr);
    auto& p = kth::cpp_ref<ct::token_mint_params>(self);
    p.nfts = nfts == nullptr
        ? std::vector<ct::nft_mint_request>{}
        : kth::cpp_ref<std::vector<ct::nft_mint_request>>(nfts);
}

void kth_wallet_cashtoken_token_mint_params_set_new_minting_commitment(
    kth_cashtoken_token_mint_params_mut_t self,
    uint8_t const* commitment, kth_size_t commitment_n
) {
    KTH_PRECONDITION(self != nullptr);
    auto& p = kth::cpp_ref<ct::token_mint_params>(self);
    p.new_minting_commitment = commitment == nullptr
        ? std::nullopt
        : std::optional<kth::data_chunk>(commitment_from(commitment, commitment_n));
}

void kth_wallet_cashtoken_token_mint_params_set_minting_destination(
    kth_cashtoken_token_mint_params_mut_t self,
    kth_payment_address_const_t minting_destination
) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(minting_destination != nullptr);
    kth::cpp_ref<ct::token_mint_params>(self).minting_destination =
        kth::cpp_ref<kth::domain::wallet::payment_address>(minting_destination);
}

void kth_wallet_cashtoken_token_mint_params_set_fee_utxos(
    kth_cashtoken_token_mint_params_mut_t self, kth_utxo_list_const_t fee_utxos
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::token_mint_params>(self).fee_utxos = copy_utxo_list(fee_utxos);
}

void kth_wallet_cashtoken_token_mint_params_set_change_address(
    kth_cashtoken_token_mint_params_mut_t self,
    kth_payment_address_const_t change_address
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::token_mint_params>(self).change_address = optional_addr(change_address);
}

void kth_wallet_cashtoken_token_mint_params_set_script_flags(
    kth_cashtoken_token_mint_params_mut_t self, uint64_t script_flags
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::token_mint_params>(self).script_flags = script_flags;
}

kth_error_code_t kth_wallet_cashtoken_create_token_mint(
    kth_cashtoken_token_mint_params_const_t params,
    KTH_OUT_OWNED kth_cashtoken_token_mint_result_mut_t* out
) {
    KTH_PRECONDITION(params != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto r = ct::create_token_mint(kth::cpp_ref<ct::token_mint_params>(params));
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
// token_transfer_params / create_token_transfer
// ---------------------------------------------------------------------------

kth_cashtoken_token_transfer_params_mut_t
kth_wallet_cashtoken_token_transfer_params_construct_default(void) {
    return kth::leak<ct::token_transfer_params>();
}

void kth_wallet_cashtoken_token_transfer_params_destruct(
    kth_cashtoken_token_transfer_params_mut_t self
) {
    kth::del<ct::token_transfer_params>(self);
}

void kth_wallet_cashtoken_token_transfer_params_set_token_utxos(
    kth_cashtoken_token_transfer_params_mut_t self, kth_utxo_list_const_t token_utxos
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::token_transfer_params>(self).token_utxos = copy_utxo_list(token_utxos);
}

void kth_wallet_cashtoken_token_transfer_params_set_destination(
    kth_cashtoken_token_transfer_params_mut_t self, kth_payment_address_const_t destination
) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(destination != nullptr);
    kth::cpp_ref<ct::token_transfer_params>(self).destination =
        kth::cpp_ref<kth::domain::wallet::payment_address>(destination);
}

void kth_wallet_cashtoken_token_transfer_params_set_ft_amount(
    kth_cashtoken_token_transfer_params_mut_t self, kth_bool_t has_value, uint64_t ft_amount
) {
    KTH_PRECONDITION(self != nullptr);
    auto& p = kth::cpp_ref<ct::token_transfer_params>(self);
    p.ft_amount = kth::int_to_bool(has_value) ? std::optional<uint64_t>(ft_amount) : std::nullopt;
}

void kth_wallet_cashtoken_token_transfer_params_set_nft(
    kth_cashtoken_token_transfer_params_mut_t self, kth_cashtoken_nft_spec_const_t nft
) {
    KTH_PRECONDITION(self != nullptr);
    auto& p = kth::cpp_ref<ct::token_transfer_params>(self);
    p.nft = nft == nullptr
        ? std::nullopt
        : std::optional<ct::nft_spec>(kth::cpp_ref<ct::nft_spec>(nft));
}

void kth_wallet_cashtoken_token_transfer_params_set_fee_utxos(
    kth_cashtoken_token_transfer_params_mut_t self, kth_utxo_list_const_t fee_utxos
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::token_transfer_params>(self).fee_utxos = copy_utxo_list(fee_utxos);
}

void kth_wallet_cashtoken_token_transfer_params_set_token_change_address(
    kth_cashtoken_token_transfer_params_mut_t self, kth_payment_address_const_t addr
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::token_transfer_params>(self).token_change_address = optional_addr(addr);
}

void kth_wallet_cashtoken_token_transfer_params_set_bch_change_address(
    kth_cashtoken_token_transfer_params_mut_t self, kth_payment_address_const_t addr
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::token_transfer_params>(self).bch_change_address = optional_addr(addr);
}

void kth_wallet_cashtoken_token_transfer_params_set_satoshis(
    kth_cashtoken_token_transfer_params_mut_t self, uint64_t satoshis
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::token_transfer_params>(self).satoshis = satoshis;
}

kth_error_code_t kth_wallet_cashtoken_create_token_transfer(
    kth_cashtoken_token_transfer_params_const_t params,
    KTH_OUT_OWNED kth_cashtoken_token_tx_result_mut_t* out
) {
    KTH_PRECONDITION(params != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto r = ct::create_token_transfer(kth::cpp_ref<ct::token_transfer_params>(params));
    if ( ! r.has_value()) return kth::to_c_err(r.error());
    *out = kth::leak(std::move(*r));
    return kth_ec_success;
}

// ---------------------------------------------------------------------------
// token_burn_params / create_token_burn
// ---------------------------------------------------------------------------

kth_cashtoken_token_burn_params_mut_t
kth_wallet_cashtoken_token_burn_params_construct_default(void) {
    return kth::leak<ct::token_burn_params>();
}

void kth_wallet_cashtoken_token_burn_params_destruct(
    kth_cashtoken_token_burn_params_mut_t self
) {
    kth::del<ct::token_burn_params>(self);
}

void kth_wallet_cashtoken_token_burn_params_set_token_utxo(
    kth_cashtoken_token_burn_params_mut_t self, kth_utxo_const_t token_utxo
) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(token_utxo != nullptr);
    kth::cpp_ref<ct::token_burn_params>(self).token_utxo =
        kth::cpp_ref<kth::domain::chain::utxo>(token_utxo);
}

void kth_wallet_cashtoken_token_burn_params_set_burn_ft_amount(
    kth_cashtoken_token_burn_params_mut_t self, kth_bool_t has_value, uint64_t burn_ft_amount
) {
    KTH_PRECONDITION(self != nullptr);
    auto& p = kth::cpp_ref<ct::token_burn_params>(self);
    p.burn_ft_amount = kth::int_to_bool(has_value)
        ? std::optional<uint64_t>(burn_ft_amount)
        : std::nullopt;
}

void kth_wallet_cashtoken_token_burn_params_set_burn_nft(
    kth_cashtoken_token_burn_params_mut_t self, kth_bool_t burn_nft
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::token_burn_params>(self).burn_nft = kth::int_to_bool(burn_nft);
}

void kth_wallet_cashtoken_token_burn_params_set_message(
    kth_cashtoken_token_burn_params_mut_t self, char const* message
) {
    KTH_PRECONDITION(self != nullptr);
    auto& p = kth::cpp_ref<ct::token_burn_params>(self);
    p.message = message == nullptr ? std::nullopt : std::optional<std::string>(message);
}

void kth_wallet_cashtoken_token_burn_params_set_destination(
    kth_cashtoken_token_burn_params_mut_t self, kth_payment_address_const_t destination
) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(destination != nullptr);
    kth::cpp_ref<ct::token_burn_params>(self).destination =
        kth::cpp_ref<kth::domain::wallet::payment_address>(destination);
}

void kth_wallet_cashtoken_token_burn_params_set_fee_utxos(
    kth_cashtoken_token_burn_params_mut_t self, kth_utxo_list_const_t fee_utxos
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::token_burn_params>(self).fee_utxos = copy_utxo_list(fee_utxos);
}

void kth_wallet_cashtoken_token_burn_params_set_change_address(
    kth_cashtoken_token_burn_params_mut_t self, kth_payment_address_const_t change_address
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::token_burn_params>(self).change_address = optional_addr(change_address);
}

void kth_wallet_cashtoken_token_burn_params_set_satoshis(
    kth_cashtoken_token_burn_params_mut_t self, uint64_t satoshis
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::token_burn_params>(self).satoshis = satoshis;
}

kth_error_code_t kth_wallet_cashtoken_create_token_burn(
    kth_cashtoken_token_burn_params_const_t params,
    KTH_OUT_OWNED kth_cashtoken_token_tx_result_mut_t* out
) {
    KTH_PRECONDITION(params != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto r = ct::create_token_burn(kth::cpp_ref<ct::token_burn_params>(params));
    if ( ! r.has_value()) return kth::to_c_err(r.error());
    *out = kth::leak(std::move(*r));
    return kth_ec_success;
}

// ---------------------------------------------------------------------------
// token_tx_result (shared by transfer / burn)
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
// ft_params / create_ft
// ---------------------------------------------------------------------------

kth_cashtoken_ft_params_mut_t kth_wallet_cashtoken_ft_params_construct_default(void) {
    return kth::leak<ct::ft_params>();
}

void kth_wallet_cashtoken_ft_params_destruct(kth_cashtoken_ft_params_mut_t self) {
    kth::del<ct::ft_params>(self);
}

void kth_wallet_cashtoken_ft_params_set_genesis_utxo(
    kth_cashtoken_ft_params_mut_t self, kth_utxo_const_t genesis_utxo
) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(genesis_utxo != nullptr);
    kth::cpp_ref<ct::ft_params>(self).genesis_utxo =
        kth::cpp_ref<kth::domain::chain::utxo>(genesis_utxo);
}

void kth_wallet_cashtoken_ft_params_set_destination(
    kth_cashtoken_ft_params_mut_t self, kth_payment_address_const_t destination
) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(destination != nullptr);
    kth::cpp_ref<ct::ft_params>(self).destination =
        kth::cpp_ref<kth::domain::wallet::payment_address>(destination);
}

void kth_wallet_cashtoken_ft_params_set_total_supply(
    kth_cashtoken_ft_params_mut_t self, uint64_t total_supply
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::ft_params>(self).total_supply = total_supply;
}

void kth_wallet_cashtoken_ft_params_set_with_minting_nft(
    kth_cashtoken_ft_params_mut_t self, kth_bool_t with_minting_nft
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::ft_params>(self).with_minting_nft = kth::int_to_bool(with_minting_nft);
}

void kth_wallet_cashtoken_ft_params_set_fee_utxos(
    kth_cashtoken_ft_params_mut_t self, kth_utxo_list_const_t fee_utxos
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::ft_params>(self).fee_utxos = copy_utxo_list(fee_utxos);
}

void kth_wallet_cashtoken_ft_params_set_change_address(
    kth_cashtoken_ft_params_mut_t self, kth_payment_address_const_t change_address
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::ft_params>(self).change_address = optional_addr(change_address);
}

void kth_wallet_cashtoken_ft_params_set_script_flags(
    kth_cashtoken_ft_params_mut_t self, uint64_t script_flags
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::ft_params>(self).script_flags = script_flags;
}

kth_error_code_t kth_wallet_cashtoken_create_ft(
    kth_cashtoken_ft_params_const_t params,
    KTH_OUT_OWNED kth_cashtoken_token_genesis_result_mut_t* out
) {
    KTH_PRECONDITION(params != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto r = ct::create_ft(kth::cpp_ref<ct::ft_params>(params));
    if ( ! r.has_value()) return kth::to_c_err(r.error());
    *out = kth::leak(std::move(*r));
    return kth_ec_success;
}

// ---------------------------------------------------------------------------
// nft_collection_params / nft_collection_result / create_nft_collection
// ---------------------------------------------------------------------------

kth_cashtoken_nft_collection_params_mut_t
kth_wallet_cashtoken_nft_collection_params_construct_default(void) {
    return kth::leak<ct::nft_collection_params>();
}

void kth_wallet_cashtoken_nft_collection_params_destruct(
    kth_cashtoken_nft_collection_params_mut_t self
) {
    kth::del<ct::nft_collection_params>(self);
}

void kth_wallet_cashtoken_nft_collection_params_set_genesis_utxo(
    kth_cashtoken_nft_collection_params_mut_t self, kth_utxo_const_t genesis_utxo
) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(genesis_utxo != nullptr);
    kth::cpp_ref<ct::nft_collection_params>(self).genesis_utxo =
        kth::cpp_ref<kth::domain::chain::utxo>(genesis_utxo);
}

void kth_wallet_cashtoken_nft_collection_params_set_nfts(
    kth_cashtoken_nft_collection_params_mut_t self,
    kth_cashtoken_nft_collection_item_list_const_t nfts
) {
    KTH_PRECONDITION(self != nullptr);
    auto& p = kth::cpp_ref<ct::nft_collection_params>(self);
    p.nfts = nfts == nullptr
        ? std::vector<ct::nft_collection_item>{}
        : kth::cpp_ref<std::vector<ct::nft_collection_item>>(nfts);
}

void kth_wallet_cashtoken_nft_collection_params_set_creator_address(
    kth_cashtoken_nft_collection_params_mut_t self, kth_payment_address_const_t creator_address
) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(creator_address != nullptr);
    kth::cpp_ref<ct::nft_collection_params>(self).creator_address =
        kth::cpp_ref<kth::domain::wallet::payment_address>(creator_address);
}

void kth_wallet_cashtoken_nft_collection_params_set_keep_minting_token(
    kth_cashtoken_nft_collection_params_mut_t self, kth_bool_t keep_minting_token
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::nft_collection_params>(self).keep_minting_token =
        kth::int_to_bool(keep_minting_token);
}

void kth_wallet_cashtoken_nft_collection_params_set_ft_amount(
    kth_cashtoken_nft_collection_params_mut_t self, kth_bool_t has_value, uint64_t ft_amount
) {
    KTH_PRECONDITION(self != nullptr);
    auto& p = kth::cpp_ref<ct::nft_collection_params>(self);
    p.ft_amount = kth::int_to_bool(has_value) ? std::optional<uint64_t>(ft_amount) : std::nullopt;
}

void kth_wallet_cashtoken_nft_collection_params_set_fee_utxos(
    kth_cashtoken_nft_collection_params_mut_t self, kth_utxo_list_const_t fee_utxos
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::nft_collection_params>(self).fee_utxos = copy_utxo_list(fee_utxos);
}

void kth_wallet_cashtoken_nft_collection_params_set_change_address(
    kth_cashtoken_nft_collection_params_mut_t self, kth_payment_address_const_t change_address
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::nft_collection_params>(self).change_address = optional_addr(change_address);
}

void kth_wallet_cashtoken_nft_collection_params_set_batch_size(
    kth_cashtoken_nft_collection_params_mut_t self, kth_size_t batch_size
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::nft_collection_params>(self).batch_size = kth::sz(batch_size);
}

void kth_wallet_cashtoken_nft_collection_params_set_script_flags(
    kth_cashtoken_nft_collection_params_mut_t self, uint64_t script_flags
) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<ct::nft_collection_params>(self).script_flags = script_flags;
}

kth_error_code_t kth_wallet_cashtoken_create_nft_collection(
    kth_cashtoken_nft_collection_params_const_t params,
    KTH_OUT_OWNED kth_cashtoken_nft_collection_result_mut_t* out
) {
    KTH_PRECONDITION(params != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto r = ct::create_nft_collection(kth::cpp_ref<ct::nft_collection_params>(params));
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
