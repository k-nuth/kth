// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/protocols/protocol_block_in.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <memory>
#include <string>

//Note(fernando): to force a debugger breakpoint we can use
//      asm("int $3");

#define FMT_HEADER_ONLY 1
#include <fmt/core.h>

#include <kth/blockchain.hpp>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/node/define.hpp>
#include <kth/node/full_node.hpp>

namespace kth::node {

#define NAME "block_in"
#define CLASS protocol_block_in

using namespace kth::blockchain;
using namespace kth::domain::message;
using namespace kth::network;
using namespace std::chrono;
using namespace std::placeholders;

constexpr
uint64_t get_compact_blocks_version() {
#if defined(KTH_CURRENCY_BCH)
        return 1;
#else
        return 2;
#endif
}

protocol_block_in::protocol_block_in(full_node& node, channel::ptr channel, safe_chain& chain)
  : protocol_timer(node, channel, false, NAME),
    node_(node),
    chain_(chain),
    block_latency_(node.node_settings().block_latency()),

    // TODO: move send_headers to a derived class protocol_block_in_70012.
    headers_from_peer_(negotiated_version() >= version::level::bip130),

    // TODO: move send_compact to a derived class protocol_block_in_70014.
    compact_from_peer_(negotiated_version() >= version::level::bip152),

    compact_blocks_high_bandwidth_set_(false),

    // This patch is treated as integral to basic block handling.
    blocks_from_peer_(
        negotiated_version() > version::level::no_blocks_end ||
        negotiated_version() < version::level::no_blocks_start),

