// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/pools/block_organizer.hpp>

#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <utility>

#include <kth/blockchain/interface/fast_chain.hpp>
#include <kth/blockchain/pools/block_pool.hpp>
#include <kth/blockchain/pools/branch.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/validate_block.hpp>
#include <kth/domain.hpp>

namespace kth::blockchain {

using namespace kd::chain;
using namespace kd::config;
using namespace std::placeholders;

#define NAME "block_organizer"

// Database access is limited to: push, pop, last-height, branch-work,
// validator->populator:
// spend: { spender }
// block: { bits, version, timestamp }
// transaction: { exists, height, output }

#if defined(KTH_WITH_MEMPOOL)
block_organizer::block_organizer(prioritized_mutex& mutex, dispatcher& dispatch, threadpool& thread_pool, fast_chain& chain, settings const& settings, domain::config::network network, bool relay_transactions, mining::mempool& mp)
#else
block_organizer::block_organizer(prioritized_mutex& mutex, dispatcher& dispatch, threadpool& thread_pool, fast_chain& chain, settings const& settings, domain::config::network network, bool relay_transactions)
#endif
    : fast_chain_(chain)
    , mutex_(mutex)
    , stopped_(true)
    , dispatch_(dispatch)
    , block_pool_(settings.reorganization_limit)
#if defined(KTH_WITH_MEMPOOL)
    , validator_(dispatch, fast_chain_, settings, network, relay_transactions, mp)
#else
    , validator_(dispatch, fast_chain_, settings, network, relay_transactions)
#endif
    , subscriber_(std::make_shared<reorganize_subscriber>(thread_pool, NAME))

#if defined(KTH_WITH_MEMPOOL)
    , mempool_(mp)
#endif
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
    subscriber_->start();
    validator_.start();
    return true;
}

bool block_organizer::stop() {
    validator_.stop();
    subscriber_->stop();
    subscriber_->invoke(error::service_stopped, 0, {}, {});
    stopped_ = true;
    return true;
}

// Organize sequence.
//-----------------------------------------------------------------------------

// This is called from blockchain::organize.
void block_organizer::organize(block_const_ptr block, result_handler handler) {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_high_priority();

    if (stopped()) {
        mutex_.unlock_high_priority();
        handler(error::service_stopped);
        return;
    }

    // Reset the reusable promise.
    resume_ = std::promise<code>();

    result_handler const complete = std::bind(&block_organizer::signal_completion, this, _1);
    auto const check_handler = std::bind(&block_organizer::handle_check, this, _1, block, complete);

    // Checks that are independent of chain state.
    validator_.check(block, check_handler);

    // Wait on completion signal.
    // This is necessary in order to continue on a non-priority thread.
    // If we do not wait on the original thread there may be none left.
    auto ec = resume_.get_future().get();

    mutex_.unlock_high_priority();
    ///////////////////////////////////////////////////////////////////////////

    // Invoke caller handler outside of critical section.
    handler(ec);
}

// private
void block_organizer::signal_completion(code const& ec) {
    // This must be protected so that it is properly cleared.
    // Signal completion, which results in original handler invoke with code.
    resume_.set_value(ec);
}

// Verify sub-sequence.
//-----------------------------------------------------------------------------

