// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/interface/block_chain.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <latch>

#include <memory>
#include <numeric>
#include <string>
#include <unordered_set>
#include <utility>

#include <kth/blockchain/populate/populate_chain_state.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/database.hpp>
#include <kth/domain.hpp>
#include <kth/domain/multi_crypto_support.hpp>

#include <kth/infrastructure/math/sip_hash.hpp>
#include <kth/infrastructure/utility/limits.hpp>
#include <kth/infrastructure/utility/timer.hpp>

namespace kth {
//TODO: remove from here
time_t floor_subtract(time_t left, time_t right) {
    static auto const floor = (std::numeric_limits<time_t>::min)();
    return right >= left ? floor : left - right;
}

} // namespace kth

namespace kth::blockchain {

using spent_value_type = std::pair<hash_digest, uint32_t>;
//using spent_container = std::vector<spent_value_type>;
using spent_container = std::unordered_set<spent_value_type>;

} // namespace kth::blockchain

namespace std {

template <>
struct hash<kth::blockchain::spent_value_type> {
    size_t operator()(kth::blockchain::spent_value_type const& point) const {
        size_t seed = 0;
        boost::hash_combine(seed, point.first);
        boost::hash_combine(seed, point.second);
        return seed;
    }
};

} // namespace std

namespace kth {

#if defined(KTH_WITH_MEMPOOL)
namespace mining {
mempool* mempool::candidate_index_t::parent_ = nullptr;
} // namespace mining
#endif

namespace blockchain {

using namespace kd::config;
using namespace kd::message;
using namespace kth::database;
using namespace std::placeholders;

#define NAME "block_chain"

static auto const hour_seconds = 3600u;

block_chain::block_chain(threadpool& pool, blockchain::settings const& chain_settings
                       , database::settings const& database_settings, domain::config::network network, bool relay_transactions /* = true*/)
    : stopped_(true)
    , settings_(chain_settings)
    , notify_limit_seconds_(chain_settings.notify_limit_hours * hour_seconds)
    , chain_state_populator_(*this, chain_settings, network)
    , database_(database_settings)
    , validation_mutex_(relay_transactions)
    , priority_pool_("blockchain", thread_ceiling(chain_settings.cores), priority(chain_settings.priority))
    , dispatch_(priority_pool_, NAME "_priority")

#if defined(KTH_WITH_MEMPOOL)
    , mempool_(chain_settings.mempool_max_template_size, chain_settings.mempool_size_multiplier)
    , transaction_organizer_(validation_mutex_, dispatch_, pool, *this, chain_settings, mempool_)
    , block_organizer_(validation_mutex_, dispatch_, pool, *this, chain_settings, network, relay_transactions, mempool_)
#else
    , transaction_organizer_(validation_mutex_, dispatch_, pool, *this, chain_settings)
    , block_organizer_(validation_mutex_, dispatch_, pool, *this, chain_settings, network, relay_transactions)
#endif
{}

// ============================================================================
// FAST CHAIN
// ============================================================================

// Readers.
// ----------------------------------------------------------------------------

uint32_t get_clock_now() {
    auto const now = std::chrono::high_resolution_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
}

bool block_chain::get_output(domain::chain::output& out_output, size_t& out_height,
    uint32_t& out_median_time_past, bool& out_coinbase,
    const domain::chain::output_point& outpoint, size_t branch_height,
    bool require_confirmed) const {

    auto const tx = database_.internal_db().get_transaction(outpoint.hash(), branch_height);

    if ( ! tx.is_valid()) return false;

    out_height = tx.height();
    out_coinbase = tx.position() == 0;
    out_median_time_past = tx.median_time_past();
    out_output = tx.transaction().outputs()[outpoint.index()];

    return true;
}

bool block_chain::get_transaction_position(size_t& out_height, size_t& out_position, hash_digest const& hash, bool require_confirmed) const {

    auto const result = database_.internal_db().get_transaction(hash, max_size_t);

    if ( result.is_valid() ) {
        out_height = result.height();
        out_position = result.position();
        return true;
    }

    if (require_confirmed ) return false;

    auto const result2 = database_.internal_db().get_transaction_unconfirmed(hash);
    if ( ! result2.is_valid() ) return false;

    out_height = result2.height();
    out_position = position_max;
    return true;
}

#if ! defined(KTH_DB_READONLY)
void block_chain::prune_reorg_async() {
    if ( ! is_stale()) {
        dispatch_.concurrent([this](){
            database_.prune_reorg();
        });
    }
}
#endif // ! defined(KTH_DB_READONLY)

bool block_chain::get_block_exists(hash_digest const& block_hash) const {
    return database_.internal_db().get_header(block_hash).first.is_valid();
}

bool block_chain::get_block_exists_safe(hash_digest const& block_hash) const {
    return get_block_exists(block_hash);
}

bool block_chain::get_block_hash(hash_digest& out_hash, size_t height) const {
    auto const result = database_.internal_db().get_header(height);
    if ( ! result.is_valid()) return false;
    out_hash = result.hash();
    return true;
}

bool block_chain::get_branch_work(uint256_t& out_work, uint256_t const& maximum, size_t from_height) const {
    size_t top;
    if ( ! get_last_height(top)) return false;

    out_work = 0;
    for (uint32_t height = from_height; height <= top && out_work < maximum; ++height) {
        auto const result = database_.internal_db().get_header(height);
        if ( ! result.is_valid()) return false;
        out_work += domain::chain::header::proof(result.bits());
    }

    return true;
}

bool block_chain::get_header(domain::chain::header& out_header, size_t height) const {
    out_header = database_.internal_db().get_header(height);
    return out_header.is_valid();
}

std::optional<database::header_with_abla_state_t> block_chain::get_header_and_abla_state(size_t height) const {
    return database_.internal_db().get_header_and_abla_state(height);
}

domain::chain::header::list block_chain::get_headers(size_t from, size_t to) const {
    return database_.internal_db().get_headers(from, to);
}

bool block_chain::get_height(size_t& out_height, hash_digest const& block_hash) const {
    auto result = database_.internal_db().get_header(block_hash);
    if ( ! result.first.is_valid()) return false;
    out_height = result.second;
    return true;
}

bool block_chain::get_bits(uint32_t& out_bits, size_t height) const {
    auto result = database_.internal_db().get_header(height);
    if ( ! result.is_valid()) return false;
    out_bits = result.bits();
    return true;
}

bool block_chain::get_timestamp(uint32_t& out_timestamp, size_t height) const {
    auto result = database_.internal_db().get_header(height);
    if ( ! result.is_valid()) return false;
    out_timestamp = result.timestamp();
    return true;
}

bool block_chain::get_version(uint32_t& out_version, size_t height) const {
    auto result = database_.internal_db().get_header(height);
    if ( ! result.is_valid()) return false;
    out_version = result.version();
    return true;
}

bool block_chain::get_last_height(size_t& out_height) const {
    uint32_t temp;
    auto const res = database_.internal_db().get_last_height(temp);
    out_height = temp;
    return succeed(res);
}

bool block_chain::get_utxo(domain::chain::output& out_output, size_t& out_height, uint32_t& out_median_time_past, bool& out_coinbase, domain::chain::output_point const& outpoint, size_t branch_height) const {
    auto entry = database_.internal_db().get_utxo(outpoint);
    if ( ! entry.is_valid()) return false;
    if (entry.height() > branch_height) return false;

    out_output = entry.output();
    out_height = entry.height();
    out_median_time_past = entry.median_time_past();
    out_coinbase = entry.coinbase();

    return true;
}

std::pair<bool, database::internal_database::utxo_pool_t> block_chain::get_utxo_pool_from(uint32_t from, uint32_t to) const {
    auto p = database_.internal_db().get_utxo_pool_from(from, to);

    if (p.first != result_code::success) {
        return {false, std::move(p.second)};
    }
    return {true, std::move(p.second)};
}

// Writers
// ----------------------------------------------------------------------------
#if ! defined(KTH_DB_READONLY)

// bool block_chain::insert(block_const_ptr block, size_t height, int) {
bool block_chain::insert(block_const_ptr block, size_t height) {
    return database_.insert(*block, height) == error::success;
}

void block_chain::push(transaction_const_ptr tx, dispatcher&, result_handler handler) {

    //TODO(kth):  dissabled this tx cache because we don't want special treatment for the last txn, it affects the explorer rpc methods
    //last_transaction_.store(tx);

    // Transaction push is currently sequential so dispatch is not used.
    handler(database_.push(*tx, chain_state()->enabled_forks()));
}

#endif // ! defined(KTH_DB_READONLY)

#ifdef KTH_DB_TRANSACTION_UNCONFIRMED
void block_chain::fetch_unconfirmed_transaction(hash_digest const& hash, transaction_unconfirmed_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, nullptr);
        return;
    }

