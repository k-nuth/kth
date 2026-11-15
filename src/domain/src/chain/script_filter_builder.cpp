// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/script_filter_builder.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <kth/domain/machine/opcode.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::chain {

namespace {

// The Golomb key is 16 bytes. A block hash is 32, so the prefix always fits.
static_assert(golomb_key{}.size() <= hash_size);

// Wallets do not watch an empty script or one that begins with OP_RETURN, so those bytes are not a leaf.
// The opcode is the first byte only: an OP_RETURN later in the script does not drop it.
bool watched_script(byte_span script) {
    return ! script.empty()
        && script.front() != static_cast<uint8_t>(machine::opcode::return_);
}

// A block hash is stored in the order the hash function writes, which is also the order on the wire.
// Display order is that sequence reversed, so the filter key is a prefix of the stored hash.
golomb_key key_from_hash(hash_digest const& hash) {
    golomb_key key{};
    std::copy_n(hash.begin(), key.size(), key.begin());
    return key;
}

void append_script_leaf(std::vector<script_leaf>& leaves, byte_span script, uint32_t tx_index, uint32_t index, script_leaf_kind kind) {
    if ( ! watched_script(script)) {
        return;
    }
    leaves.push_back(script_leaf{script_key(script), tx_index, index, kind});
}

void append_category_leaf(std::vector<script_leaf>& leaves, token_data_opt const& token, uint32_t tx_index, uint32_t index, script_leaf_kind kind) {
    // No token is an empty optional. A present token always has a category, whether or not its payload is valid.
    if ( ! token.has_value()) {
        return;
    }
    leaves.push_back(script_leaf{category_key(token->id), tx_index, index, kind});
}

// Reads the outputs `tx` spends, starting at `used`, and appends its leaves.
// The first transaction of a block is the coinbase: its input does not spend an output and takes nothing from `spent`.
std::expected<size_t, script_filter_error>
append_leaves(std::vector<script_leaf>& leaves, transaction const& tx, uint32_t tx_index, bool coinbase, std::span<output const> spent, size_t used) {
    if ( ! coinbase) {
        auto const& inputs = tx.inputs();
        for (size_t j = 0; j < inputs.size(); ++j) {
            if (used >= spent.size()) {
                return std::unexpected(script_filter_error::spent_outputs);
            }
            auto const& prev = spent[used];
            ++used;
            auto const index = static_cast<uint32_t>(j);
            append_script_leaf(leaves, prev.script().bytes(), tx_index, index, script_leaf_kind::spent);
            append_category_leaf(leaves, prev.token_data(), tx_index, index, script_leaf_kind::spent_category);
        }
    }

    auto const& outputs = tx.outputs();
    for (size_t j = 0; j < outputs.size(); ++j) {
        auto const& created = outputs[j];
        auto const index = static_cast<uint32_t>(j);
        append_script_leaf(leaves, created.script().bytes(), tx_index, index, script_leaf_kind::output);
        append_category_leaf(leaves, created.token_data(), tx_index, index, script_leaf_kind::output_category);
    }
    return used;
}

std::expected<std::vector<script_leaf>, script_filter_error>
collect_leaves(std::span<transaction const> txs, std::span<output const> spent, bool first_is_coinbase) {
    std::vector<script_leaf> leaves;
    size_t used = 0;
    for (size_t i = 0; i < txs.size(); ++i) {
        // Only the transaction at index 0 can be the coinbase. A later transaction spends an output even when its outpoint is null.
        auto const coinbase = first_is_coinbase && i == 0;
        auto const next = append_leaves(leaves, txs[i], static_cast<uint32_t>(i), coinbase, spent, used);
        if ( ! next) {
            return std::unexpected(next.error());
        }
        used = *next;
    }
    if (used != spent.size()) {
        return std::unexpected(script_filter_error::spent_outputs);
    }
    std::ranges::sort(leaves);
    return leaves;
}

std::expected<golomb_set, script_filter_error>
build_set(hash_digest const& hash, std::span<script_leaf const> leaves) {
    std::vector<data_chunk> items;
    items.reserve(leaves.size());
    for (auto const& leaf : leaves) {
        items.push_back(to_chunk(leaf.key));
    }
    // Equal keys are one item. The set reports one error for every failure to build.
    auto built = golomb_set::build(key_from_hash(hash), items);
    if ( ! built) {
        return std::unexpected(script_filter_error::set);
    }
    return std::move(*built);
}

} // namespace

hash_digest script_key(byte_span script) {
    return sha256_hash(script);
}

hash_digest category_key(hash_digest const& category) {
    uint8_t const tag = 0x01;
    return sha256_hash(byte_span(&tag, 1), category);
}

std::expected<std::vector<script_leaf>, script_filter_error>
script_leaves(block const& blk, std::span<output const> spent) {
    return collect_leaves(blk.transactions(), spent, true);
}

std::expected<script_filter, script_filter_error>
build_script_filter(block const& blk, std::span<output const> spent) {
    auto leaves = script_leaves(blk, spent);
    if ( ! leaves) {
        return std::unexpected(leaves.error());
    }
    auto set = build_set(blk.hash(), *leaves);
    if ( ! set) {
        return std::unexpected(set.error());
    }
    return script_filter{std::move(*set), script_leaves_root(*leaves)};
}

data_chunk to_data(script_filter const& filter) {
    auto const set_bytes = filter.set.to_data();
    data_chunk out;
    out.reserve(hash_size + set_bytes.size());
    out.insert(out.end(), filter.root.begin(), filter.root.end());
    out.insert(out.end(), set_bytes.begin(), set_bytes.end());
    return out;
}

std::expected<script_filter, script_filter_error>
parse_script_filter(byte_span data) {
    // Refused before the remainder is parsed: subspan throws when the offset runs past the buffer.
    if (data.size() < hash_size) {
        return std::unexpected(script_filter_error::too_short);
    }
    auto set = golomb_set::from_data(data.subspan(hash_size));
    if ( ! set) {
        return std::unexpected(script_filter_error::set);
    }
    hash_digest root{};
    std::copy_n(data.begin(), hash_size, root.begin());
    return script_filter{std::move(*set), root};
}

hash_digest script_filter_hash(script_filter const& filter) {
    return bitcoin_hash(to_data(filter));
}

hash_digest script_filter_header(script_filter const& filter, hash_digest const& previous_header) {
    auto const filter_hash = script_filter_hash(filter);
    return bitcoin_hash(splice(filter_hash, previous_header));
}

std::expected<golomb_set, script_filter_error>
build_tx_script_filter(hash_digest const& hash, std::span<transaction const> txs, std::span<output const> spent) {
    auto leaves = collect_leaves(txs, spent, false);
    if ( ! leaves) {
        return std::unexpected(leaves.error());
    }
    return build_set(hash, *leaves);
}

} // namespace kth::domain::chain