// private
void block_organizer::handle_check(code const& ec, block_const_ptr block, result_handler handler) {

    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    if (ec) {
        handler(ec);
        return;
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
    if (branch->empty() || fast_chain_.get_block_exists(block->hash())) {
        handler(error::duplicate_block);
        return;
    }

    if ( ! set_branch_height(branch)) {
        handler(error::orphan_block);
        return;
    }


    auto const accept_handler = std::bind(&block_organizer::handle_accept, this, _1, branch, handler);

    // Checks that are dependent on chain state and prevouts.
    validator_.accept(branch, accept_handler);
}

// private
void block_organizer::handle_accept(code const& ec, branch::ptr branch, result_handler handler) {
    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    if (ec) {
        handler(ec);
        return;
    }

    auto const connect_handler = std::bind(&block_organizer::handle_connect, this, _1, branch, handler);

    // Checks that include script validation.
    validator_.connect(branch, connect_handler);
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

#if defined(KTH_WITH_MEMPOOL)

//TODO(fernando): similar function in populate_block class
void block_organizer::populate_prevout_1(branch::const_ptr branch, domain::chain::output_point const& outpoint, bool require_confirmed) const {
    // The previous output will be cached on the input's outpoint.
    auto& prevout = outpoint.validation;

    auto const branch_height = branch->height();

    prevout.spent = false;
    prevout.confirmed = false;
    prevout.cache = domain::chain::output{};
    prevout.from_mempool = false;

    // If the input is a coinbase there is no prevout to populate.
    if (outpoint.is_null()) {
        return;
    }

    //TODO(fernando): check the value of the parameters: branch_height and require_confirmed
    if ( ! fast_chain_.get_utxo(prevout.cache, prevout.height, prevout.median_time_past, prevout.coinbase, outpoint, branch_height)) {
        // std::println("{}", "outpoint not found in UTXO: " << encode_hash(outpoint.hash()) << " - " << outpoint.index());
        return;
    }

    // BUGBUG: Spends are not marked as spent by unconfirmed transactions.
    // So tx pool transactions currently have no double spend limitation.
    // The output is spent only if by a spend at or below the branch height.
    auto const spend_height = prevout.cache.validation.spender_height;

    // The previous output has already been spent (double spend).
    if ((spend_height <= branch_height) && (spend_height != output::validation::not_spent)) {
        prevout.spent = true;
        prevout.confirmed = true;
        prevout.cache = domain::chain::output{};
    }
}

//TODO(fernando): similar function in populate_block class
void block_organizer::populate_prevout_2(branch::const_ptr branch, output_point const& outpoint, local_utxo_set_t const& branch_utxo) const {
    if ( ! outpoint.validation.spent) {
        branch->populate_spent(outpoint);
    }

    // Populate the previous output even if it is spent.
    if ( ! outpoint.validation.cache.is_valid()) {
        branch->populate_prevout(outpoint, branch_utxo);
    }
}

//TODO(fernando): similar function in populate_block class
void block_organizer::populate_transaction_inputs(branch::const_ptr branch, domain::chain::input::list const& inputs, local_utxo_set_t const& branch_utxo) const {
    // auto const branch_height = branch->height();

    for (auto const& input : inputs) {
        auto const& prevout = input.previous_output();
        populate_prevout_1(branch, prevout, true);            //Populate from Database
        populate_prevout_2(branch, prevout, branch_utxo);     //Populate from the Blocks in the Branch
    }
}

//TODO(fernando): similar function in populate_block class
void block_organizer::populate_transactions(branch::const_ptr branch, domain::chain::block const& block, local_utxo_set_t const& branch_utxo) const {

    auto const& txs = block.transactions();

    // Must skip coinbase here as it is already accounted for.
    for (auto tx = txs.begin() + 1; tx != txs.end(); ++tx) {
        auto const& inputs = tx->inputs();
        populate_transaction_inputs(branch, inputs, branch_utxo);
    }
}

local_utxo_set_t create_outgoing_utxo_set(block_const_ptr_list_ptr const& outgoing_blocks) {
    local_utxo_set_t res;
    res.reserve(outgoing_blocks->size());

    for (auto const& block : *outgoing_blocks) {
        // std::println("{}", "create_branch_utxo_set - block: {" << encode_hash(block->hash()) << "}");
        res.push_back(create_local_utxo_set(*block));
    }

    return res;
}

void block_organizer::organize_mempool(branch::const_ptr branch, block_const_ptr_list_const_ptr const& incoming_blocks, block_const_ptr_list_ptr const& outgoing_blocks) {

    std::unordered_set<hash_digest> txs_in;
    std::unordered_set<domain::chain::point> prevouts_in;

    for (auto const& block : *incoming_blocks) {
        if (block->transactions().size() > 1) {

            //TODO(fernando): Remove!!!!
            // std::println("src/blockchain/src/pools/block_organizer.cpp", "Arrive Block -------------------------------------------------------------------");
            // std::println("src/blockchain/src/pools/block_organizer.cpp", encode_hash(block->hash()));
            // std::println("src/blockchain/src/pools/block_organizer.cpp", "--------------------------------------------------------------------------------");


            mempool_.remove(block->transactions().begin() + 1, block->transactions().end(), block->non_coinbase_input_count());

            if ( ! fast_chain_.is_stale_fast() && ! outgoing_blocks->empty()) {
                std::for_each(block->transactions().begin() + 1, block->transactions().end(), [&txs_in, &prevouts_in](domain::chain::transaction const& tx){
                    txs_in.insert(tx.hash());

                    for (auto const& input : tx.inputs()) {
                        prevouts_in.insert(input.previous_output());
                    }
                });
            }
        }
    }

    if ( ! fast_chain_.is_stale_fast() && ! outgoing_blocks->empty()) {
        auto branch_utxo = create_outgoing_utxo_set(outgoing_blocks);

        for (auto const& block : *outgoing_blocks) {

            // std::println("{}", "Inserting Block in Mempool: " << encode_hash(block->hash()));

            if (block->transactions().size() > 1) {
                std::for_each(block->transactions().begin() + 1, block->transactions().end(),
                [this, branch, &txs_in, &prevouts_in, &branch_utxo](domain::chain::transaction const& tx) {
                    auto it = txs_in.find(tx.hash());
                    if (it == txs_in.end()) {


                        auto double_spend = std::any_of(tx.inputs().begin(), tx.inputs().end(), [this, &prevouts_in](domain::chain::input const& in) {
                            return prevouts_in.find(in.previous_output()) != prevouts_in.end();
                        });

                        if ( ! double_spend) {
                            tx.validation.state = fast_chain_.chain_state();
                            populate_transaction_inputs(branch, tx.inputs(), branch_utxo);
                            mempool_.add(tx);       //TODO(fernando): add bulk
                        }
                    }
                });
            }
        }
    }
}
#endif // defined(KTH_WITH_MEMPOOL)

// private
void block_organizer::handle_connect(code const& ec, branch::ptr branch, result_handler handler) {

    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    if (ec) {
        handler(ec);
        return;
    }

    auto& top_block = branch->top()->validation;
    top_block.error = error::success;

    auto& top_header = branch->top()->header().validation;
    top_header.median_time_past = top_block.state->median_time_past();
    top_header.height = branch->top_height();

    uint256_t threshold;
    auto const work = branch->work();
    auto const first_height = branch->height() + 1u;
    top_block.start_notify = asio::steady_clock::now();

    // The chain query will stop if it reaches work level.
    if ( ! fast_chain_.get_branch_work(threshold, work, first_height)) {
        handler(error::branch_work_failed);
        return;
    }

    // TODO(legacy): consider relay of pooled blocks by modifying subscriber semantics.
    if (work <= threshold) {
        if ( ! top_block.simulate) {
            block_pool_.add(branch->top());
        }

        handler(error::insufficient_work);
        return;
    }

    //Note(fernando): If there is just one block, internal double spend was checked previously.
    if (branch->blocks() && branch->blocks()->size() > 1) {
        if (is_branch_double_spend(branch)) {
            handler(error::double_spend);
            return;
        }
    }

    // TODO(legacy): create a simulated validation path that does not block others.
    if (top_block.simulate) {
        handler(error::success);
        return;
    }


#if ! defined(KTH_DB_READONLY)
    // Get the outgoing blocks to forward to reorg handler.
    auto const out_blocks = std::make_shared<block_const_ptr_list>();
    auto const reorganized_handler = std::bind(&block_organizer::handle_reorganized, this, _1, branch, out_blocks, handler);

    // Replace! Switch!
    //#########################################################################
    // Incoming blocks must have median_time_past set.
    fast_chain_.reorganize(branch->fork_point(), branch->blocks(), out_blocks, dispatch_, reorganized_handler);
    //#########################################################################
#endif // ! defined(KTH_DB_READONLY)
}

#if ! defined(KTH_DB_READONLY)
// private
// Outgoing blocks must have median_time_past set.
void block_organizer::handle_reorganized(code const& ec, branch::const_ptr branch, block_const_ptr_list_ptr outgoing, result_handler handler) {
    if (ec) {
        spdlog::critical("[blockchain] Failure writing block to store, is now corrupted: {}", ec.message());
        handler(ec);
        return;
    }

    block_pool_.remove(branch->blocks());
    block_pool_.prune(branch->top_height());
    block_pool_.add(outgoing);


#if defined(KTH_WITH_MEMPOOL)
    organize_mempool(branch, branch->blocks(), outgoing);
#endif

    // v3 reorg block order is reverse of v2, branch.back() is the new top.
    notify(branch->height(), branch->blocks(), outgoing);

    fast_chain_.prune_reorg_async();
    //fast_chain_.set_database_flags();

    handler(error::success);
}
#endif // ! defined(KTH_DB_READONLY)

// Subscription.
//-----------------------------------------------------------------------------

// private
void block_organizer::notify(size_t branch_height, block_const_ptr_list_const_ptr branch, block_const_ptr_list_const_ptr original) {
    // This invokes handlers within the criticial section (deadlock risk).
    subscriber_->invoke(error::success, branch_height, branch, original);
}

void block_organizer::subscribe(reorganize_handler&& handler) {
    subscriber_->subscribe(std::move(handler), error::service_stopped, 0, {}, {});
}

void block_organizer::unsubscribe() {
    subscriber_->relay(error::success, 0, {}, {});
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
    size_t height;

    // Get blockchain parent of the oldest branch block.
    if ( ! fast_chain_.get_height(height, branch->hash())) {
        return false;
    }

    branch->set_height(height);
    return true;
}

} // namespace kth::blockchain