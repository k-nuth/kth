// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/utxo.hpp>

namespace kth::domain::chain {

utxo::utxo(output_point const& point, uint64_t amount, std::optional<token_data_t> token_data)
    : point_(point), amount_(amount), token_data_(token_data)
{}

utxo::utxo(output_point&& point, uint64_t amount, std::optional<token_data_t> token_data)
    : point_(std::move(point)), amount_(amount), token_data_(token_data)
{}

} // namespace kth::domain::chain
