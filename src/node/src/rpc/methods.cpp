// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <asio/awaitable.hpp>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/domain/config/network.hpp>
#include <kth/domain/message/transaction.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>

#include <kth/node/rpc/dispatch.hpp>
#include <kth/node/rpc/error.hpp>
#include <kth/node/mining/block_submit.hpp>
#include <kth/node/mining/job_store.hpp>
#include <kth/node/rpc/json.hpp>
#include <kth/node/rpc/mining.hpp>
#include <kth/node/rpc/query.hpp>

namespace kth::node::rpc {

namespace {

// How many blocks the connected chain has from the block at `height` with
// `hash` up to its tip, that block included; -1 when that block is not on the
// connected chain.
std::int64_t chain_confirmations(blockchain::block_chain const& chain,
                                 std::size_t height, hash_digest const& hash) {
    auto const view = chain.chain_view();
    if ( ! view || height > view->connected_tip_height) {
        return -1;
    }
    auto const on_chain = height == view->connected_tip_height
        ? std::expected<hash_digest, database::result_code>{view->tip_hash}
        : chain.get_block_hash(height);
    if ( ! on_chain || *on_chain != hash) {
        return -1;
    }
    return static_cast<std::int64_t>(view->connected_tip_height - height + 1);
}

// The median of the timestamps of the 11 blocks ending at header-index entry
// `idx`, or of all of them nearer genesis.
std::uint32_t median_time_past(blockchain::header_index const& index,
                               blockchain::header_index::index_t idx) {
    std::vector<std::uint32_t> times;
    for (int i = 0; i < 11 && idx != blockchain::header_index::null_index; ++i) {
        times.push_back(index.get_timestamp(idx));
        idx = index.get_parent_index(idx);
    }
    if (times.empty()) {
        return 0;
    }
    std::ranges::sort(times);
    return times[times.size() / 2];
}

// The getblock / getrawtransaction verbosity: a JSON boolean, or a number.
// `fallback` when absent; nullopt when it is neither.
std::optional<std::uint64_t> params_verbosity(std::string_view params, std::size_t index,
                                              std::uint64_t fallback) {
    if (auto const flag = params_bool(params, index, fallback != 0)) {
        return *flag ? std::max<std::uint64_t>(fallback, 1) : 0;
    }
    return params_uint(params, index);
}

// getblockcount -> the height of the most-work fully-validated block.
// C-API counterpart: kth_chain_sync_last_height (see docs/json-rpc.md).
::asio::awaitable<std::expected<std::string, rpc_error>>
get_block_count(method_context& ctx, request const& /*req*/) {
    auto const heights = co_await ctx.chain.fetch_last_height();
    if ( ! heights) {
        co_return std::unexpected(from_code(heights.error()));
    }

    writer w;
    w.value(static_cast<std::uint64_t>(heights->block));
    co_return w.str();
}

// getblocktemplatelight -> the next-block template minus the transactions, which
// are cached under `job_id` for a later submitblocklight to reassemble.
// C-API counterpart: kth_chain_async_fetch_mining_template (see docs/json-rpc.md).
::asio::awaitable<std::expected<std::string, rpc_error>>
get_block_template_light(method_context& ctx, request const& /*req*/) {
    auto const tmpl = co_await ctx.chain.fetch_mining_template(
        blockchain::default_coinbase_reserve_size);
    if ( ! tmpl) {
        co_return std::unexpected(from_code(tmpl.error()));
    }

    auto const job_id = ctx.jobs.add(tmpl->selection.txs);
    co_return render_mining_template(*tmpl, job_id);
}

// getmininginfo -> height, difficulty, mempool size, and network.
// C-API counterpart: kth_chain_async_fetch_mining_info (see docs/json-rpc.md).
::asio::awaitable<std::expected<std::string, rpc_error>>
get_mining_info(method_context& ctx, request const& /*req*/) {
    auto const info = co_await ctx.chain.fetch_mining_info();
    if ( ! info) {
        co_return std::unexpected(from_code(info.error()));
    }
    co_return render_mining_info(*info);
}

// submitblocklight -> reconstruct the full block from the miner's solved header +
// coinbase and the cached job selection, then submit it to the chain. Returns
// (Bitcoin submitblock semantics) null on accept, or a reject-reason string.
// C-API counterpart: kth_chain_async_organize_block (see docs/json-rpc.md).
::asio::awaitable<std::expected<std::string, rpc_error>>
submit_block_light(method_context& ctx, request const& req) {
    auto const args = params_strings(req.params);
    if (args.size() < 2 || args[0].empty() || args[1].empty()) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "submitblocklight requires [hexdata, job_id]"));
    }

    auto const raw = decode_base16(args[0]);
    if ( ! raw) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "hexdata is not valid hex"));
    }

    // The submitted block carries the solved header and just the coinbase.
    byte_reader reader(*raw);
    auto header_and_coinbase = domain::message::block::from_data(reader, 0u);
    if ( ! header_and_coinbase || header_and_coinbase->transactions().empty()) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "hexdata is not a decodable block with a coinbase"));
    }

    auto const job = ctx.jobs.get(args[1]);
    if ( ! job) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "job_id not found or expired"));
    }

    // Reassemble (coinbase followed by the cached selection) and submit.
    auto const ec = co_await mining::submit_block_light(
        ctx.chain,
        header_and_coinbase->header(),
        header_and_coinbase->transactions().front(),
        *job);

    writer w;
    if (ec) {
        w.value(ec.message()); // BIP22-style reject reason
    } else {
        w.value_null();        // accepted
    }
    co_return w.str();
}