    auto const result = database_.transactions_unconfirmed().get(hash);

    if ( ! result) {
        handler(error::not_found, nullptr);
        return;
    }

    auto const tx = std::make_shared<const transaction>(result.transaction());
    handler(error::success, tx);
}
#endif // KTH_DB_TRANSACTION_UNCONFIRMED

#if ! defined(KTH_DB_READONLY)
void block_chain::reorganize(const infrastructure::config::checkpoint& fork_point,
    block_const_ptr_list_const_ptr incoming_blocks,
    block_const_ptr_list_ptr outgoing_blocks, dispatcher& dispatch,
    result_handler handler) {
    if (incoming_blocks->empty()) {
        handler(error::reorganize_empty_blocks);
        return;
    }

    // The top (back) block is used to update the chain state.
    auto const complete = std::bind(&block_chain::handle_reorganize, this, _1, incoming_blocks->back(), handler);
    database_.reorganize(fork_point, incoming_blocks, outgoing_blocks, dispatch, complete);
}

void block_chain::handle_reorganize(code const& ec, block_const_ptr top, result_handler handler) {
    if (ec) {
        handler(ec);
        return;
    }

    if ( ! top->validation.state) {
        handler(error::chain_state_invalid);
        return;
    }

    set_chain_state(top->validation.state);
    last_block_.store(top);

    handler(error::success);
}

#endif // ! defined(KTH_DB_READONLY)

// Properties.
// ----------------------------------------------------------------------------

// For tx validator, call only from inside validate critical section.
domain::chain::chain_state::ptr block_chain::chain_state() const {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    shared_lock lock(pool_state_mutex_);

    // Initialized on start and updated after each successful organization.
    return pool_state_;
    ///////////////////////////////////////////////////////////////////////////
}

// For block validator, call only from inside validate critical section.
domain::chain::chain_state::ptr block_chain::chain_state(branch::const_ptr branch) const {
    // Promote from cache if branch is same height as pool (most typical).
    // Generate from branch/store if the promotion is not successful.
    // If the organize is successful pool state will be updated accordingly.
    return chain_state_populator_.populate(chain_state(), branch);
}

// private.
code block_chain::set_chain_state(domain::chain::chain_state::ptr previous) {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    unique_lock lock(pool_state_mutex_);

    pool_state_ = chain_state_populator_.populate(previous);
    return pool_state_ ? error::success : error::pool_state_failed;
    ///////////////////////////////////////////////////////////////////////////
}

// ============================================================================
// SAFE CHAIN
// ============================================================================

// Startup and shutdown.
// ----------------------------------------------------------------------------

bool block_chain::start() {
    stopped_ = false;

    if ( ! database_.open()) {
        LOG_ERROR(LOG_BLOCKCHAIN, "Failed to open database.");
        return false;
    }

    //switch to fast mode if the database is stale
    //set_database_flags();

    // Initialize chain state after database start but before organizers.
    pool_state_ = chain_state_populator_.populate();
    if ( ! pool_state_) {
        LOG_ERROR(LOG_BLOCKCHAIN, "Failed to initialize chain state.");
        return false;
    }

    auto const tx_org_started = transaction_organizer_.start();
    if ( ! tx_org_started) {
        LOG_ERROR(LOG_BLOCKCHAIN, "Failed to start transaction organizer.");
        return false;
    }

    auto const blk_org_started = block_organizer_.start();
    if ( ! blk_org_started) {
        LOG_ERROR(LOG_BLOCKCHAIN, "Failed to start block organizer.");
        return false;
    }
    return true;
}

bool block_chain::stop() {
    stopped_ = true;

    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    validation_mutex_.lock_high_priority();

    // This cannot call organize or stop (lock safe).
    auto result = transaction_organizer_.stop() && block_organizer_.stop();

    // The priority pool must not be stopped while organizing.
    priority_pool_.shutdown();

    validation_mutex_.unlock_high_priority();
    ///////////////////////////////////////////////////////////////////////////
    return result;
}

// Close is idempotent and thread safe.
// Optional as the blockchain will close on destruct.
bool block_chain::close() {
    auto const result = stop();
    priority_pool_.join();
    return result && database_.close();
}

block_chain::~block_chain() {
    close();
}

// Queries.
// ----------------------------------------------------------------------------
// Blocks are and transactions returned const because they don't change and
// this eliminates the need to copy the cached items.

