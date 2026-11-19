// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/wallet/blocks.hpp>

#include <algorithm>

#include <kth/domain/chain/compact_filter.hpp>
#include <kth/domain/chain/golomb_match.hpp>
#include <kth/domain/chain/input_prefix_filter.hpp>
#include <kth/domain/chain/script_filter_builder.hpp>
#include <kth/domain/machine/opcode.hpp>
#include <kth/infrastructure/error.hpp>

namespace kth::node::wallet {

namespace {

// Empty scripts and those that start with OP_RETURN have no key.
bool keyed_script(data_chunk const& script) {
    return ! script.empty() && script.front() != static_cast<uint8_t>(domain::machine::opcode::return_);
}

} // namespace

std::error_code check_block(domain::chain::block const& blk, hash_digest const& expected) {
    if (blk.transactions().empty()) return error::empty_block;
    if (blk.hash() != expected) return error::bad_stream;
    if (blk.generate_merkle_root() != blk.header().merkle()) return error::merkle_mismatch;
    if ( ! blk.is_distinct_transaction_set()) return error::bad_stream;
    return {};
}

std::expected<script_filter_check, std::error_code>
check_script_filter(domain::chain::block const& blk, byte_span filter) {
    if (filter.size() <= hash_size) return std::unexpected(error::bad_stream);
    std::vector<hash_digest> created;
    for (auto const& tx : blk.transactions()) {
        for (auto const& out : tx.outputs()) {
            if (keyed_script(out.script().bytes())) created.push_back(domain::chain::script_key(out.script().bytes()));
            if (out.token_data()) created.push_back(domain::chain::category_key(out.token_data()->id));
        }
    }
    std::ranges::sort(created);
    created.erase(std::unique(created.begin(), created.end()), created.end());
    auto const created_count = created.size();
    for (auto const& tx : blk.transactions()) {
        created.push_back(domain::chain::transaction_key(tx.hash()));
    }
    std::vector<byte_span> items(created.begin(), created.end());
    auto const found = domain::chain::golomb_match_each(domain::chain::compact_filter_key(blk.hash()),
        filter.subspan(hash_size), items);
    if ( ! found) return std::unexpected(error::bad_stream);
    auto const outputs = size_t(std::ranges::count_if(*found, [&](size_t at) { return at < created_count; }));
    if (outputs != created_count) return std::unexpected(error::bad_stream);
    auto const transactions = found->size() - outputs;
    return script_filter_check{transactions == blk.transactions().size()};
}

std::error_code check_input_prefix_filter(domain::chain::block const& blk, byte_span filter) {
    auto const rebuilt = domain::chain::to_data(domain::chain::build_input_prefix_filter(blk));
    if ( ! std::ranges::equal(rebuilt, filter)) return error::bad_stream;
    return {};
}

std::vector<uint32_t> prefix_inputs(domain::chain::transaction const& tx, uint16_t prefix, uint8_t bits) {
    std::vector<uint32_t> out;
    if (tx.is_coinbase() || bits > 16) return out;
    auto const shift = 16u - bits;
    auto const low = uint32_t(prefix) << shift;
    auto const high = (uint32_t(prefix) + 1) << shift;
    auto const& inputs = tx.inputs();
    auto const counted = std::min(inputs.size(), domain::chain::input_prefix_inputs);
    for (size_t at = 0; at < counted; ++at) {
        auto const value = uint32_t(domain::chain::input_prefix(inputs[at]));
        if (value >= low && value < high) out.push_back(uint32_t(at));
    }
    return out;
}

} // namespace kth::node::wallet