// ---- blockchain query methods --------------------------------------------

// getbestblockhash -> the tip block hash.
// C-API counterpart: kth_chain_sync_block_hash (see docs/json-rpc.md).
::asio::awaitable<std::expected<std::string, rpc_error>>
get_best_block_hash(method_context& ctx, request const& /*req*/) {
    auto const heights = ctx.chain.get_last_heights();
    if ( ! heights) {
        co_return std::unexpected(make_error(error_code::internal_error,
            "chain height unavailable"));
    }
    auto const hash = ctx.chain.get_block_hash(heights->block);
    if ( ! hash) {
        co_return std::unexpected(make_error(error_code::internal_error,
            "best block hash unavailable"));
    }
    writer w;
    w.value(encode_hash(*hash));
    co_return w.str();
}

// getblockhash <height> -> the block hash at that height.
// C-API counterpart: kth_chain_sync_block_hash.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_block_hash(method_context& ctx, request const& req) {
    auto const height = params_uint(req.params, 0);
    if ( ! height) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "getblockhash requires [height] (a non-negative integer)"));
    }
    auto const hash = ctx.chain.get_block_hash(static_cast<std::size_t>(*height));
    if ( ! hash) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "block height out of range"));
    }
    writer w;
    w.value(encode_hash(*hash));
    co_return w.str();
}

// getdifficulty -> the difficulty of the next required work.
// C-API counterpart: kth_chain_sync_mining_info.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_difficulty(method_context& ctx, request const& /*req*/) {
    auto const info = co_await ctx.chain.fetch_mining_info();
    if ( ! info) {
        co_return std::unexpected(from_code(info.error()));
    }
    writer w;
    w.value(info->difficulty);
    co_return w.str();
}

// getblockchaininfo -> chain, height, headers, best block hash, difficulty.
// C-API counterpart: kth_chain_sync_mining_info (+ sync_block_hash).
::asio::awaitable<std::expected<std::string, rpc_error>>
get_blockchain_info(method_context& ctx, request const& /*req*/) {
    auto const info = co_await ctx.chain.fetch_mining_info();
    if ( ! info) {
        co_return std::unexpected(from_code(info.error()));
    }
    auto const heights = ctx.chain.get_last_heights();
    if ( ! heights) {
        co_return std::unexpected(make_error(error_code::internal_error,
            "chain height unavailable"));
    }
    auto const best = ctx.chain.get_block_hash(info->blocks);
    if ( ! best) {
        co_return std::unexpected(make_error(error_code::internal_error,
            "best block hash unavailable"));
    }
    co_return render_blockchain_info(
        domain::config::name(info->chain), info->blocks, heights->header,
        encode_hash(*best), info->difficulty);
}

