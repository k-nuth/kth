// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_UTXO_HPP
#define KTH_DOMAIN_CHAIN_UTXO_HPP

#include <compare>
#include <cstdint>
#include <optional>

#include <kth/domain/chain/output_point.hpp>
#include <kth/domain/chain/token_data.hpp>
#include <kth/domain/define.hpp>

namespace kth::domain::chain {

struct KD_API utxo {
    utxo() = default;

    utxo(output_point const& point, uint64_t amount,
         std::optional<token_data_t> token_data = std::nullopt);
    utxo(output_point&& point, uint64_t amount,
         std::optional<token_data_t> token_data = std::nullopt);

    friend
    bool operator==(utxo const&, utxo const&) = default;

    uint32_t height() const;
    output_point& point();
    output_point const& point() const;
    uint64_t amount() const;
    std::optional<token_data_t>& token_data();
    std::optional<token_data_t> const& token_data() const;

    void set_height(uint32_t height);
    void set_point(output_point const& point);
    void set_amount(uint64_t amount);
    void set_token_data(std::optional<token_data_t> token_data);

private:
    uint32_t height_ = 0;
    output_point point_;
    uint64_t amount_ = 0;
    std::optional<token_data_t> token_data_;
};

} // namespace kth::domain::chain

#endif