void block_chain::fetch_block(size_t height,
    block_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, nullptr, 0);
        return;
    }

    auto const cached = last_block_.load();

    // Try the cached block first.
    if (cached && cached->validation.state &&
        cached->validation.state->height() == height) {
        handler(error::success, cached, height);
        return;
    }

    auto const block_result = database_.internal_db().get_block(height);

    if ( ! block_result.is_valid()) {
        handler(error::not_found, nullptr, 0);
        return;
    }

    auto const result = std::make_shared<const block>(block_result);

    handler(error::success, result, height);
}

void block_chain::fetch_block(hash_digest const& hash,
    block_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, nullptr, 0);
        return;
    }

    auto const cached = last_block_.load();

    // Try the cached block first.
    if (cached && cached->validation.state && cached->hash() == hash) {
        handler(error::success, cached, cached->validation.state->height());
        return;
    }

    auto const block_result = database_.internal_db().get_block(hash);

    if ( ! block_result.first.is_valid()) {
        handler(error::not_found, nullptr, 0);
        return;
    }

    auto const height = block_result.second;

    auto const result = std::make_shared<const block>(block_result.first);

    handler(error::success, result, height);
}

void block_chain::fetch_block_header_txs_size(hash_digest const& hash,
    block_header_txs_size_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, nullptr, 0, std::make_shared<hash_list>(hash_list()),0);
        return;
    }

    auto const block_result = database_.internal_db().get_block(hash);

    if ( ! block_result.first.is_valid()) {
        handler(error::not_found, nullptr, 0, std::make_shared<hash_list>(hash_list()),0);
        return;
    }

    auto const height = block_result.second;
    auto const result = std::make_shared<const header>(block_result.first.header());
    auto const tx_hashes = std::make_shared<hash_list>(block_result.first.to_hashes());
    //TODO(fernando): encapsulate header and tx_list
    handler(error::success, result, height, tx_hashes, block_result.first.serialized_size());
}


// void block_chain::fetch_merkle_block(size_t height, transaction_hashes_fetch_handler handler) const
void block_chain::fetch_merkle_block(size_t height, merkle_block_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, nullptr, 0);
        return;
    }

    auto const block_result = database_.internal_db().get_block(height);

    if ( ! block_result.is_valid()) {
        handler(error::not_found, nullptr, 0);
        return;
    }

    auto const merkle = std::make_shared<merkle_block>(block_result.header(),
        block_result.transactions().size(), block_result.to_hashes(), data_chunk{});
    handler(error::success, merkle, height);
}

void block_chain::fetch_merkle_block(hash_digest const& hash,
    merkle_block_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, nullptr, 0);
        return;
    }

    auto const block_result = database_.internal_db().get_block(hash);

    if ( ! block_result.first.is_valid()) {
        handler(error::not_found, nullptr, 0);
        return;
    }

    auto const merkle = std::make_shared<merkle_block>(block_result.first.header(),
        block_result.first.transactions().size(), block_result.first.to_hashes(), data_chunk{});
    handler(error::success, merkle, block_result.second);
}

void block_chain::fetch_compact_block(size_t height, compact_block_fetch_handler handler) const {
    // TODO (Mario): implement compact blocks.
    handler(error::not_implemented, {}, 0);
}

void block_chain::fetch_compact_block(hash_digest const& hash, compact_block_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, {},0);
        return;
    }

    fetch_block(hash,[&handler](code const& ec, block_const_ptr message, size_t height) {
        if (ec == error::success) {
            auto blk_ptr = std::make_shared<compact_block>(compact_block::factory_from_block(*message));
            handler(error::success, blk_ptr, height);
        } else {
            handler(ec, nullptr, height);
        }
    });
}

// This may execute over 500 queries.
void block_chain::fetch_locator_block_hashes(get_blocks_const_ptr locator,
    hash_digest const& threshold, size_t limit,
    inventory_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, nullptr);
        return;
    }

    // This is based on the idea that looking up by block hash to get heights
    // will be much faster than hashing each retrieved block to test for stop.

    // Find the start block height.
    // If no start block is on our chain we start with block 0.
    uint32_t start = 0;
    for (auto const& hash: locator->start_hashes()) {
        auto const result = database_.internal_db().get_block(hash);
        if (result.first.is_valid())
        {
            start = result.second;
            break;
        }
    }

    // The begin block requested is always one after the start block.
    auto begin = *safe_add(start, uint32_t(1));

    // The maximum number of headers returned is 500.
    auto end = *safe_add(begin, uint32_t(limit));

    // Find the upper threshold block height (peer-specified).
    if (locator->stop_hash() != null_hash) {
        // If the stop block is not on chain we treat it as a null stop.
        auto const result = database_.internal_db().get_block(locator->stop_hash());

        // Otherwise limit the end height to the stop block height.
        // If end precedes begin floor_subtract will handle below.
        if (result.first.is_valid())
            end = std::min(result.second, end);
    }

    // Find the lower threshold block height (self-specified).
    if (threshold != null_hash) {
        // If the threshold is not on chain we ignore it.
        auto const result = database_.internal_db().get_block(threshold);

        // Otherwise limit the begin height to the threshold block height.
        // If begin exceeds end floor_subtract will handle below.
        if (result.first.is_valid())
            begin = std::max(result.second, begin);
    }

    auto hashes = std::make_shared<inventory>();
    hashes->inventories().reserve(floor_subtract(end, begin));

    // Build the hash list until we hit end or the blockchain top.
    for (auto height = begin; height < end; ++height) {
        auto const result = database_.internal_db().get_block(height);

        // If not found then we are at our top.
        if ( ! result.is_valid())
        {
            hashes->inventories().shrink_to_fit();
            break;
        }

        static auto const id = inventory::type_id::block;
        hashes->inventories().emplace_back(id, result.header().hash());
    }

    handler(error::success, std::move(hashes));
}

void block_chain::fetch_ds_proof(hash_digest const& hash, ds_proof_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, nullptr);
        return;
    }

    transaction_organizer_.fetch_ds_proof(hash, handler);
}

void block_chain::fetch_transaction(hash_digest const& hash, bool require_confirmed, transaction_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, nullptr, 0, 0);
        return;
    }
    auto const result = database_.internal_db().get_transaction(hash, max_size_t);
    if ( result.is_valid() ) {
        auto const tx = std::make_shared<const transaction>(result.transaction());
        handler(error::success, tx, result.position(), result.height());
        return;
    }

    if (require_confirmed) {
        handler(error::not_found, nullptr, 0, 0);
        return;
    }

    auto const result2 = database_.internal_db().get_transaction_unconfirmed(hash);
    if ( !  result2.is_valid() ) {
        handler(error::not_found, nullptr, 0, 0);
        return;
    }

    auto const tx = std::make_shared<const transaction>(result2.transaction());
    handler(error::success, tx, position_max, result2.height());
}