// getrawtransaction <txid> [verbose] [blockhash] -> the wire-serialized
// transaction as hex. With a block hash it is read from that block; without
// one only the mempool can answer, since confirmed transactions are not
// indexed by txid (#491).
// C-API counterpart: kth_chain_sync_transaction (+ kth_chain_sync_block_by_hash).
::asio::awaitable<std::expected<std::string, rpc_error>>
get_raw_transaction(method_context& ctx, request const& req) {
    auto const args = params_strings(req.params);
    if (args.empty() || args[0].empty()) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "getrawtransaction requires [txid]"));
    }
    auto const hash = decode_hash(args[0]);
    if ( ! hash) {
        co_return std::unexpected(make_error(error_code::invalid_params, "invalid txid"));
    }

    auto const verbose = params_verbosity(req.params, 1, /*fallback*/ 0);
    if ( ! verbose) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "verbose must be a boolean or a number"));
    }
    auto const view = ctx.chain.chain_view();
    if ( ! view || ! view->state) {
        co_return std::unexpected(make_error(error_code::internal_error,
            "chain state unavailable"));
    }
    auto const render = [&](domain::chain::transaction const& tx,
                            std::optional<transaction_location> const& location) {
        if (*verbose != 0) {
            return render_transaction_verbose(tx, view->state->network(),
                view->state->enabled_flags(), location);
        }
        writer w;
        w.value(transaction_to_hex(tx));
        return w.str();
    };

    if (args.size() > 2 && ! args[2].empty()) {
        auto const block_hash = decode_hash(args[2]);
        if ( ! block_hash) {
            co_return std::unexpected(make_error(error_code::invalid_params, "invalid block hash"));
        }
        auto const block = co_await ctx.chain.fetch_block(*block_hash);
        if ( ! block) {
            co_return std::unexpected(make_error(error_code::invalid_address_or_key,
                "Block hash not found"));
        }
        auto const& [found, height] = *block;
        for (auto const& tx : found->transactions()) {
            if (tx.hash() == *hash) {
                co_return render(tx, transaction_location{*block_hash,
                    chain_confirmations(ctx.chain, height, *block_hash), found->header().timestamp()});
            }
        }
        co_return std::unexpected(make_error(error_code::invalid_address_or_key,
            "No such transaction found in the provided block"));
    }

    auto const result = co_await ctx.chain.fetch_transaction(*hash, /*require_confirmed*/ false);
    if ( ! result) {
        if (result.error() == error::not_found) {
            co_return std::unexpected(make_error(error_code::invalid_address_or_key,
                "No such mempool transaction; pass the block hash for a confirmed one"));
        }
        co_return std::unexpected(from_code(result.error()));
    }
    co_return render(*std::get<0>(*result), std::nullopt);
}

// getblock <blockhash> [verbosity=1] -> with verbosity 0 (or false) the
// wire-serialized block as hex; with 1 (or true) its fields and txids; with 2
// its fields and its transactions decoded.
// C-API counterpart: kth_chain_sync_block_by_hash.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_block(method_context& ctx, request const& req) {
    auto const args = params_strings(req.params);
    if (args.empty() || args[0].empty()) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "getblock requires [blockhash]"));
    }
    auto const hash = decode_hash(args[0]);
    if ( ! hash) {
        co_return std::unexpected(make_error(error_code::invalid_params, "invalid block hash"));
    }
    auto const verbosity = params_verbosity(req.params, 1, /*fallback*/ 1);
    if ( ! verbosity || *verbosity > 2) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "verbosity must be 0, 1, 2 or a boolean"));
    }
    auto const result = co_await ctx.chain.fetch_block(*hash);
    if ( ! result) {
        if (result.error() == error::not_found) {
            co_return std::unexpected(make_error(error_code::invalid_address_or_key,
                "Block not found"));
        }
        co_return std::unexpected(from_code(result.error()));
    }
    auto const& [block, height] = *result;
    if (*verbosity == 0) {
        writer w;
        w.value(block_to_hex(*block));
        co_return w.str();
    }

    auto const view = ctx.chain.chain_view();
    if ( ! view || ! view->state) {
        co_return std::unexpected(make_error(error_code::internal_error,
            "chain state unavailable"));
    }
    auto const& index = ctx.chain.headers();
    auto const idx = index.find(*hash);
    if (idx == blockchain::header_index::null_index) {
        co_return std::unexpected(make_error(error_code::invalid_address_or_key,
            "Block not found"));
    }
    block_location location{
        .height = height,
        .confirmations = chain_confirmations(ctx.chain, height, *hash),
        .median_time = median_time_past(index, idx),
        .chain_work = std::nullopt,
        .next_block_hash = std::nullopt};
    // The header index counts work from the genesis block's child on.
    if (auto const genesis = index.get_ancestor(idx, 0); genesis != blockchain::header_index::null_index) {
        location.chain_work = index.get_chain_work(idx) +
            domain::chain::header::proof(index.get_bits(genesis));
    }
    if (location.confirmations > 1) {
        if (auto const next = ctx.chain.get_block_hash(height + 1)) {
            location.next_block_hash = *next;
        }
    }
    co_return render_block_verbose(*block, location, *verbosity == 2,
        view->state->network(), view->state->enabled_flags());
}

