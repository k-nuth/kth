// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_SRC_WALLET_CASHTOKEN_MINTING_PRIVATE_HPP_
#define KTH_CAPI_SRC_WALLET_CASHTOKEN_MINTING_PRIVATE_HPP_

// Private to the C-API sources, not installed. Helpers shared by
// `cashtoken_minting.cpp` (hand-written) and `cashtoken_minting_generated.cpp`
// (generated).

#include <kth/capi/wallet/cashtoken_minting.h>

#include <optional>
#include <vector>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/chain/utxo.hpp>
#include <kth/domain/wallet/cashtoken_minting.hpp>
#include <kth/domain/wallet/payment_address.hpp>

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

#endif // KTH_CAPI_SRC_WALLET_CASHTOKEN_MINTING_PRIVATE_HPP_
