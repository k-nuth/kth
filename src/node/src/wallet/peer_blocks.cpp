// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Wallet support answered from peers: whole blocks. Each is checked against
// its header, and the stored filters against it. A block a wallet needs is
// downloaded with decoys from its bucket of 144 heights, derived from a seed
// the store keeps, so the same block brings the same decoys in every
// session and sessions cannot be intersected; the bucket at the tip, not
// whole yet, comes all of it. Every new block at the tip is downloaded.

#include "peer_source.hpp"

#include <algorithm>

#include <spdlog/spdlog.h>

#include <kth/node/wallet/blocks.hpp>

namespace kth::node::wallet::detail {

using network::raw_message;
using domain::chain::point;

void peer_impl::cache_insert(hash_digest const& hash, block_ptr const& blk, size_t bytes) {
    if (cache.contains(hash)) return;
    cache.emplace(hash, blk);
    cache_order.emplace_back(hash, bytes);
    cache_bytes += bytes;
    while (cache_bytes > settings.block_cache_bytes && cache_order.size() > 1) {
        auto const [old, size] = cache_order.front();
        cache_order.pop_front();
        cache.erase(old);
        cache_bytes -= size;
    }
}

// A filter that does not fit the block it was served for: the block is
// proven by its header, so the filter is the one that lies.
void peer_impl::check_filters(uint32_t height, domain::chain::block const& blk, hash_digest const& hash) {
    if (scripts && scripts->block(height) == hash) {
        auto const filter = scripts->read(height);
        if (filter) {
            auto const checked = check_script_filter(blk, *filter);
            if ( ! checked) {
                drop_filters(height, script_type, "a filter that does not fit block " + std::to_string(height));
                return;
            }
            // What the highest block checked shows.
            if (height >= transaction_keys_height && transaction_keys != checked->transaction_keys) {
                transaction_keys = checked->transaction_keys;
                spdlog::info("[wallet] Peers' script filters {} transaction keys", *transaction_keys ? "hold" : "do not hold");
            }
            transaction_keys_height = std::max(transaction_keys_height, height);
        }
    }
    if (prefixes && prefixes->block(height) == hash) {
        auto const filter = prefixes->read(height);
        if (filter && check_input_prefix_filter(blk, *filter)) {
            drop_filters(height, prefix_type, "an input prefix filter that does not fit block " + std::to_string(height));
        }
    }
}

::asio::awaitable<std::expected<block_ptr, std::error_code>> peer_impl::fetch_block(uint32_t height, bool check) {
    // Peers serve no body for the genesis block; it is known.
    if (height == 0) co_return std::make_shared<domain::chain::block const>(genesis_of(settings.network));
    auto const hash = hash_at(height);
    if ( ! hash) co_return std::unexpected(error::not_found);
    if (auto const cached = cache.find(*hash); cached != cache.end()) co_return cached->second;
    // Another download of it is under way: woken when it ends.
    if (auto const busy = inflight.find(*hash); busy != inflight.end()) {
        auto const signal = std::make_shared<kth::select_channel<int>>(1);
        busy->second.push_back(signal);
        (void)co_await acquire(*signal);
        if (auto const cached = cache.find(*hash); cached != cache.end()) co_return cached->second;
        if (stopping) co_return std::unexpected(error::service_stopped);
    }
    inflight.try_emplace(*hash);
    struct finish {
        peer_impl& self;
        hash_digest hash;
        ~finish() {
            auto const found = self.inflight.find(hash);
            if (found == self.inflight.end()) return;
            for (auto const& signal : found->second) (void)signal->try_send(0);
            self.inflight.erase(found);
        }
    } guard{*this, *hash};

    std::error_code last = error::not_found;
    for (size_t attempt = 0; attempt < 4 && ! stopping; ++attempt) {
        // Over a connection of its own, unless isolation is off: the peers
        // that serve headers and filters do not see which blocks follow.
        std::shared_ptr<peer_link> link;
        if (settings.isolate_downloads) {
            link = co_await connect_isolated();
        } else {
            link = next_link();
        }
        if ( ! link) {
            last = error::channel_stopped;
            continue;
        }
        auto const version = link->session->negotiated_version();
        auto const ask = message::get_data::create(hash_list{*hash}, type_id::block);
        if ( ! ask) co_return std::unexpected(error::operation_failed);
        auto const wanted = *hash;
        auto answer = co_await request(link, *ask, [wanted, version](raw_message const& m) {
            auto const command = m.heading.command();
            if (command == message::block::command && m.payload.size() >= header_size &&
                bitcoin_hash(byte_span(m.payload.data(), header_size)) == wanted) return step::done;
            if (command == message::not_found::command) {
                byte_reader reader(m.payload);
                auto const missing = message::not_found::from_data(reader, version);
                if (missing && std::ranges::any_of(missing->inventories(), [&](auto const& item) {
                        return item.type() == type_id::block && item.hash() == wanted; })) return step::done;
            }
            return step::skip;
        });
        if (link->isolated) link->session->stop();
        if ( ! answer) {
            last = answer.error();
            continue;
        }
        auto const& raw = answer->back();
        if (raw.heading.command() != message::block::command) {
            last = error::not_found;
            continue;
        }
        byte_reader reader(raw.payload);
        auto body = message::block::from_data(reader, version);
        if ( ! body || reader.remaining_size() != 0) {
            last = error::bad_stream;
            continue;
        }
        auto blk = std::make_shared<domain::chain::block const>(domain::chain::block(std::move(*body)));
        if (auto const ec = check_block(*blk, *hash)) {
            last = ec;
            continue;
        }
        if (check) check_filters(height, *blk, *hash);
        cache_insert(*hash, blk, raw.payload.size());
        ++blocks_downloaded;
        co_return blk;
    }
    co_return std::unexpected(last);
}

std::vector<uint32_t> peer_impl::decoys_for(uint32_t height) const {
    std::vector<uint32_t> out;
    if (settings.decoy_blocks == 0 || height == 0) return out;
    auto const tip = header_tip();
    auto const base = (height / decoy_bucket) * decoy_bucket;
    auto const low = std::max<uint32_t>(1, base);
    auto const high = base + decoy_bucket - 1;
    // The bucket at the tip is not whole: its decoys would move as it fills,
    // so all of it comes.
    if (high > tip) {
        for (auto at = low; at <= tip; ++at) {
            if (at != height) out.push_back(at);
        }
        return out;
    }
    std::set<uint32_t> chosen;
    for (uint32_t draw = 0; chosen.size() < settings.decoy_blocks && draw < 64 * settings.decoy_blocks; ++draw) {
        data_chunk material(decoy_seed);
        for (auto const value : {height, draw}) {
            for (int shift = 0; shift < 32; shift += 8) material.push_back(uint8_t(value >> shift));
        }
        auto const digest = sha256_hash(material);
        auto const value = uint32_t(digest[0]) | uint32_t(digest[1]) << 8 | uint32_t(digest[2]) << 16 | uint32_t(digest[3]) << 24;
        auto const candidate = low + value % (high - low + 1);
        if (candidate != height) chosen.insert(candidate);
    }
    return {chosen.begin(), chosen.end()};
}

// Following the tip: every new block is downloaded (all of them, so peers
// learn nothing from which), and the mempool copy follows it.
::asio::awaitable<std::error_code> peer_impl::follow_tip() {
    if ( ! scripts || ! scripts->tip()) co_return error::success;
    auto const tip = *scripts->tip();
    uint32_t start = tip;
    if (live_tip && *live_tip >= tip) co_return error::success;
    if (live_tip && tip - *live_tip <= live_window) {
        start = *live_tip + 1;
    } else if (live_tip) {
        // Too many blocks went by: the copy starts over from relay.
        pool.clear();
    }
    std::vector<uint32_t> heights;
    for (auto height = start; height <= tip; ++height) heights.push_back(height);
    co_return co_await each_block(heights, [this](uint32_t height, block_ptr const& blk) -> std::error_code {
        if ( ! pool.remove_for_block(blk->transactions()).empty()) {
            resolve_dsproofs(true);
            changes.bump();
        }
        std::vector<point> created;
        for (auto const& tx : blk->transactions()) {
            auto const txid = tx.hash();
            for (uint32_t vout = 0; vout < tx.outputs().size(); ++vout) {
                recent_outputs.insert_or_assign(point(txid, vout), tx.outputs()[vout].value());
                created.emplace_back(txid, vout);
            }
        }
        recent_created.push_back(std::move(created));
        while (recent_created.size() > recent_blocks) {
            for (auto const& old : recent_created.front()) recent_outputs.erase(old);
            recent_created.pop_front();
        }
        live_tip = height;
        return {};
    });
}

} // namespace kth::node::wallet::detail