// getblockheader <blockhash> [verbose=true] -> the header fields, or with
// verbose=false the 80-byte serialized header as hex.
// C-API counterpart: kth_chain_sync_block_header_by_hash.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_block_header(method_context& ctx, request const& req) {
    auto const args = params_strings(req.params);
    if (args.empty() || args[0].empty()) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "getblockheader requires [blockhash]"));
    }
    auto const hash = decode_hash(args[0]);
    if ( ! hash) {
        co_return std::unexpected(make_error(error_code::invalid_params, "invalid block hash"));
    }
    auto const verbose = params_bool(req.params, 1, /*fallback*/ true);
    if ( ! verbose) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "verbose must be a boolean"));
    }
    auto const result = co_await ctx.chain.fetch_block_header(*hash);
    if ( ! result) {
        co_return std::unexpected(from_code(result.error()));
    }
    if ( ! *verbose) {
        writer w;
        w.value(header_to_hex(*std::get<0>(*result)));
        co_return w.str();
    }

    auto const& [header, height] = *result;
    co_return render_block_header(*header, height, *hash);
}

// sendrawtransaction <hexstring> -> the accepted txid, or a reject error.
// C-API counterpart: kth_chain_sync_organize_transaction.
::asio::awaitable<std::expected<std::string, rpc_error>>
send_raw_transaction(method_context& ctx, request const& req) {
    auto const args = params_strings(req.params);
    if (args.empty() || args[0].empty()) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "sendrawtransaction requires [hexstring]"));
    }
    auto const raw = decode_base16(args[0]);
    if ( ! raw) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "hexstring is not valid hex"));
    }
    byte_reader reader(*raw);
    auto tx = domain::chain::transaction::from_data(reader, /*wire*/ true);
    if ( ! tx) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "transaction decode failed"));
    }

    auto const txid = encode_hash(tx->hash());
    auto const pooled = std::make_shared<domain::message::transaction>(std::move(*tx));
    auto const ec = co_await ctx.chain.organize(pooled);
    if (ec) {
        co_return std::unexpected(make_error(error_code::misc_error, ec.message()));
    }

    writer w;
    w.value(txid);
    co_return w.str();
}

// decoderawtransaction <hexstring> -> the transaction's fields, decoded for
// this node's network and current script rules.
// C-API counterpart: kth_chain_transaction_construct_from_data (+ its accessors).
::asio::awaitable<std::expected<std::string, rpc_error>>
decode_raw_transaction(method_context& ctx, request const& req) {
    auto const args = params_strings(req.params);
    if (args.empty() || args[0].empty()) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "decoderawtransaction requires [hexstring]"));
    }
    auto const raw = decode_base16(args[0]);
    if ( ! raw) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "hexstring is not valid hex"));
    }
    byte_reader reader(*raw);
    auto const tx = domain::chain::transaction::from_data(reader, /*wire*/ true);
    if ( ! tx || ! reader.is_exhausted()) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "transaction decode failed"));
    }
    auto const view = ctx.chain.chain_view();
    if ( ! view || ! view->state) {
        co_return std::unexpected(make_error(error_code::internal_error,
            "chain state unavailable"));
    }
    co_return render_transaction(*tx, view->state->network(), view->state->enabled_flags());
}

