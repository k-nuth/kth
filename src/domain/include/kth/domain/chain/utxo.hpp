// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_UTXO_HPP
#define KTH_DOMAIN_CHAIN_UTXO_HPP

#include <compare>
#include <cstdint>
#include <optional>
#include <vector>

#include <kth/domain/chain/output_point.hpp>
#include <kth/domain/chain/token_data.hpp>
#include <kth/domain/define.hpp>

namespace kth::domain::chain {

struct KD_API utxo {
    using list = std::vector<utxo>;

    // Default state is a null utxo (coinbase-null prevout + zero amount
    // + no token). Analogous to `output_point::null()`: a well-defined
    // domain sentinel, not an "invalid" value.
    utxo() = default;

    utxo(output_point const& point, uint64_t amount,
         std::optional<token_data_t> token_data = std::nullopt);
    utxo(output_point&& point, uint64_t amount,
         std::optional<token_data_t> token_data = std::nullopt);

    friend
    auto operator<=>(utxo const&, utxo const&) = default;

    [[nodiscard]]
    constexpr uint32_t height() const noexcept { return height_; }

    [[nodiscard]]
    constexpr output_point const& point() const noexcept { return point_; }

    [[nodiscard]]
    constexpr uint64_t amount() const noexcept { return amount_; }

    [[nodiscard]]
    constexpr std::optional<token_data_t> const& token_data() const noexcept { return token_data_; }

private:
    uint32_t height_ = 0;
    output_point point_;
    uint64_t amount_ = 0;
    std::optional<token_data_t> token_data_;
};

} // namespace kth::domain::chain

#endif