// This is same as fetch_transaction but skips deserializing the tx payload.
void block_chain::fetch_transaction_position(hash_digest const& hash, bool require_confirmed, transaction_index_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, 0, 0);
        return;
    }

    auto const result = database_.internal_db().get_transaction(hash, max_size_t);

    if ( result.is_valid() ) {
        handler(error::success, result.position(), result.height());
        return;
    }

    if (require_confirmed) {
        handler(error::not_found, 0, 0);
        return;
    }

    auto const result2 = database_.internal_db().get_transaction_unconfirmed(hash);
    if ( !  result2.is_valid() ) {
        handler(error::not_found, 0, 0);
        return;
    }

    handler(error::success, position_max, result2.height());
}


//TODO (Mario) : Review and move to proper location
hash_digest generate_merkle_root(std::vector<domain::chain::transaction> transactions) {
    using std::swap;

    if (transactions.empty()) return null_hash;

    hash_list merkle;

    auto hasher = [&merkle](transaction const& tx) {
        merkle.push_back(tx.hash());
    };

    // Hash ordering matters, don't use std::transform here.
    std::for_each(transactions.begin(), transactions.end(), hasher);

    hash_list update;
    // Initial capacity is half of the original list (clear doesn't reset).
    update.reserve((merkle.size() + 1) / 2);

    while (merkle.size() > 1) {
        // If number of hashes is odd, duplicate last hash in the list.
        if (merkle.size() % 2 != 0) {
            merkle.push_back(merkle.back());
        }

        for (auto it = merkle.begin(); it != merkle.end(); it += 2) {
            update.push_back(bitcoin_hash(build_chunk({ it[0], it[1] })));
        }

        swap(merkle, update);
        update.clear();
    }

    // There is now only one item in the list.
    return merkle.front();
}

namespace {

std::tuple<uint8_t, uint8_t> get_address_versions(bool use_testnet_rules) {
    if (use_testnet_rules) {
        return {
            kth::domain::wallet::payment_address::testnet_p2kh,
            kth::domain::wallet::payment_address::testnet_p2sh};
    }

    return {
        kth::domain::wallet::payment_address::mainnet_p2kh,
        kth::domain::wallet::payment_address::mainnet_p2sh};

}

} // anonymous namespace

//TODO(fernando): refactor!!!
std::vector<kth::blockchain::mempool_transaction_summary> block_chain::get_mempool_transactions(std::vector<std::string> const& payment_addresses, bool use_testnet_rules) const {
/*          "    \"address\"  (string) The base58check encoded address\n"
            "    \"txid\"  (string) The related txid\n"
            "    \"index\"  (number) The related input or output index\n"
            "    \"satoshis\"  (number) The difference of satoshis\n"
            "    \"timestamp\"  (number) The time the transaction entered the mempool (seconds)\n"
            "    \"prevtxid\"  (string) The previous txid (if spending)\n"
            "    \"prevout\"  (string) The previous transaction output index (if spending)\n"
*/

    auto const [encoding_p2kh, encoding_p2sh] = get_address_versions(use_testnet_rules);

    std::vector<kth::blockchain::mempool_transaction_summary> ret;

    std::unordered_set<kth::domain::wallet::payment_address> addrs;
    for (auto const& payment_address : payment_addresses) {
        kth::domain::wallet::payment_address address(payment_address);
        if (address){
            addrs.insert(address);
        }
    }

    auto const result = database_.internal_db().get_all_transaction_unconfirmed();

    for (auto const& tx_res : result) {
        auto const& tx = tx_res.transaction();
        //tx.recompute_hash();
        size_t i = 0;
        for (auto const& output : tx.outputs()) {
            auto const tx_addresses = kth::domain::wallet::payment_address::extract(output.script(), encoding_p2kh, encoding_p2sh);
            for(auto const tx_address : tx_addresses) {
                if (tx_address && addrs.find(tx_address) != addrs.end()) {
                    ret.push_back
                            (kth::blockchain::mempool_transaction_summary
                                     (tx_address.encoded_cashaddr(false), kth::encode_hash(tx.hash()), "",
                                      "", std::to_string(output.value()), i, tx_res.arrival_time()));
                }
            }
            ++i;
        }
        i = 0;
        for (auto const& input : tx.inputs()) {
            // TODO(kth): payment_addrress::extract should use the prev_output script instead of the input script
            // see https://github.com/k-nuth/core/blob/v0.10.0/src/wallet/payment_address.cpp#L505
            auto const tx_addresses = kth::domain::wallet::payment_address::extract(input.script(), encoding_p2kh, encoding_p2sh);
            for(auto const tx_address : tx_addresses)
            if (tx_address && addrs.find(tx_address) != addrs.end()) {
                std::latch latch(1);
                fetch_transaction(input.previous_output().hash(), false,
                                  [&](const kth::code &ec,
                                      kth::transaction_const_ptr tx_ptr, size_t index,
                                      size_t height) {
                                      if (ec == kth::error::success) {
                                          ret.push_back(kth::blockchain::mempool_transaction_summary
                                                                (tx_address.encoded_cashaddr(false),
                                                                kth::encode_hash(tx.hash()),
                                                                kth::encode_hash(input.previous_output().hash()),
                                                                 std::to_string(input.previous_output().index()),
                                                                "-"+std::to_string(tx_ptr->outputs()[input.previous_output().index()].value()),
                                                                i,
                                                                tx_res.arrival_time()));
                                      }
                                      latch.count_down();
                                  });
                latch.wait();
            }
            ++i;
        }
    }

    return ret;
}