// ---- mempool query methods -----------------------------------------------

// getrawmempool -> the txids currently in the mempool.
// C-API counterpart: kth_chain_get_mempool_txids.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_raw_mempool(method_context& ctx, request const& /*req*/) {
    co_return render_hash_list(ctx.chain.get_mempool_txids());
}

// getmempoolinfo -> mempool size, byte total, and fee total.
// C-API counterpart: kth_chain_get_mempool_info.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_mempool_info(method_context& ctx, request const& /*req*/) {
    co_return render_mempool_info(ctx.chain.get_mempool_info());
}

// getmempoolentry <txid> -> the entry's fee/size/time plus depends/spentby.
// C-API counterpart: kth_chain_get_mempool_entry.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_mempool_entry(method_context& ctx, request const& req) {
    auto const args = params_strings(req.params);
    if (args.empty() || args[0].empty()) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "getmempoolentry requires [txid]"));
    }
    auto const hash = decode_hash(args[0]);
    if ( ! hash) {
        co_return std::unexpected(make_error(error_code::invalid_params, "invalid txid"));
    }
    auto const entry = ctx.chain.get_mempool_entry(*hash);
    if ( ! entry) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "transaction not in mempool"));
    }
    co_return render_mempool_entry(*entry,
        ctx.chain.get_mempool_depends(*hash), ctx.chain.get_mempool_spentby(*hash));
}

// getmempoolancestors <txid> -> in-mempool ancestor txids.
// C-API counterpart: kth_chain_get_mempool_ancestors.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_mempool_ancestors(method_context& ctx, request const& req) {
    auto const args = params_strings(req.params);
    if (args.empty() || args[0].empty()) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "getmempoolancestors requires [txid]"));
    }
    auto const hash = decode_hash(args[0]);
    if ( ! hash) {
        co_return std::unexpected(make_error(error_code::invalid_params, "invalid txid"));
    }
    co_return render_hash_list(ctx.chain.get_mempool_ancestors(*hash));
}

// getmempooldescendants <txid> -> in-mempool descendant txids.
// C-API counterpart: kth_chain_get_mempool_descendants.
::asio::awaitable<std::expected<std::string, rpc_error>>
get_mempool_descendants(method_context& ctx, request const& req) {
    auto const args = params_strings(req.params);
    if (args.empty() || args[0].empty()) {
        co_return std::unexpected(make_error(error_code::invalid_params,
            "getmempooldescendants requires [txid]"));
    }
    auto const hash = decode_hash(args[0]);
    if ( ! hash) {
        co_return std::unexpected(make_error(error_code::invalid_params, "invalid txid"));
    }
    co_return render_hash_list(ctx.chain.get_mempool_descendants(*hash));
}

} // namespace

void register_builtin_methods(dispatcher& d) {
    d.add("getblockcount", get_block_count);
    d.add("getbestblockhash", get_best_block_hash);
    d.add("getblockhash", get_block_hash);
    d.add("getblockheader", get_block_header);
    d.add("getdifficulty", get_difficulty);
    d.add("getblockchaininfo", get_blockchain_info);
    d.add("getrawtransaction", get_raw_transaction);
    d.add("getblock", get_block);
    d.add("sendrawtransaction", send_raw_transaction);
    d.add("decoderawtransaction", decode_raw_transaction);
    d.add("getrawmempool", get_raw_mempool);
    d.add("getmempoolinfo", get_mempool_info);
    d.add("getmempoolentry", get_mempool_entry);
    d.add("getmempoolancestors", get_mempool_ancestors);
    d.add("getmempooldescendants", get_mempool_descendants);
    d.add("getblocktemplatelight", get_block_template_light);
    d.add("getmininginfo", get_mining_info);
    d.add("submitblocklight", submit_block_light);
}

} // namespace kth::node::rpc
