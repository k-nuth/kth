// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_MINING_TRANSACTION_ELEMENT_HPP_
#define KTH_BLOCKCHAIN_MINING_TRANSACTION_ELEMENT_HPP_

#include <unordered_map>
#include <vector>

#include <kth/domain.hpp>

namespace kth {
namespace mining {

class transaction_element {
public:

    transaction_element(domain::chain::transaction const& tx)
        : transaction_(tx)
        , txid_(transaction_.hash())
#if ! defined(KTH_CURRENCY_BCH)
        , hash_(transaction_.hash(true))
#endif
        , raw_(transaction_.to_data(true, KTH_WITNESS_DEFAULT))
        , fee_(transaction_.fees())
        , sigops_(transaction_.signature_operations(true /*bip16*/, false /*bip141*/)) {}  // BCH: P2SH always active, no segwit.

    transaction_element(domain::chain::transaction&& tx)
        : transaction_(std::move(tx))
        , txid_(transaction_.hash())
#if ! defined(KTH_CURRENCY_BCH)
        , hash_(transaction_.hash(true))
#endif
        , raw_(transaction_.to_data(true, KTH_WITNESS_DEFAULT))
        , fee_(transaction_.fees())
        , sigops_(transaction_.signature_operations(true /*bip16*/, false /*bip141*/)) {}  // BCH: P2SH always active, no segwit.

    domain::chain::transaction const& transaction() const {
        return transaction_;
    }

    hash_digest const& txid() const {
        return txid_;
    }

#if ! defined(KTH_CURRENCY_BCH)
    hash_digest const& hash() const {
        return hash_;
    }
#endif

    data_chunk const& raw() const {
        return raw_;
    }

    uint64_t fee() const {
        return fee_;
    }

    size_t sigops() const {
        return sigops_;
    }

    size_t size() const {
        return raw_.size();
    }

    uint32_t output_count() const {
        return transaction_.outputs().size();
    }

private:
    domain::chain::transaction transaction_;
    hash_digest txid_;

#if ! defined(KTH_CURRENCY_BCH)
    hash_digest hash_;
#endif

    data_chunk raw_;
    uint64_t fee_;
    size_t sigops_;
};

}  // namespace mining
}  // namespace kth

#endif  //KTH_BLOCKCHAIN_MINING_TRANSACTION_ELEMENT_HPP_
