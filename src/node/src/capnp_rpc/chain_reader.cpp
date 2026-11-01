// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/capnp_rpc/reader.hpp>

#include <algorithm>
#include <cstring>
#include <utility>

#include <asio/co_spawn.hpp>
#include <asio/use_awaitable.hpp>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth::node::capnp_rpc {

namespace {

failure from_code(kth::code const& ec) {
    if (ec == error::not_found) {
        return {status::not_found, "not found"};
    }
    if (ec == error::service_stopped) {
        return {status::unavailable, "node is stopping"};
    }
    return {status::internal, ec.message()};
}

using flat_read = std::expected<std::vector<data_chunk>, database::result_code>;

// Runs on the chain's own worker pool. Everything it touches is passed by value
// or is the chain, which outlives the (suspended) caller that awaits it.
::asio::awaitable<flat_read> read_block_file(blockchain::block_chain& chain, uint32_t height) {
    co_return chain.fetch_blocks_raw(height, height);
}

static_assert(std::is_same_v<std::shared_ptr<domain::message::transaction const>, transaction_const_ptr>,
    "to_transaction_data must take the pointer type fetch_transaction returns");

} // namespace

transaction_data to_transaction_data(
    std::tuple<std::shared_ptr<domain::message::transaction const>, size_t, size_t> const& confirmed) {
    auto const& [tx, index, height] = confirmed;   // (transaction, index, height)
    return transaction_data{to_data_chunk(*tx, /*wire*/ true),
        transaction_data::confirmed_position{height, index}};
}

chain_reader::chain_reader(blockchain::block_chain& chain)
    : chain_(chain) {}

// The reader runs on the network's pool, so it reads only what the chain makes
// safe from any thread: the published chain view, the header index and the
// fetch_* queries, never the get_* readers.

namespace {

// Whether `hash` is the active chain's block at `height`.
bool active_at(blockchain::block_chain const& chain, size_t height, hash_digest const& hash) {
    auto const& headers = chain.headers();
    auto const index = headers.active_at(int32_t(height));
    return index != headers.null_index && headers.get_hash(index) == hash;
}

} // namespace

::asio::awaitable<result<chain_info>> chain_reader::get_chain_info() {
    // One publication: the connected tip's height and hash read together.
    auto const view = chain_.chain_view();
    if ( ! view) {
        co_return std::unexpected(failure{status::unavailable, "the chain has not published a tip yet"});
    }
    auto const block_height = uint64_t(view->connected_tip_height);
    auto const headers = chain_.headers().active_tip_height();
    co_return chain_info{block_height, std::max(block_height, uint64_t(std::max(headers, 0))),
        view->tip_hash};
}

::asio::awaitable<result<block_data>> chain_reader::get_block(hash_digest const& hash) {
    // The by-hash block fetch of the chain is not backed by the flat-file block
    // store (the block store is addressed by height), so resolve the height and
    // read the stored wire bytes of that height instead.
    auto const known = co_await chain_.fetch_block_height(hash);
    if ( ! known) {
        co_return std::unexpected(from_code(known.error()));
    }
    auto const height = *known;

    // Only validated, connected blocks of the active chain are served: a header
    // above the connected tip has no validated body, and a header on a side
    // branch shares its height with an active block.
    auto const view = chain_.chain_view();
    if ( ! view) {
        co_return std::unexpected(failure{status::unavailable, "the chain has not published a tip yet"});
    }
    if (height > view->connected_tip_height) {
        co_return std::unexpected(failure{status::not_found,
            "block is not part of the connected chain"});
    }
    if ( ! active_at(chain_, height, hash)) {
        co_return std::unexpected(failure{status::not_found, "block is on a side branch"});
    }

    // The flat-file read is blocking disk I/O: run it on the chain's worker pool
    // and suspend this strand meanwhile. The await is the ownership: this frame
    // (and with it the hash and chain reference) cannot be destroyed or resumed
    // until the pool task has finished, even if the operation is cancelled.
    auto raw = co_await ::asio::co_spawn(chain_.thread_pool().get_executor(),
        read_block_file(chain_, uint32_t(height)), ::asio::use_awaitable);
    if ( ! raw) {
        if (chain_.stopped()) {
            co_return std::unexpected(failure{status::unavailable, "node is stopping"});
        }
        if (raw.error() == database::result_code::key_not_found) {
            co_return std::unexpected(failure{status::not_found, "block data not stored"});
        }
        co_return std::unexpected(failure{status::internal, "block read failed"});
    }
    if (raw->size() != 1 || raw->front().size() < 80) {
        co_return std::unexpected(failure{status::internal, "block read failed"});
    }

    // Back on the owner strand. The height was resolved before the read, and a
    // reorganization can have replaced that height meanwhile: the bytes must be
    // the requested block, and it must still be the active one. Otherwise a
    // different block would be returned under the requested hash.
    auto const& wire = raw->front();
    if (bitcoin_hash(byte_span{wire.data(), 80}) != hash) {
        co_return std::unexpected(failure{status::not_found, "block changed during read"});
    }
    if ( ! active_at(chain_, height, hash)) {
        if (chain_.stopped()) {
            co_return std::unexpected(failure{status::unavailable, "node is stopping"});
        }
        co_return std::unexpected(failure{status::not_found, "block left the active chain during read"});
    }
    co_return block_data{height, std::move(raw->front())};
}

::asio::awaitable<result<transaction_data>> chain_reader::get_transaction(hash_digest const& hash) {
    // Confirmed first: the node has no transaction index yet (issue #491), so
    // this reports not_found today and starts answering when the index lands.
    auto const confirmed = co_await chain_.fetch_transaction(hash, /*require_confirmed*/ true);
    if (confirmed) {
        co_return to_transaction_data(*confirmed);
    }
    if (confirmed.error() != error::not_found) {
        co_return std::unexpected(from_code(confirmed.error()));
    }

    auto const pooled = co_await chain_.fetch_unconfirmed_transaction(hash);
    if ( ! pooled) {
        co_return std::unexpected(from_code(pooled.error()));
    }
    co_return transaction_data{to_data_chunk(**pooled, /*wire*/ true), std::nullopt};
}

} // namespace kth::node::capnp_rpc