// Precondition: valid payment addresses
std::vector<domain::chain::transaction> block_chain::get_mempool_transactions_from_wallets(std::vector<domain::wallet::payment_address> const& payment_addresses, bool use_testnet_rules) const {
    auto const [encoding_p2kh, encoding_p2sh] = get_address_versions(use_testnet_rules);

    std::vector<domain::chain::transaction> ret;

    auto const result = database_.internal_db().get_all_transaction_unconfirmed();

    for (auto const& tx_res : result) {
        auto const& tx = tx_res.transaction();
        //tx.recompute_hash();
        // Only insert the transaction once. Avoid duplicating the tx if serveral wallets are used in the same tx, and if the same wallet is the input and output addr.
        bool inserted = false;

        for (auto iter_output = tx.outputs().begin(); (iter_output != tx.outputs().end() && !inserted); ++iter_output) {

            auto const tx_addresses = kth::domain::wallet::payment_address::extract((*iter_output).script(), encoding_p2kh, encoding_p2sh);

            for (auto iter_addr = tx_addresses.begin(); (iter_addr != tx_addresses.end() && !inserted); ++iter_addr) {
                if (*iter_addr) {
                    auto it = std::find(payment_addresses.begin(), payment_addresses.end(), *iter_addr);
                    if (it != payment_addresses.end()) {
                        ret.push_back(tx);
                        inserted = true;
                    }
                }
            }
        }

        for (auto iter_input = tx.inputs().begin(); (iter_input != tx.inputs().end() && !inserted); ++iter_input) {
            // TODO(kth): payment_addrress::extract should use the prev_output script instead of the input script
            // see https://github.com/k-nuth/core/blob/v0.10.0/src/wallet/payment_address.cpp#L505
            auto const tx_addresses = kth::domain::wallet::payment_address::extract((*iter_input).script(), encoding_p2kh, encoding_p2sh);
            for (auto iter_addr = tx_addresses.begin(); (iter_addr != tx_addresses.end() && !inserted); ++iter_addr) {
                if (*iter_addr) {
                    auto it = std::find(payment_addresses.begin(), payment_addresses.end(), *iter_addr);
                    if (it != payment_addresses.end()) {
                        ret.push_back(tx);
                        inserted = true;
                    }
                }
            }
        }

    }

    return ret;
}

void block_chain::fill_tx_list_from_mempool(domain::message::compact_block const& block, size_t& mempool_count, std::vector<domain::chain::transaction>& txn_available, std::unordered_map<uint64_t, uint16_t> const& shorttxids) const {

    std::vector<bool> have_txn(txn_available.size());

    auto header_hash = hash(block);
    auto k0 = from_little_endian_unsafe<uint64_t>(header_hash.begin());
    auto k1 = from_little_endian_unsafe<uint64_t>(header_hash.begin() + sizeof(uint64_t));

    auto const result = database_.internal_db().get_all_transaction_unconfirmed();

    for (auto const& tx_res : result) {
        auto const& tx = tx_res.transaction();

        uint64_t shortid = sip_hash_uint256(k0, k1, tx.hash()) & uint64_t(0xffffffffffff);

        auto idit = shorttxids.find(shortid);
        if (idit != shorttxids.end()) {
            if ( ! have_txn[idit->second]) {
                txn_available[idit->second] = tx;
                have_txn[idit->second] = true;
                ++mempool_count;
            } else {
                // If we find two mempool txn that match the short id, just
                // request it. This should be rare enough that the extra
                // bandwidth doesn't matter, but eating a round-trip due to
                // FillBlock failure would be annoying.
                if (txn_available[idit->second].is_valid()) {
                    //txn_available[idit->second].reset();
                    txn_available[idit->second] = domain::chain::transaction{};
                    --mempool_count;
                }
            }
        }

        //TODO (Mario) :  break the loop
        // Though ideally we'd continue scanning for the
        // two-txn-match-shortid case, the performance win of an early exit
        // here is too good to pass up and worth the extra risk.
        /*if (mempool_count == shorttxids.size()) {
            return false;
        } else {
            return true;
        }*/
    }

}

safe_chain::mempool_mini_hash_map block_chain::get_mempool_mini_hash_map(domain::message::compact_block const& block) const {
    if (stopped()) {
        return safe_chain::mempool_mini_hash_map();
    }

    auto header_hash = hash(block);

    auto k0 = from_little_endian_unsafe<uint64_t>(header_hash.begin());
    auto k1 = from_little_endian_unsafe<uint64_t>(header_hash.begin() + sizeof(uint64_t));

    safe_chain::mempool_mini_hash_map mempool;


    auto const result = database_.internal_db().get_all_transaction_unconfirmed();

    for (auto const& tx_res : result) {
        auto const& tx = tx_res.transaction();

        auto sh = sip_hash_uint256(k0, k1, tx.hash());

       /* to_little_endian()
        uint64_t pepe = 4564564;
        uint64_t pepe2 = pepe & 0x0000ffffffffffff;

        reinterpret_cast<uint8_t*>(pepe2);
        */
        //Drop the most significative bytes from the sh
        mini_hash short_id;
        mempool.emplace(short_id,tx);

    }

    return mempool;
}

std::vector<kth::blockchain::mempool_transaction_summary> block_chain::get_mempool_transactions(std::string const& payment_address, bool use_testnet_rules) const{
    std::vector<std::string> addresses = {payment_address};
    return get_mempool_transactions(addresses, use_testnet_rules);
}


void block_chain::fetch_unconfirmed_transaction(hash_digest const& hash, transaction_unconfirmed_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, nullptr);
        return;
    }

    auto const result = database_.internal_db().get_transaction_unconfirmed(hash);

    if ( ! result.is_valid()) {
        handler(error::not_found, nullptr);
        return;
    }

    auto const tx = std::make_shared<const transaction>(result.transaction());
    handler(error::success, tx);
}



#ifdef KTH_DB_TRANSACTION_UNCONFIRMED
//TODO(fernando): refactor!!!
std::vector<kth::blockchain::mempool_transaction_summary> block_chain::get_mempool_transactions(std::vector<std::string> const& payment_addresses, bool use_testnet_rules) const {
/*          "    \"address\"  (string) The base58check encoded address\n"
            "    \"txid\"  (string) The related txid\n"
            "    \"index\"  (number) The related input or output index\n"
            "    \"satoshis\"  (number) The difference of satoshis\n"
            "    \"timestamp\"  (number) The time the transaction entered the mempool (seconds)\n"
            "    \"prevtxid\"  (string) The previous txid (if spending)\n"
            "    \"prevout\"  (string) The previous transaction output index (if spending)\n"
*/

    auto const [encoding_p2kh, encoding_p2sh] = get_address_versions(use_testnet_rules);

    std::vector<kth::blockchain::mempool_transaction_summary> ret;
    std::unordered_set<kth::domain::wallet::payment_address> addrs;
    for (auto const & payment_address : payment_addresses) {
        kth::domain::wallet::payment_address address(payment_address);
        if (address){
            addrs.insert(address);
        }
    }

    database_.transactions_unconfirmed().for_each_result([&](kth::database::transaction_unconfirmed_result const &tx_res) {
        auto tx = tx_res.transaction();
        tx.recompute_hash();
        size_t i = 0;
        for (auto const& output : tx.outputs()) {
            auto const tx_addresses = kth::domain::wallet::payment_address::extract(output.script(), encoding_p2kh, encoding_p2sh);
            for(auto const tx_address : tx_addresses) {
                if (tx_address && addrs.find(tx_address) != addrs.end()) {
                    ret.push_back
                            (kth::blockchain::mempool_transaction_summary
                                     (tx_address.encoded_cashaddr(false), kth::encode_hash(tx.hash()), "",
                                      "", std::to_string(output.value()), i, tx_res.arrival_time()));
                }
            }
            ++i;
        }
        i = 0;
        for (auto const& input : tx.inputs()) {
            // TODO(kth): payment_addrress::extract should use the prev_output script instead of the input script
            // see https://github.com/k-nuth/core/blob/v0.10.0/src/wallet/payment_address.cpp#L505
            auto const tx_addresses = kth::domain::wallet::payment_address::extract(input.script(), encoding_p2kh, encoding_p2sh);
            for(auto const tx_address : tx_addresses)
            if (tx_address && addrs.find(tx_address) != addrs.end()) {
                std::latch latch(1);
                fetch_transaction(input.previous_output().hash(), false,
                                  [&](const kth::code &ec,
                                      kth::transaction_const_ptr tx_ptr, size_t index,
                                      size_t height) {
                                      if (ec == kth::error::success) {
                                          ret.push_back(kth::blockchain::mempool_transaction_summary
                                                                (tx_address.encoded_cashaddr(false),
                                                                kth::encode_hash(tx.hash()),
                                                                kth::encode_hash(input.previous_output().hash()),
                                                                 std::to_string(input.previous_output().index()),
                                                                "-"+std::to_string(tx_ptr->outputs()[input.previous_output().index()].value()),
                                                                i,
                                                                tx_res.arrival_time()));
                                      }
                                      latch.count_down();
                                  });
                latch.wait();
            }
            ++i;
        }
        return true;
    });

    return ret;
}

