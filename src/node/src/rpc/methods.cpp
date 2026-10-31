// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>

#include <asio/awaitable.hpp>

#include <kth/blockchain/interface/block_chain.hpp>
#if defined(KTH_CURRENCY_BCH)
#include <kth/blockchain/tick_schedule.hpp>
#endif
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
#if defined(KTH_CURRENCY_BCH)
std::expected<chain_progress, rpc_error> progress_for(
    blockchain::block_chain const& chain, hash_digest const& hash, size_t height) {
    auto const& index = chain.headers();
    auto const idx = index.find(hash);
    if (idx == blockchain::header_index::null_index) {
        return std::unexpected(make_error(error_code::internal_error,
            "block progress unavailable"));
    }
    auto const published = chain.chain_view();
    if (!published) {
        return std::unexpected(make_error(error_code::internal_error,
            "chain context unavailable"));
    }
    blockchain::validate_header validator(chain.chain_settings(), published->state->network());
    auto const schedule = blockchain::tick_schedule_for_tip(validator, index, idx);
    if (!schedule) {
        return std::unexpected(from_code(schedule.error()));
    }
    return chain_progress{schedule->height_to_tick(height),
        schedule->block_interval(height), schedule->next_block_interval(height)};
}
#endif

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
#if defined(KTH_CURRENCY_BCH)
    // The template's parent fixes its branch even if the active tip changes.
    auto const progress = progress_for(ctx.chain, tmpl->previous_block_hash, tmpl->height);
    if (!progress) {
        co_return std::unexpected(progress.error());
    }
    co_return render_mining_template(*tmpl, job_id, *progress);
#else
    co_return render_mining_template(*tmpl, job_id);
#endif
}

// getmininginfo -> height, difficulty, mempool size, and network.
// C-API counterpart: kth_chain_async_fetch_mining_info (see docs/json-rpc.md).
::asio::awaitable<std::expected<std::string, rpc_error>>
get_mining_info(method_context& ctx, request const& /*req*/) {
#if defined(KTH_CURRENCY_BCH)
    auto const published = ctx.chain.chain_view();
#endif
    auto const info = co_await ctx.chain.fetch_mining_info();
    if ( ! info) {
        co_return std::unexpected(from_code(info.error()));
    }
#if defined(KTH_CURRENCY_BCH)
    if (!published || ctx.chain.chain_view() != published) {
        co_return std::unexpected(make_error(error_code::internal_error,
            "mining progress unavailable during chain transition"));
    }
    auto const progress = progress_for(ctx.chain, published->tip_hash, info->blocks);
    if (!progress) {
        co_return std::unexpected(progress.error());
    }
    co_return render_mining_info(*info, *progress);
#else
    co_return render_mining_info(*info);
#endif
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
#if defined(KTH_CURRENCY_BCH)
    auto const published = ctx.chain.chain_view();
#endif
    auto const info = co_await ctx.chain.fetch_mining_info();
    if ( ! info) {
        co_return std::unexpected(from_code(info.error()));
    }
    auto const heights = ctx.chain.get_last_heights();
    if ( ! heights) {
        co_return std::unexpected(make_error(error_code::internal_error,
            "chain height unavailable"));
    }
#if defined(KTH_CURRENCY_BCH)
    if (!published || ctx.chain.chain_view() != published) {
        co_return std::unexpected(make_error(error_code::internal_error,
            "chain progress unavailable during chain transition"));
    }
    auto const best = std::optional<hash_digest>{published->tip_hash};
#else
    auto const best = ctx.chain.get_block_hash(info->blocks);
    if ( ! best) {
        co_return std::unexpected(make_error(error_code::internal_error,
            "best block hash unavailable"));
    }
#endif
#if defined(KTH_CURRENCY_BCH)
    auto const progress = progress_for(ctx.chain, *best, info->blocks);
    if (!progress) {
        co_return std::unexpected(progress.error());
    }
    co_return render_blockchain_info(
        domain::config::name(info->chain), info->blocks, heights->header,
        encode_hash(*best), info->difficulty, *progress);
#else
    co_return render_blockchain_info(
        domain::config::name(info->chain), info->blocks, heights->header,
        encode_hash(*best), info->difficulty);
#endif
}

// getrawtransaction <txid> -> the wire-serialized transaction as hex.
// C-API counterpart: kth_chain_sync_transaction.
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
    auto const result = co_await ctx.chain.fetch_transaction(*hash, /*require_confirmed*/ false);
    if ( ! result) {
        co_return std::unexpected(from_code(result.error()));
    }
    auto const& tx = std::get<0>(*result);
    writer w;
    w.value(transaction_to_hex(*tx));
    co_return w.str();
}

// getblock <blockhash> -> the wire-serialized block as hex.
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
    auto const result = co_await ctx.chain.fetch_block(*hash);
    if ( ! result) {
        co_return std::unexpected(from_code(result.error()));
    }
    auto const& block = std::get<0>(*result);
    writer w;
    w.value(block_to_hex(*block));
    co_return w.str();
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
#if defined(KTH_CURRENCY_BCH)
    auto const progress = progress_for(ctx.chain, *hash, height);
    if (!progress) {
        co_return std::unexpected(progress.error());
    }
    co_return render_block_header(*header, height, *hash, *progress);
#else
    co_return render_block_header(*header, height, *hash);
#endif
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
