// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/sighash_cache.hpp>

#include <kth/domain/chain/transaction.hpp>

namespace kth::domain::chain {

hash_digest const& sighash_cache::inpoints_hash() {
    if ( ! inpoints_) inpoints_ = tx_->inpoints_hash();
    return *inpoints_;
}

hash_digest const& sighash_cache::sequences_hash() {
    if ( ! sequences_) sequences_ = tx_->sequences_hash();
    return *sequences_;
}

hash_digest const& sighash_cache::outputs_hash() {
    if ( ! outputs_) outputs_ = tx_->outputs_hash();
    return *outputs_;
}

hash_digest const& sighash_cache::utxos_hash() {
    if ( ! utxos_) utxos_ = tx_->utxos_hash();
    return *utxos_;
}

} // namespace kth::domain::chain