// Precondition: valid payment addresses
std::vector<domain::chain::transaction> block_chain::get_mempool_transactions_from_wallets(std::vector<domain::wallet::payment_address> const& payment_addresses, bool use_testnet_rules) const {
    auto const [encoding_p2kh, encoding_p2sh] = get_address_versions(use_testnet_rules);

    std::vector<domain::chain::transaction> ret;

    database_.transactions_unconfirmed().for_each_result([&](kth::database::transaction_unconfirmed_result const &tx_res) {
        auto tx = tx_res.transaction();
        tx.recompute_hash();

        // Only insert the transaction once. Avoid duplicating the tx if serveral wallets are used in the same tx, and if the same wallet is the input and output addr.
        bool inserted = false;

        for (auto iter_output = tx.outputs().begin(); (iter_output != tx.outputs().end() && !inserted); ++iter_output) {

            auto const tx_addresses = kth::domain::wallet::payment_address::extract((*iter_output).script(), encoding_p2kh, encoding_p2sh);

            for (auto iter_addr = tx_addresses.begin(); (iter_addr != tx_addresses.end() && !inserted); ++iter_addr) {
                if (*iter_addr) {
                    auto it = std::find(payment_addresses.begin(), payment_addresses.end(), *iter_addr);
                    if (it != payment_addresses.end()) {
                        ret.push_back(tx);
                        inserted = true;
                    }
                }
            }
        }

        for (auto iter_input = tx.inputs().begin(); (iter_input != tx.inputs().end() && !inserted); ++iter_input) {
            // TODO(kth): payment_addrress::extract should use the prev_output script instead of the input script
            // see https://github.com/k-nuth/core/blob/v0.10.0/src/wallet/payment_address.cpp#L505
            auto const tx_addresses = kth::domain::wallet::payment_address::extract((*iter_input).script(), encoding_p2kh, encoding_p2sh);
            for (auto iter_addr = tx_addresses.begin(); (iter_addr != tx_addresses.end() && !inserted); ++iter_addr) {
                if (*iter_addr) {
                    auto it = std::find(payment_addresses.begin(), payment_addresses.end(), *iter_addr);
                    if (it != payment_addresses.end()) {
                        ret.push_back(tx);
                        inserted = true;
                    }
                }
            }
        }
        return true;
    });

    return ret;
}

/*
   def get_siphash_keys(self):
        header_nonce = self.header.serialize()
        header_nonce += struct.pack("<Q", self.nonce)
        hash_header_nonce_as_str = sha256(header_nonce)
        key0 = struct.unpack("<Q", hash_header_nonce_as_str[0:8])[0]
        key1 = struct.unpack("<Q", hash_header_nonce_as_str[8:16])[0]
        return [key0, key1]
*/


void block_chain::fill_tx_list_from_mempool(domain::message::compact_block const& block, size_t& mempool_count, std::vector<domain::chain::transaction>& txn_available, std::unordered_map<uint64_t, uint16_t> const& shorttxids) const {

    std::vector<bool> have_txn(txn_available.size());

    auto header_hash = hash(block);
    auto k0 = from_little_endian_unsafe<uint64_t>(header_hash.begin());
    auto k1 = from_little_endian_unsafe<uint64_t>(header_hash.begin() + sizeof(uint64_t));


    //LOG_INFO(LOG_BLOCKCHAIN
    //<< "fill_tx_list_from_mempool header_hash ->  " << encode_hash(header_hash)
    //<< " k0 " << k0
    //<< " k1 " << k1);


    database_.transactions_unconfirmed().for_each([&](domain::chain::transaction const &tx) {
        uint64_t shortid = sip_hash_uint256(k0, k1, tx.hash()) & uint64_t(0xffffffffffff);

      /*   LOG_INFO(LOG_BLOCKCHAIN
            << "mempool tx ->  " << encode_hash(tx.hash())
            << " shortid " << shortid);
      */
        auto idit = shorttxids.find(shortid);
        if (idit != shorttxids.end()) {
            if ( ! have_txn[idit->second]) {
                txn_available[idit->second] = tx;
                have_txn[idit->second] = true;
                ++mempool_count;
            } else {
                // If we find two mempool txn that match the short id, just
                // request it. This should be rare enough that the extra
                // bandwidth doesn't matter, but eating a round-trip due to
                // FillBlock failure would be annoying.
                if (txn_available[idit->second].is_valid()) {
                    //txn_available[idit->second].reset();
                    txn_available[idit->second] = domain::chain::transaction{};
                    --mempool_count;
                }
            }
        }
        // Though ideally we'd continue scanning for the
        // two-txn-match-shortid case, the performance win of an early exit
        // here is too good to pass up and worth the extra risk.
        if (mempool_count == shorttxids.size()) {
            return false;
        } else {
            return true;
        }
    });
}

