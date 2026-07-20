// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/pools/block_organizer.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <utility>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/blockchain/pools/block_pool.hpp>
#include <kth/blockchain/pools/branch.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/validate_block.hpp>
#include <kth/domain.hpp>

#include <asio/co_spawn.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

namespace kth::blockchain {

using namespace kd::chain;
using namespace kd::config;

#define NAME "block_organizer"

// Database access is limited to: push, pop, last-height, branch-work,
// validator->populator:
// spend: { spender }
// block: { bits, version, timestamp }
// transaction: { exists, height, output }

block_organizer::block_organizer(prioritized_mutex& mutex, executor_type executor, size_t threads, threadpool& thread_pool, block_chain& chain, settings const& settings, domain::config::network network, bool relay_transactions)
    : chain_(chain)
    , mutex_(mutex)
    , stopped_(true)
    , executor_(std::move(executor))
    , threads_(threads)
    , block_pool_(settings.reorganization_limit, chain_.block_validations())
    , validator_(executor_, threads_, chain_, settings, network, relay_transactions)
    , broadcaster_(executor_)

{}

// Properties.
//-----------------------------------------------------------------------------

bool block_organizer::stopped() const {
    return stopped_;
}

// Start/stop sequences.
//-----------------------------------------------------------------------------

bool block_organizer::start() {
    stopped_ = false;
    broadcaster_.start();
    validator_.start();
    return true;
}

bool block_organizer::stop() {
    validator_.stop();
    broadcaster_.stop();
    stopped_ = true;
    return true;
}

// Organize sequence.
//-----------------------------------------------------------------------------

::asio::awaitable<code> block_organizer::organize(block_const_ptr block, bool headers_pre_validated) {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    // The mutex is held across `co_await` calls below (validator_.check,
    // populator_.populate, ...). This serialises concurrent `organize()`
    // callers against each other — necessary for now because the legacy
    // pool/validator code paths assume single-writer semantics. Replacing
    // this with a serialising channel (CSP) would unblock higher-priority
    // tasks while a block validates; that refactor is out of scope here.
    mutex_.lock_high_priority();

    if (stopped()) {
        mutex_.unlock_high_priority();
        co_return error::service_stopped;
    }

    // The block hasn't been checked yet.
    if (block->transactions().empty()) {
        mutex_.unlock_high_priority();
        co_return error::success;
    }

    // Checks that are independent of chain state.
    // For headers-first sync, skip header validation since headers were already validated.
    auto ec = co_await validator_.check(block, headers_pre_validated);

    if (stopped()) {
        mutex_.unlock_high_priority();
        co_return error::service_stopped;
    }

    if (ec) {
        mutex_.unlock_high_priority();
        co_return ec;
    }

    // Verify the last branch block (all others are verified).
    // Get the path through the block forest to the new block.
    auto const branch = block_pool_.get_path(block);

    //*************************************************************************
    // CONSENSUS: This is the same check performed by satoshi, yet it will
    // produce a chain split in the case of a hash collision. This is because
    // it is not applied at the branch point, so some nodes will not see the
    // collision block and others will, depending on block order of arrival.
    //*************************************************************************
    if (branch->empty() || chain_.block_exists(block->hash())) {
        mutex_.unlock_high_priority();
        co_return error::duplicate_block;
    }

    if ( ! set_branch_height(branch)) {
        mutex_.unlock_high_priority();
        co_return error::orphan_block;
    }

    // The transaction_validation_store holds only in-flight validation scratch:
    // a block's txs get entries (populated during accept below) that are dead
    // once the block leaves this organize() call, whatever the outcome. Erasing
    // runs under the high-priority validation mutex held across this whole
    // coroutine (the only writer of the store), so it is race-free. If an erased
    // block is later reorganized in, the populator recreates the entries it
    // needs (a missing entry only forces harmless re-validation, never a wrong
    // result). This is the primary fix for the per-block-tx IBD leak.
    auto const erase_block_tx_validations = [this](block_const_ptr const& blk) {
        for (auto const& tx : blk->transactions()) {
            chain_.transaction_validations().erase(tx.hash());
        }
    };

    // Checks that are dependent on chain state and prevouts.
    // For headers-first sync, use accept_body() to skip header validation.
    ec = co_await validator_.accept(branch, headers_pre_validated);

    if (stopped()) {
        erase_block_tx_validations(branch->top());
        mutex_.unlock_high_priority();
        co_return error::service_stopped;
    }

    if (ec) {
        erase_block_tx_validations(branch->top());
        mutex_.unlock_high_priority();
        co_return ec;
    }

    // Checks that include script validation.
    ec = co_await validator_.connect(branch);

    if (stopped()) {
        erase_block_tx_validations(branch->top());
        mutex_.unlock_high_priority();
        co_return error::service_stopped;
    }

    if (ec) {
        erase_block_tx_validations(branch->top());
        mutex_.unlock_high_priority();
        co_return ec;
    }

    // Validation of branch->top() is complete; nothing below reads this block's
    // tx-validation entries again (organize_mempool reads only the incoming tx
    // hashes and writes fresh entries for outgoing/re-added txs). Drop them here
    // so the store never accumulates across accepted/pooled blocks. Every mined
    // mempool tx is cleaned by this same erase when its block is accepted.
    erase_block_tx_validations(branch->top());

    auto const top_hash = branch->top()->hash();
    chain_.block_validations().mutate(top_hash, [](auto& bv){ bv.error = error::success; });
    bool simulate = false;
    chain_.block_validations().visit(top_hash, [&](auto const& bv){ simulate = bv.simulate; });
    // The header used to carry mutable validation.median_time_past /
    // .height stamped here. The header is now a pure value type; both
    // values are already available on the block's chain_state and the
    // branch's top_height(), so we no longer mutate the header.

    auto const work = branch->work();
    auto const first_height = branch->height() + 1u;

    // The chain query will stop if it reaches work level.
    auto const threshold = chain_.get_branch_work(work, first_height);
    if ( ! threshold) {
        mutex_.unlock_high_priority();
        co_return error::branch_work_failed;
    }

    // TODO(legacy): consider relay of pooled blocks by modifying subscriber semantics.
    if (work <= *threshold) {
        if ( ! simulate) {
            block_pool_.add(branch->top());
        }

        mutex_.unlock_high_priority();
        co_return error::insufficient_work;
    }

    //Note(fernando): If there is just one block, internal double spend was checked previously.
    if (branch->blocks() && branch->blocks()->size() > 1) {
        if (is_branch_double_spend(branch)) {
            mutex_.unlock_high_priority();
            co_return error::double_spend;
        }
    }

    // TODO(legacy): create a simulated validation path that does not block others.
    if (simulate) {
        mutex_.unlock_high_priority();
        co_return error::success;
    }

#if ! defined(KTH_DB_READONLY)
    // Replace! Switch!
    //#########################################################################
    // Incoming blocks must have median_time_past set.
    auto reorg_result = co_await chain_.reorganize(branch->fork_point(), branch->blocks());

    if ( ! reorg_result.has_value()) {
        spdlog::critical("[blockchain] Failure writing block to store, is now corrupted: {}", reorg_result.error().message());
        mutex_.unlock_high_priority();
        co_return reorg_result.error();
    }

    auto out_blocks = std::move(reorg_result.value());

    block_pool_.remove(branch->blocks());
    block_pool_.prune(branch->top_height());
    block_pool_.add(out_blocks);

    // Update the mempool for the reorganization: evict the newly-confirmed
    // (incoming) transactions and their conflicts; re-admit of the disconnected
    // (outgoing) transactions is tracked in #498. Common case: a single new
    // block extending the tip (out_blocks empty) => plain removal.
    chain_.mempool_ref().update_for_reorg(out_blocks, branch->blocks());


    // v3 reorg block order is reverse of v2, branch.back() is the new top.
    notify(branch->height(), branch->blocks(), out_blocks);

    chain_.prune_reorg_async();
    //#########################################################################
#endif // ! defined(KTH_DB_READONLY)

    mutex_.unlock_high_priority();
    co_return ec;
}

bool block_organizer::is_branch_double_spend(branch::ptr const& branch) const {
    // precondition: branch->blocks() != nullptr

    auto blocks = *branch->blocks();
    size_t non_coinbase_inputs = 0;

    for (auto const& block : blocks) {
        non_coinbase_inputs += block->non_coinbase_input_count();
    }

    point::list outs;
    outs.reserve(non_coinbase_inputs);

    // Merge the prevouts of all non-coinbase transactions into one set.
    for (auto const& block : blocks) {
        auto const& txs = block->transactions();
        for (auto tx = txs.begin() + 1; tx != txs.end(); ++tx) {
            auto out = tx->previous_outputs();
            std::move(out.begin(), out.end(), std::inserter(outs, outs.end()));
        }
    }

    std::sort(outs.begin(), outs.end());
    auto const distinct_end = std::unique(outs.begin(), outs.end());
    auto const distinct = (distinct_end == outs.end());
    return !distinct;
}


// Subscription.
//-----------------------------------------------------------------------------

void block_organizer::notify(size_t branch_height, block_const_ptr_list_const_ptr branch, block_const_ptr_list_const_ptr original) {
    broadcaster_.publish(branch_height, branch, original);
}

block_organizer::block_broadcaster::channel_ptr block_organizer::subscribe() {
    return broadcaster_.subscribe();
}

void block_organizer::unsubscribe(block_broadcaster::channel_ptr const& channel) {
    broadcaster_.unsubscribe(channel);
}

// Queries.
//-----------------------------------------------------------------------------

void block_organizer::filter(get_data_ptr message) const {
    block_pool_.filter(message);
}

// Utility.
//-----------------------------------------------------------------------------

// TODO(legacy): store this in the block pool and avoid this query.
bool block_organizer::set_branch_height(branch::ptr branch) {
    // Get blockchain parent of the oldest branch block.
    auto const height = chain_.get_height(branch->hash());
    if ( ! height) {
        return false;
    }

    branch->set_height(*height);
    return true;
}

} // namespace kth::blockchain