    CONSTRUCT_TRACK(protocol_block_in)
{}

// Start.
//-----------------------------------------------------------------------------

void protocol_block_in::start() {
    // Use timer to drop slow peers.
    protocol_timer::start(block_latency_, BIND1(handle_timeout, _1));

    // TODO: move headers to a derived class protocol_block_in_31800.
    SUBSCRIBE2(headers, handle_receive_headers, _1, _2);

    // TODO: move not_found to a derived class protocol_block_in_70001.
    SUBSCRIBE2(not_found, handle_receive_not_found, _1, _2);
    SUBSCRIBE2(inventory, handle_receive_inventory, _1, _2);
    SUBSCRIBE2(block, handle_receive_block, _1, _2);

    SUBSCRIBE2(compact_block, handle_receive_compact_block, _1, _2);
    SUBSCRIBE2(block_transactions, handle_receive_block_transactions, _1, _2);

    // TODO: move send_headers to a derived class protocol_block_in_70012.
    if (headers_from_peer_) {
        // Ask peer to send headers vs. inventory block announcements.
        SEND2(send_headers{}, handle_send, _1, send_headers::command);
    }

    // TODO: move send_compact to a derived class protocol_block_in_70014.
    if (compact_from_peer_) {
        if (chain_.is_stale()) {
            //force low bandwidth
            spdlog::debug("[node] The chain is stale, send sendcmcpt low bandwidth [{}]", authority());
            SEND2((send_compact{false, get_compact_blocks_version()}), handle_send, _1, send_compact::command);
        } else {
            spdlog::debug("[node] The chain is not stale, send sendcmcpt with configured setting [{}]", authority());
            SEND2((send_compact{node_.node_settings().compact_blocks_high_bandwidth, get_compact_blocks_version()}), handle_send, _1, send_compact::command);
            compact_blocks_high_bandwidth_set_ = node_.node_settings().compact_blocks_high_bandwidth;
        }
    }

    send_get_blocks(null_hash);
}

// Send get_[headers|blocks] sequence.
//-----------------------------------------------------------------------------

void protocol_block_in::send_get_blocks(hash_digest const& stop_hash) {
    auto const heights = block::locator_heights(node_.top_block().height());
    chain_.fetch_block_locator(heights, BIND3(handle_fetch_block_locator, _1, _2, stop_hash));
}

void protocol_block_in::handle_fetch_block_locator(code const& ec, get_headers_ptr message, hash_digest const& stop_hash) {
    if (stopped(ec)) {
        return;
    }

    if (ec) {
        spdlog::error("[node] Internal failure generating block locator for [{}] {}", authority(), ec.message());
        stop(ec);
        return;
    }

    if (message->start_hashes().empty()) {
        return;
    }

    auto const& last_hash = message->start_hashes().front();

    // TODO: move get_headers to a derived class protocol_block_in_31800.
    auto const use_headers = negotiated_version() >= version::level::headers;
    auto const request_type = (use_headers ? "headers" : "inventory");

    if (stop_hash == null_hash) {
        spdlog::debug("[node] Ask [{}] for {} after [{}]", authority(), request_type, encode_hash(last_hash));
    } else {
        spdlog::debug("[node] Ask [{}] for {} from [{}] through [{}]", authority(), request_type, encode_hash(last_hash), encode_hash(stop_hash));
    }

    message->set_stop_hash(stop_hash);

    if (use_headers) {
        SEND2(*message, handle_send, _1, message->command);
    } else {
        SEND2(static_cast<get_blocks>(*message), handle_send, _1, message->command);
    }
}

// Receive headers|inventory sequence.
//-----------------------------------------------------------------------------

// TODO: move headers to a derived class protocol_block_in_31800.
// This originates from send_header->annoucements and get_headers requests, or
// from an unsolicited announcement. There is no way to distinguish.
bool protocol_block_in::handle_receive_headers(code const& ec, headers_const_ptr message) {
    if (stopped(ec)) {
        return false;
    }

    // We don't want to request a batch of headers out of order.
    if ( ! message->is_sequential()) {
        spdlog::warn("[node] Block headers out of order from [{}].", authority());
        stop(error::channel_stopped);
        return false;
    }

    // There is no benefit to this use of headers, in fact it is suboptimal.
    // In v3 headers will be used to build block tree before getting blocks.
    auto const response = std::make_shared<get_data>();

    if (compact_from_peer_) {
        message->to_inventory(response->inventories(), inventory::type_id::compact_block);
    } else {
        message->to_inventory(response->inventories(), inventory::type_id::block);
    }

    // Remove hashes of blocks that we already have.
    chain_.filter_blocks(response, BIND2(send_get_data, _1, response));
    return true;
}

// This originates from default annoucements and get_blocks requests, or from
// an unsolicited announcement. There is no way to distinguish.
bool protocol_block_in::handle_receive_inventory(code const& ec, inventory_const_ptr message) {
    if (stopped(ec)) {
        return false;
    }

    auto const response = std::make_shared<get_data>();

    if (compact_from_peer_) {
        message->reduce(response->inventories(), inventory::type_id::compact_block);
    } else {
        message->reduce(response->inventories(), inventory::type_id::block);
    }

    // Remove hashes of blocks that we already have.
    chain_.filter_blocks(response, BIND2(send_get_data, _1, response));
    return true;
}

void protocol_block_in::send_get_data(code const& ec, get_data_ptr message) {
    if (stopped(ec)) {
        return;
    }

    if (ec) {
        spdlog::error("[node] Internal failure filtering block hashes for [{}] {}", authority(), ec.message());
        stop(ec);
        return;
    }

    if (message->inventories().empty()) {
        return;
    }

    if (compact_from_peer_) {
        if (node_.node_settings().compact_blocks_high_bandwidth) {
            if ( ! compact_blocks_high_bandwidth_set_ && ! chain_.is_stale() ) {
                spdlog::info("[node] The chain is not stale, send sendcmcpt with high bandwidth [{}]", authority());
                SEND2((send_compact{true, get_compact_blocks_version()}), handle_send, _1, send_compact::command);
                compact_blocks_high_bandwidth_set_ = true;
            }
        }
    }

    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    mutex.lock_upgrade();
    auto const fresh = backlog_.empty();
    mutex.unlock_upgrade_and_lock();
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

    // Enqueue the block inventory behind the preceding block inventory.
    for (auto const& inventory: message->inventories()){
        if (inventory.type() == inventory::type_id::block) {
            backlog_.push(inventory.hash());
        } else if (inventory.type() == inventory::type_id::compact_block) {
            backlog_.push(inventory.hash());
        }
    }

    mutex.unlock();
    ///////////////////////////////////////////////////////////////////////////

    // There was no backlog so the timer must be started now.
    if (fresh) {
        reset_timer();
    }

    // inventory|headers->get_data[blocks]
    SEND2(*message, handle_send, _1, message->command);
}

// Receive not_found sequence.
//-----------------------------------------------------------------------------

// TODO: move not_found to a derived class protocol_block_in_70001.
bool protocol_block_in::handle_receive_not_found(code const& ec, not_found_const_ptr message) {
    if (stopped(ec)) {
        return false;
    }

    if (ec) {
        spdlog::debug("[node] Failure getting block not_found from [{}] {}", authority(), ec.message());
        stop(ec);
        return false;
    }

    hash_list hashes;
    message->to_hashes(hashes, inventory::type_id::block);

    for (auto const& hash : hashes) {
        spdlog::debug("[node] Block not_found [{}] from [{}]", encode_hash(hash), authority());
    }

    // The peer cannot locate one or more blocks that it told us it had.
    // This only results from reorganization assuming peer is proper.
    // Drop the peer so next channgel generates a new locator and backlog.
    if ( ! hashes.empty()) {
        stop(error::channel_stopped);
    }

    return true;
}

// Receive block sequence.
//-----------------------------------------------------------------------------

void protocol_block_in::organize_block(block_const_ptr message) {
    message->validation.originator = nonce();
    chain_.organize(message, BIND2(handle_store_block, _1, message));
}

bool protocol_block_in::handle_receive_block(code const& ec, block_const_ptr message) {
    if (stopped(ec)) {
        return false;
    }

    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    mutex.lock();

    auto matched = ! backlog_.empty() && backlog_.front() == message->hash();

    if (matched) {
        backlog_.pop();
    }

    // Empty after pop means we need to make a new request.
    auto const cleared = backlog_.empty();

    mutex.unlock();
    ///////////////////////////////////////////////////////////////////////////

    // If a peer sends a block unannounced we drop the peer - always. However
    // it is common for block announcements to cause block requests to be sent
    // out of backlog order due to interleaving of threads. This results in
    // channel drops during initial block download but not after sync. The
    // resolution to this issue is use of headers-first sync, but short of that
    // the current implementation performs well and drops peers no more
    // frequently than block announcements occur during initial block download,
    // and not typically after it is complete.
    if ( ! matched) {
        spdlog::debug("[node] Block [{}] unexpected or out of order from [{}]", encode_hash(message->hash()), authority());
        stop(error::channel_stopped);
        return false;
    }

    // message->validation.originator = nonce();
    // chain_.organize(message, BIND2(handle_store_block, _1, message));
    organize_block(message);

    // Sending a new request will reset the timer upon inventory->get_data, but
    // we need to time out the lack of response to those requests when stale.
    // So we rest the timer in case of cleared and for not cleared.
    reset_timer();

    if (cleared) {
        send_get_blocks(null_hash);
    }

    return true;
}

bool protocol_block_in::handle_receive_block_transactions(code const& ec, block_transactions_const_ptr message) {
    if (stopped(ec)) {
        return false;
    }

    auto it = compact_blocks_map_.find(message->block_hash());
    if (it == compact_blocks_map_.end()) {
        spdlog::debug("[node] Compact Block [{}] The blocktxn received doesn't match with any temporal compact block [{}]", encode_hash(message->block_hash()), authority());
        stop(error::channel_stopped);
        return false;
    }

    auto temp_compact_block_ = it->second;

    auto const& vtx_missing = message->transactions();

    auto& txn_available = temp_compact_block_.transactions;
    auto const& header_temp = temp_compact_block_.header;

    size_t tx_missing_offset = 0;

    for (size_t i = 0; i < txn_available.size(); i++) {
        if ( ! txn_available[i].is_valid()) {
            if (vtx_missing.size() <= tx_missing_offset) {
                spdlog::debug("[node] Compact Block [{}] The offset {} is invalid [{}]", encode_hash(message->block_hash()), tx_missing_offset, authority());
                stop(error::channel_stopped);

                //TODO(Mario) verify if necesary mutual exclusion
                compact_blocks_map_.erase(it);
                return false;
            }
            txn_available[i] = std::move(vtx_missing[tx_missing_offset]);
            ++tx_missing_offset;
        }
    }

    if (vtx_missing.size() != tx_missing_offset) {
       spdlog::debug("[node] Compact Block [{}] The offset {} is invalid [{}]", encode_hash(message->block_hash()), tx_missing_offset, authority());
        stop(error::channel_stopped);

        //TODO(Mario): verify if necesary mutual exclusion
        compact_blocks_map_.erase(it);
        return false;
    }

    auto const tempblock = std::make_shared<domain::message::block>(std::move(header_temp), std::move(txn_available));
    organize_block(tempblock);
    //TODO(Mario) verify if necesary mutual exclusion
    compact_blocks_map_.erase(it);

    return true;
}

void protocol_block_in::handle_fetch_block_locator_compact_block(code const& ec, get_headers_ptr message, hash_digest const& stop_hash) {
    if (stopped(ec)) {
        return;
    }

    if (ec) {
        spdlog::error("[node] Internal failure generating block locator (compact block) for [{}] {}", authority(), ec.message());
        stop(ec);
        return;
    }

    if (message->start_hashes().empty()) {
        return;
    }

    message->set_stop_hash(stop_hash);
    SEND2(*message, handle_send, _1, message->command);

    spdlog::debug("[node] Sended get header message compact blocks to [{}]", authority());

}

bool protocol_block_in::handle_receive_compact_block(code const& ec, compact_block_const_ptr message) {
    if (stopped(ec)) {
        return false;
    }

    //TODO(Mario): purge old compact blocks

    //the header of the compact block is the header of the block
    auto const& header_temp = message->header();

    if ( ! header_temp.is_valid()) {
        spdlog::debug("[node] Compact Block [{}] The compact block header is invalid [{}]", encode_hash(header_temp.hash()), authority());
        stop(error::channel_stopped);
        return false;
    }

    //if the compact block exists in the map, is already in process
    if (compact_blocks_map_.count(header_temp.hash()) > 0) {
        return true;
    }

    //if we haven't the parent block already, send a get_header message and return
    if ( ! chain_.get_block_exists_safe(header_temp.previous_block_hash() ) ) {
        spdlog::debug("[node] Compact Block parent block not exists [ {} [{}]", encode_hash(header_temp.previous_block_hash()), authority());

        if ( ! chain_.is_stale() ) {
            spdlog::debug("[node] The chain isn't stale sending getheaders message [{}]", authority());
            auto const heights = block::locator_heights(node_.top_block().height());
            chain_.fetch_block_locator(heights,BIND3(handle_fetch_block_locator_compact_block, _1, _2, null_hash));
        }
        return true;
    }
    //  else {
    //     spdlog::info("[node]
    //] Compact Block parent block EXISTS [ {} [{}]", encode_hash(header_temp.previous_block_hash())
    //, authority());
    // }


    //the nonce used to calculate the short id
    auto const nonce = message->nonce();
    auto const& prefiled_txs = message->transactions();
    auto const& short_ids = message->short_ids();

    std::vector<domain::chain::transaction> txs_available(short_ids.size() + prefiled_txs.size());
    int32_t lastprefilledindex = -1;

    for (size_t i = 0; i < prefiled_txs.size(); ++i) {
        if ( ! prefiled_txs[i].is_valid()) {

            spdlog::debug("[node] Compact Block [{}] The prefilled transaction is invalid [{}]", encode_hash(header_temp.hash()), authority());
            stop(error::channel_stopped);
            return false;
        }

        //encoded = (current_index - prev_index) - 1
        // current = +1 + prev
        //       prev            current
        lastprefilledindex += prefiled_txs[i].index() + 1;


        if (lastprefilledindex > std::numeric_limits<uint16_t>::max()) {
            spdlog::debug("[node] Compact Block [{}] The prefilled index {} is out of range [{}]", encode_hash(header_temp.hash()), lastprefilledindex, authority());
            stop(error::channel_stopped);
            return false;
        }

        if ((uint32_t)lastprefilledindex > short_ids.size() + i) {
            // If we are inserting a tx at an index greater than our full list
            // of shorttxids plus the number of prefilled txn we've inserted,
            // then we have txn for which we have neither a prefilled txn or a
            // shorttxid!

            spdlog::debug("[node] Compact Block [{}] The prefilled index {} is out of range [{}]", encode_hash(header_temp.hash()), lastprefilledindex, authority());
            stop(error::channel_stopped);
            return false;
        }

        txs_available[lastprefilledindex] = prefiled_txs[i].transaction();
    }

    // Calculate map of txids -> positions and check mempool to see what we have
    // (or don't). Because well-formed cmpctblock messages will have a
    // (relatively) uniform distribution of short IDs, any highly-uneven
    // distribution of elements can be safely treated as a READ_STATUS_FAILED.
    std::unordered_map<uint64_t, uint16_t> shorttxids(short_ids.size());
    uint16_t index_offset = 0;

    for (size_t i = 0; i < short_ids.size(); ++i) {

        while (txs_available[i + index_offset].is_valid()) {
            ++index_offset;
        }
        shorttxids[short_ids[i]] = i + index_offset;
        // To determine the chance that the number of entries in a bucket
        // exceeds N, we use the fact that the number of elements in a single
        // bucket is binomially distributed (with n = the number of shorttxids
        // S, and p = 1 / the number of buckets), that in the worst case the
        // number of buckets is equal to S (due to std::unordered_map having a
        // default load factor of 1.0), and that the chance for any bucket to
        // exceed N elements is at most buckets * (the chance that any given
        // bucket is above N elements). Thus: P(max_elements_per_bucket > N) <=
        // S * (1 - cdf(binomial(n=S,p=1/S), N)). If we assume blocks of up to
        // 16000, allowing 12 elements per bucket should only fail once per ~1
        // million block transfers (per peer and connection).

        if (shorttxids.bucket_size(shorttxids.bucket(short_ids[i])) > 12) {
            // Duplicate txindexes, the block is now in-flight, so
            // just request it.

            spdlog::info("[node] Compact Block, sendening getdata for hash ({}) to [{}]", encode_hash(header_temp.hash()), authority());
            send_get_data_compact_block(ec, header_temp.hash());

            return true;
        }
    }


    // TODO: in the shortid-collision case, we should instead request both
    // transactions which collided. Falling back to full-block-request here is
    // overkill.
    if (shorttxids.size() != short_ids.size()) {
        // Short ID collision
        spdlog::info("[node] Compact Block, sendening getdata for hash ({}) to [{}]", encode_hash(header_temp.hash()), authority());
        send_get_data_compact_block(ec, header_temp.hash());
        return true;
    }

    size_t mempool_count = 0;
    chain_.fill_tx_list_from_mempool(*message, mempool_count, txs_available, shorttxids);

    std::vector<uint64_t> txs;
    size_t prev_idx = 0;

    for (size_t i = 0; i < txs_available.size(); ++i) {
        if ( ! txs_available[i].is_valid()) {
            //diff_enc = (current_index - prev_index) - 1
            size_t diff_enc = i - prev_idx - (txs.size() > 0 ? 1 : 0);
            prev_idx = i;
            txs.push_back(diff_enc);
        }
    }

    if (txs.empty()) {
        auto const tempblock = std::make_shared<domain::message::block>(std::move(header_temp), std::move(txs_available));
        organize_block(tempblock);
        return true;
    } else {
        compact_blocks_map_.emplace(header_temp.hash(), temp_compact_block{std::move(header_temp), std::move(txs_available)});
        auto req_tx = get_block_transactions(header_temp.hash(),txs);
        SEND2(req_tx, handle_send, _1, get_block_transactions::command);
        return true;
    }
}

void protocol_block_in::send_get_data_compact_block(code const& ec, hash_digest const& hash) {
    hash_list hashes;
    hashes.push_back(hash);

    get_data_ptr request;
    request = std::make_shared<get_data>(hashes, inventory::type_id::block);

    send_get_data(ec,request);
}

// The block has been saved to the block chain (or not).
// This will be picked up by subscription in block_out and will cause the block
// to be announced to non-originating peers.
void protocol_block_in::handle_store_block(code const& ec, block_const_ptr message) {
    if (stopped(ec)) {
        return;
    }

    auto const hash = message->header().hash();

    // Ask the peer for blocks from the chain top up to this orphan.
    if (ec == error::orphan_block) {
        send_get_blocks(hash);
    }

    auto const encoded = encode_hash(hash);

    if (ec == error::orphan_block ||
        ec == error::duplicate_block ||
        ec == error::insufficient_work) {
        spdlog::debug("[node] Captured block [{}] from [{}] {}", encoded, authority(), ec.message());
        return;
    }

    // TODO: send reject as applicable.
    if (ec) {
        spdlog::debug("[node] Rejected block [{}] from [{}] {}", encoded, authority(), ec.message());
        stop(ec);
        return;
    }

    auto const state = message->validation.state;
    KTH_ASSERT(state);

    // Show that diplayed forks may be missing activations due to checkpoints.
    auto const checked = state->is_under_checkpoint() ? "*" : "";

    spdlog::debug("[node] Connected block [{}] at height [{}] from [{}] ({}{}, {}).", encoded, state->height(), authority(), state->enabled_forks(), checked, state->minimum_version());


#if defined(KTH_STATISTICS_ENABLED)
    report(*message, node_);
#else
    report(*message);
#endif
}

// Subscription.
//-----------------------------------------------------------------------------

// This is fired by the callback (i.e. base timer and stop handler).
void protocol_block_in::handle_timeout(code const& ec) {
    if (stopped(ec)) {
        // This may get called more than once per stop.
        handle_stop(ec);
        return;
    }

    // Since we need blocks do not stay connected to peer in bad version range.
    if ( ! blocks_from_peer_) {
        stop(error::channel_stopped);
        return;
    }

    if (ec && ec != error::channel_timeout) {
        spdlog::debug("[node] Failure in block timer for [{}] {}", authority(), ec.message());
        stop(ec);
        return;
    }

    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    mutex.lock_shared();
    auto const backlog_empty = backlog_.empty();
    mutex.unlock_shared();
    ///////////////////////////////////////////////////////////////////////////

    // Can only end up here if time was not extended.
    if ( ! backlog_empty) {
        spdlog::debug("[node] Peer [{}] exceeded configured block latency.", authority());
        stop(ec);
    }

    // Can only end up here if peer did not respond to inventory or get_data.
    // At this point we are caught up with an honest peer. But if we are stale
    // we should try another peer and not just keep pounding this one.
    if (chain_.is_stale()) {
        stop(error::channel_stopped);
    }

    // If we are not stale then we are either good or stalled until peer sends
    // an announcement. There is no sense pinging a broken peer, so we either
    // drop the peer after a certain mount of time (above 10 minutes) or rely
    // on other peers to keep us moving and periodically age out connections.
    // Note that this allows a non-witness peer to hang on indefinately to our
    // witness-requiring node until the node becomes stale. Allowing this then
    // depends on requiring witness peers for explicitly outbound connections.
}

void protocol_block_in::handle_stop(code const&) {
    spdlog::debug("[network] Stopped block_in protocol for [{}].", authority());
}

// Block reporting.
//-----------------------------------------------------------------------------

inline
bool enabled(size_t height) {
    // Vary the reporting performance reporting interval by height.
    // auto const modulus =
    //     (height < 100000 ? 100 :
    //     (height < 200000 ? 10 : 1));

    // return height % modulus == 0;

    return true;
}

inline
float difference(const asio::time_point& start, const asio::time_point& end) {
    auto const elapsed = duration_cast<asio::microseconds>(end - start);
    return static_cast<float>(elapsed.count());
}

inline
size_t unit_cost(const asio::time_point& start, const asio::time_point& end, size_t value) {
    return static_cast<size_t>(std::round(difference(start, end) / value));
}

inline
size_t total_cost_ms(const asio::time_point& start, const asio::time_point& end) {
    static constexpr size_t microseconds_per_millisecond = 1000;
    return unit_cost(start, end, microseconds_per_millisecond);
}

//static

#if defined(KTH_STATISTICS_ENABLED)
void protocol_block_in::report(domain::chain::block const& block, full_node& node) {
#else
void protocol_block_in::report(domain::chain::block const& block) {
#endif
    KTH_ASSERT(block.validation.state);
    auto const height = block.validation.state->height();

#if defined(KTH_STATISTICS_ENABLED)
    if (true) {
#else
    if (enabled(height)) {
#endif
        auto const& times = block.validation;
        auto const now = asio::steady_clock::now();
        auto const transactions = block.transactions().size();
        auto const inputs = std::max(block.total_inputs(), size_t(1));
        auto const outputs = size_t{1};

        // // auto const outputs = std::max(block.total_outputs(), size_t(1));
        // // auto [inputs, outputs] = block.total_inputs_outputs();
        // auto [inputs, outputs] = domain::chain::total_inputs_outputs(block);

        // inputs = std::max(inputs, size_t(1));
        // outputs = std::max(outputs, size_t(1));

        // Subtract total deserialization time from start of validation because
        // the wait time is between end_deserialize and start_check. This lets
        // us simulate block announcement validation time as there is no wait.
        auto const start_validate = times.start_check - (times.end_deserialize - times.start_deserialize);

#if defined(KTH_STATISTICS_ENABLED)
        node.collect_statistics(height, transactions, inputs, outputs,
            total_cost_ms(times.end_deserialize, times.start_check),
            total_cost_ms(start_validate, times.start_notify),
            unit_cost(start_validate, times.start_notify, inputs),
            unit_cost(times.start_deserialize, times.end_deserialize, inputs),
            unit_cost(times.start_check, times.start_populate, inputs),
            unit_cost(times.start_populate, times.start_accept, inputs),
            unit_cost(times.start_accept, times.start_connect, inputs),
            unit_cost(times.start_connect, times.start_notify, inputs),
            unit_cost(times.start_push, times.end_push, inputs),
            block.validation.cache_efficiency);
#endif

        auto formatted = fmt::format("[{:6}] {:>5} txs {:>5} ins "
            "{:>4} wms {:>5} vms {:>4} vus {:>4} rus {:>4} cus {:>4} pus "
            "{:>4} aus {:>4} sus {:>4} dus {:f}", height, transactions, inputs,

            // wms: wait total (ms)
            total_cost_ms(times.end_deserialize, times.start_check),

            // vms: validation total (ms)
            total_cost_ms(start_validate, times.start_notify),

            // vus: validation per input (µs)
            unit_cost(start_validate, times.start_notify, inputs),

            // rus: deserialization (read) per input (µs)
            unit_cost(times.start_deserialize, times.end_deserialize, inputs),

            // cus: check per input (µs)
            unit_cost(times.start_check, times.start_populate, inputs),

            // pus: population per input (µs)
            unit_cost(times.start_populate, times.start_accept, inputs),

            // aus: accept per input (µs)
            unit_cost(times.start_accept, times.start_connect, inputs),

            // sus: connect (script) per input (µs)
            unit_cost(times.start_connect, times.start_notify, inputs),

            // dus: deposit per input (µs)
            unit_cost(times.start_push, times.end_push, inputs),

            // this block transaction cache efficiency (hits/queries)
            block.validation.cache_efficiency);

#if defined(KTH_STATISTICS_ENABLED)
        if (enabled(height)) {
            spdlog::debug("[blockchain] {}", formatted);
        }
#else
        // spdlog::info("[blockchain] {}", formatted);
        spdlog::info("{}", formatted);
#endif
    }
}

} // namespace kth::node