safe_chain::mempool_mini_hash_map block_chain::get_mempool_mini_hash_map(domain::message::compact_block const& block) const {
    if (stopped()) {
        return safe_chain::mempool_mini_hash_map();
    }

    auto header_hash = hash(block);

    auto k0 = from_little_endian_unsafe<uint64_t>(header_hash.begin());
    auto k1 = from_little_endian_unsafe<uint64_t>(header_hash.begin() + sizeof(uint64_t));

    safe_chain::mempool_mini_hash_map mempool;

    database_.transactions_unconfirmed().for_each([&](domain::chain::transaction const &tx) {

        auto sh = sip_hash_uint256(k0, k1, tx.hash());

       /* to_little_endian()
        uint64_t pepe = 4564564;
        uint64_t pepe2 = pepe & 0x0000ffffffffffff;

        reinterpret_cast<uint8_t*>(pepe2);
        */
        //Drop the most significative bytes from the sh
        mini_hash short_id;
        mempool.emplace(short_id,tx);
        return true;
    });

    return mempool;
}

std::vector<kth::blockchain::mempool_transaction_summary> block_chain::get_mempool_transactions(std::string const& payment_address, bool use_testnet_rules) const{
    std::vector<std::string> addresses = {payment_address};
    return get_mempool_transactions(addresses, use_testnet_rules);
}



void block_chain::fetch_unconfirmed_transaction(hash_digest const& hash, transaction_unconfirmed_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, nullptr);
        return;
    }

    auto const result = database_.transactions_unconfirmed().get(hash);

    if ( ! result) {
        handler(error::not_found, nullptr);
        return;
    }

    auto const tx = std::make_shared<const transaction>(result.transaction());
    handler(error::success, tx);
}


#endif // KTH_DB_TRANSACTION_UNCONFIRMED

void block_chain::fetch_block_hash_timestamp(size_t height, block_hash_time_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, null_hash, 0, 0);
        return;
    }

    auto const result = database_.internal_db().get_header(height);

    if ( ! result.is_valid() ) {
        handler(error::not_found, null_hash, 0, 0);
        return;
    }

    handler(error::success, result.hash(), result.timestamp(), height);

}


void block_chain::fetch_block_height(hash_digest const& hash,
    block_height_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, {});
        return;
    }

    auto const result = database_.internal_db().get_header(hash);

    if ( ! result.first.is_valid() ) {
        handler(error::not_found, 0);
        return;
    }

    handler(error::success, result.second);
}


void block_chain::fetch_block_header(size_t height,
    block_header_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, nullptr, 0);
        return;
    }

    auto const result = database_.internal_db().get_header(height);

    if ( ! result.is_valid()) {
        handler(error::not_found, nullptr, 0);
        return;
    }

    auto const message = std::make_shared<header>(std::move(result));
    handler(error::success, message, height);
}

void block_chain::fetch_block_header(hash_digest const& hash,
    block_header_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, nullptr, 0);
        return;
    }

    auto const result = database_.internal_db().get_header(hash);

    if ( ! result.first.is_valid() ) {
        handler(error::not_found, nullptr, 0);
        return;
    }

    auto const message = std::make_shared<header>(std::move(result.first));
    handler(error::success, message, result.second);
}


void block_chain::fetch_last_height(last_height_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, {});
        return;
    }

    uint32_t last_height;

    if ( database_.internal_db().get_last_height(last_height) != database::result_code::success ) {
        handler(error::not_found, 0);
        return;
    }

    handler(error::success, last_height);
}

// This may execute over 2000 queries.
void block_chain::fetch_locator_block_headers(get_headers_const_ptr locator, hash_digest const& threshold, size_t limit, locator_block_headers_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, nullptr);
        return;
    }

    // This is based on the idea that looking up by block hash to get heights
    // will be much faster than hashing each retrieved block to test for stop.

    // Find the start block height.
    // If no start block is on our chain we start with block 0.
    size_t start = 0;
    for (auto const& hash: locator->start_hashes()) {
        auto const result = database_.internal_db().get_header(hash);
        if (result.first.is_valid()) {
            start = result.second; //result.height();
            break;
        }
    }

    // The begin block requested is always one after the start block.
    auto begin = *safe_add(start, size_t(1));

    // The maximum number of headers returned is 2000.
    auto end = *safe_add(begin, limit);

    // Find the upper threshold block height (peer-specified).
    if (locator->stop_hash() != null_hash) {
        // If the stop block is not on chain we treat it as a null stop.
        auto const result = database_.internal_db().get_header(locator->stop_hash());

        // Otherwise limit the end height to the stop block height.
        // If end precedes begin floor_subtract will handle below.
        if (result.first.is_valid()) {
            // end = std::min(result.height(), end);
            end = std::min(size_t(result.second), end);
        }
    }

    // Find the lower threshold block height (self-specified).
    if (threshold != null_hash) {
        // If the threshold is not on chain we ignore it.
        auto const result = database_.internal_db().get_header(threshold);

        // Otherwise limit the begin height to the threshold block height.
        // If begin exceeds end floor_subtract will handle below.
        if (result.first.is_valid()) {
            // begin = std::max(result.height(), begin);
            begin = std::max(size_t(result.second), begin);
        }
    }

    auto message = std::make_shared<headers>();
    message->elements().reserve(floor_subtract(end, begin));

    // Build the hash list until we hit end or the blockchain top.
    for (auto height = begin; height < end; ++height) {
        auto const result = database_.internal_db().get_header(height);

        // If not found then we are at our top.
        if ( ! result.is_valid()) {
            message->elements().shrink_to_fit();
            break;
        }
        message->elements().push_back(result);
    }
    handler(error::success, std::move(message));
}

// This may generally execute 29+ queries.
void block_chain::fetch_block_locator(block::indexes const& heights, block_locator_fetch_handler handler) const {

    if (stopped()) {
        handler(error::service_stopped, nullptr);
        return;
    }

    // Caller can cast get_headers down to get_blocks.
    auto message = std::make_shared<domain::message::get_headers>();
    auto& hashes = message->start_hashes();
    hashes.reserve(heights.size());

    for (auto const height : heights) {
        auto const result = database_.internal_db().get_header(height);
        if ( ! result.is_valid()) {
            handler(error::not_found, nullptr);
            break;
        }
        hashes.push_back(result.hash());
    }

    handler(error::success, message);
}

// Server Queries.
//-----------------------------------------------------------------------------
void block_chain::fetch_spend(const domain::chain::output_point& outpoint, spend_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, {});
        return;
    }

    #if defined(KTH_DB_SPENDS)
        auto point = database_.spends().get(outpoint);
    #else
        auto point = database_.internal_db().get_spend(outpoint);
    #endif

    if (point.hash() == null_hash) {
        handler(error::not_found, {});
        return;
    }

    handler(error::success, std::move(point));
}

void block_chain::fetch_history(short_hash const& address_hash, size_t limit, size_t from_height, history_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, {});
        return;
    }

#if defined(KTH_DB_HISTORY)
    handler(error::success, database_.history().get(address_hash, limit, from_height));
#else
    handler(error::success, database_.internal_db().get_history(address_hash, limit, from_height));
#endif

}

void block_chain::fetch_confirmed_transactions(const short_hash& address_hash, size_t limit, size_t from_height, confirmed_transactions_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, {});
        return;
    }

#if defined(KTH_DB_HISTORY)
    handler(error::success, database_.history().get_txns(address_hash, limit, from_height));
#else
    handler(error::success, database_.internal_db().get_history_txns(address_hash, limit, from_height));
#endif
}


#ifdef KTH_DB_STEALTH
void block_chain::fetch_stealth(const binary& filter, size_t from_height, stealth_fetch_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped, {});
        return;
    }

    handler(error::success, database_.stealth().scan(filter, from_height));
}
#endif // KTH_DB_STEALTH

// Transaction Pool.
//-----------------------------------------------------------------------------

// Same as fetch_mempool but also optimized for maximum possible block fee as
// limited by total bytes and signature operations.
void block_chain::fetch_template(merkle_block_fetch_handler handler) const {
    transaction_organizer_.fetch_template(handler);
}

// Fetch a set of currently-valid unconfirmed txs in dependency order.
// All txs satisfy the fee minimum and are valid at the next chain state.
// The set of blocks is limited in count to size. The set may have internal
// dependencies but all inputs must be satisfied at the current height.
void block_chain::fetch_mempool(size_t count_limit, uint64_t minimum_fee,
    inventory_fetch_handler handler) const {
    transaction_organizer_.fetch_mempool(count_limit, handler);
}

// Filters.
//-----------------------------------------------------------------------------

// This filters against all transactions (confirmed and unconfirmed).
void block_chain::filter_transactions(get_data_ptr message, result_handler handler) const {
    // This filters against all transactions (confirmed and unconfirmed).

    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    auto& inventories = message->inventories();

//TODO(fernando): Do we have to use the mempool when both KTH_DB_NEW_FULL and KTH_WITH_MEMPOOL are activated?
#if defined(KTH_WITH_MEMPOOL)
    auto validated_txs = mempool_.get_validated_txs_low();

    if (validated_txs.empty()) {
        handler(error::success);
        return;
    }

    for (auto it = inventories.begin(); it != inventories.end();) {
        auto found = validated_txs.find(it->hash());
        if (it->is_transaction_type() && found != validated_txs.end()) {
            it = inventories.erase(it);
        } else {
            ++it;
        }
    }
#else

    size_t out_height;
    size_t out_position;

    for (auto it = inventories.begin(); it != inventories.end();) {
        //Knuth: We don't store spent information
        if (it->is_transaction_type()
            //&& get_is_unspent_transaction(it->hash(), max_size_t, false))
            && get_transaction_position(out_height, out_position, it->hash(), false)) {
            it = inventories.erase(it);
        } else {
            ++it;
        }
    }

#endif

    handler(error::success);
}

// This may execute up to 500 queries.
// This filters against the block pool and then the block chain.
void block_chain::filter_blocks(get_data_ptr message, result_handler handler) const {

    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    // Filter through block pool first.
    block_organizer_.filter(message);
    auto& inventories = message->inventories();
    auto const& internal_db = database_.internal_db();

    for (auto it = inventories.begin(); it != inventories.end();) {
        if (it->is_block_type() && internal_db.get_header(it->hash()).first.is_valid()) {
            it = inventories.erase(it);
        } else {
            ++it;
        }
    }

    handler(error::success);
}

// Subscribers.
//-----------------------------------------------------------------------------

void block_chain::subscribe_blockchain(reorganize_handler&& handler) {
    // Pass this through to the organizer, which issues the notifications.
    block_organizer_.subscribe(std::move(handler));
}

void block_chain::subscribe_transaction(transaction_handler&& handler) {
    // Pass this through to the tx organizer, which issues the notifications.
    transaction_organizer_.subscribe(std::move(handler));
}

void block_chain::subscribe_ds_proof(ds_proof_handler&& handler) {
    // Pass this through to the tx organizer, which issues the notifications.
    transaction_organizer_.subscribe_ds_proof(std::move(handler));
}

void block_chain::unsubscribe() {
    block_organizer_.unsubscribe();
    transaction_organizer_.unsubscribe();
    transaction_organizer_.unsubscribe_ds_proof();
}

// Transaction Validation.
//-----------------------------------------------------------------------------

void block_chain::transaction_validate(transaction_const_ptr tx, result_handler handler) const {
    transaction_organizer_.transaction_validate(tx, handler);
}

// Organizers.
//-----------------------------------------------------------------------------

void block_chain::organize(block_const_ptr block, result_handler handler) {
    // This cannot call organize or stop (lock safe).
    block_organizer_.organize(block, handler);
}

void block_chain::organize(transaction_const_ptr tx, result_handler handler) {
    // This cannot call organize or stop (lock safe).
    transaction_organizer_.organize(tx, handler);
}

void block_chain::organize(double_spend_proof_const_ptr ds_proof, result_handler handler) {
    // This cannot call organize or stop (lock safe).
    transaction_organizer_.organize(ds_proof, handler);
}


// Properties (thread safe).
// ----------------------------------------------------------------------------

inline
bool block_chain::is_stale_fast() const {
    return is_stale();
}

bool block_chain::is_stale() const {
    // If there is no limit set the chain is never considered stale.
    if (notify_limit_seconds_ == 0) {
        return false;
    }

    auto const top = last_block_.load();

    // TODO(fernando): refactor this!
    // Knuth: get the last block if there is no cache
    uint32_t last_timestamp = 0;
    if ( ! top) {
        size_t last_height;
        if (get_last_height(last_height)) {
            domain::chain::header last_header;
            if (get_header(last_header, last_height)) {
                last_timestamp = last_header.timestamp();
            }
        }
    }
    auto const timestamp = top ? top->header().timestamp() : last_timestamp;
    return timestamp < floor_subtract(zulu_time(), notify_limit_seconds_);
}

settings const& block_chain::chain_settings() const {
    return settings_;
}

// protected
bool block_chain::stopped() const {
    return stopped_;
}

#if defined(KTH_WITH_MEMPOOL)
std::pair<std::vector<kth::mining::transaction_element>, uint64_t> block_chain::get_block_template() const {
    return mempool_.get_block_template();
}
#endif

}} // namespace kth::blockchain