// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/validate/batch_validate.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/validate_header.hpp>
#include <kth/blockchain/validate/validate_input.hpp>
#include <kth/domain/constants.hpp>
#include <kth/infrastructure/utility/assert.hpp>

namespace kth::blockchain {

using namespace kth::domain::chain;

namespace {

// Key for the intra-batch created-output and spent-output indexes.
struct bv_outpoint {
    hash_digest hash;
    uint32_t index;
    bool operator==(bv_outpoint const& o) const {
        return index == o.index && hash == o.hash;
    }
};

// The store's native key for a domain outpoint.
inline utxoz::raw_outpoint to_raw_outpoint(domain::chain::output_point const& op) {
    auto const h = op.hash();
    return utxoz::make_outpoint(std::span<uint8_t const, 32>{h.data(), 32}, op.index());
}

struct bv_outpoint_hasher {
    size_t operator()(bv_outpoint const& k) const noexcept {
        size_t h;
        std::memcpy(&h, k.hash.data(), sizeof(h));
        return h ^ (static_cast<size_t>(k.index) * 0x9e3779b97f4a7c15ull);
    }
};

// An output produced by a block inside the batch (not yet in UTXO-Z).
struct created_output {
    output const* out;
    size_t height;     // block height that created it
    bool coinbase;
};

// A prevout resolved from UTXO-Z (owns the output by value).
struct db_prevout {
    output out;
    size_t height;
    bool coinbase;
};

// One transaction to script-verify, filled during the serial pass and run in
// parallel. Verifying a whole transaction (rather than a single input) lets the
// signature-checker context be built once per tx instead of once per input.
// Flags are the per-height consensus flags for the block the tx belongs to.
struct verify_task {
    transaction const* tx;
    domain::script_flags_t flags;
    size_t height;   // for diagnostics and per-block SigChecks accounting
};

} // namespace

code enforce_sigcheck_limits(
    std::vector<sigcheck_entry> const& entries,
    std::vector<uint64_t> const& block_limits,
    uint64_t tx_limit) {

    std::vector<uint64_t> block_sigchecks(block_limits.size(), 0);
    size_t cur_tx = std::numeric_limits<size_t>::max();
    uint64_t tx_sigchecks = 0;

    for (auto const& e : entries) {
        // Precondition: every entry's block_index addresses block_limits.
        KTH_CONTRACT(e.block_index < block_limits.size());

        if (e.tx_index != cur_tx) {
            cur_tx = e.tx_index;
            tx_sigchecks = 0;
        }
        tx_sigchecks += e.sigchecks;
        if (tx_sigchecks > tx_limit) {
            return error::transaction_sigchecks_limit;
        }

        block_sigchecks[e.block_index] += e.sigchecks;
        if (block_sigchecks[e.block_index] > block_limits[e.block_index]) {
            return error::block_sigchecks_limit;
        }
    }
    return error::success;
}

code validate_block_batch(
    block_chain& chain,
    settings const& settings,
    domain::config::network network,
    std::vector<block_const_ptr> const& blocks,
    size_t start_height) {

    if (blocks.empty()) {
        return error::success;
    }

    // Per-height consensus flags: build the chain_state for the first block from
    // the header_index (same construction header validation uses) and advance it
    // one block at a time with from_pool_ptr. Using a fixed all-forks flag set
    // would misvalidate historical blocks (e.g. strict-DER before BIP66).
    validate_header const header_validator(settings, network);
    auto const& header_index = chain.headers();

    domain::chain::chain_state::ptr state;
    std::vector<domain::script_flags_t> block_flags(blocks.size());
    // Per-block dynamic SigChecks limit (consensus), one per block in the batch.
    std::vector<uint64_t> block_sigcheck_limit(blocks.size());

    boost::unordered_flat_map<bv_outpoint, created_output, bv_outpoint_hasher> created;
    boost::unordered_flat_set<bv_outpoint, bv_outpoint_hasher> spent;
    std::vector<verify_task> tasks;

    boost::unordered_flat_map<bv_outpoint, db_prevout, bv_outpoint_hasher> db_resolved;

    // This call's OWN batch of unresolved lookups. It never leaves this
    // function, and the store keeps no copy, so nothing here can be consumed by
    // a concurrent transaction validation and nothing of its can arrive here —
    // which is what stopped a foreign key from being read as a missing prevout
    // of this block (#646).
    std::vector<utxoz::lookup_request> pending;

    // -----------------------------------------------------------------------
    // Full intra-batch created-output index. Under BCH CTOR a tx may spend the
    // output of any other tx in the batch created at its own height or earlier.
    // -----------------------------------------------------------------------
    for (size_t i = 0; i < blocks.size(); ++i) {
        auto const height = start_height + i;
        for (auto const& tx : blocks[i]->transactions()) {
            auto const& outs = tx.outputs();
            auto const txid = tx.hash();
            bool const cb = tx.is_coinbase();
            for (uint32_t oi = 0; oi < outs.size(); ++oi) {
                created.insert({bv_outpoint{txid, oi}, created_output{&outs[oi], height, cb}});
            }
        }
    }

    // -----------------------------------------------------------------------
    // Phase 1 (serial): per-height consensus flags, double-spend detection, and
    // EMIT every UTXO-Z lookup. find()/get_utxo is a two-phase contract — its
    // not_resolved only means "the active versions cannot answer"; the older
    // lookup", NOT "does not exist". So we do not conclude anything here.
    // -----------------------------------------------------------------------
    for (size_t i = 0; i < blocks.size(); ++i) {
        auto const height = start_height + i;
        auto const& block = *blocks[i];

        if (i == 0) {
            auto const parent_idx = header_index.find(block.header().previous_block_hash());
            if (parent_idx == header_index::null_index) {
                // Parent header not in the index: cannot build chain_state (the
                // KTH_ASSERT downstream is a no-op in Release, so guard here).
                return error::not_found;
            }
            auto cs = header_validator.chain_state_at(
                block.header(), block.hash(), height, parent_idx, header_index);
            if ( ! cs) {
                return cs.error();
            }
            state = std::move(*cs);
        } else {
            state = domain::chain::chain_state::from_pool_ptr(*state, block);
        }
        if ( ! state) {
            return error::operation_failed;
        }
        block_flags[i] = state->enabled_flags();
        // Per-block dynamic SigChecks limit: the ABLA block-size limit at this
        // height divided by the fixed ratio, matching BCHN's
        // GetMaxBlockSigChecksCount(nMaxBlockSize).
        block_sigcheck_limit[i] =
            state->dynamic_max_block_size() / block_maxbytes_maxsigchecks_ratio;

        for (auto const& tx : block.transactions()) {
            if (tx.is_coinbase()) {
                continue;
            }
            for (auto const& in : tx.inputs()) {
                auto const& op = in.previous_output();
                bv_outpoint const key{op.hash(), op.index()};

                // Double spend within the batch (or a prevout claimed twice).
                if ( ! spent.insert(key).second) {
                    return error::double_spend;
                }

                // Resolved intra-batch (created at this height or earlier)?
                if (auto const it = created.find(key);
                    it != created.end() && it->second.height <= height) {
                    continue;
                }

                // Otherwise it must come from UTXO-Z. get_utxo() probes the
                // ACTIVE versions only; a miss is kept in a batch THIS call owns
                // and resolved after the loop. Nothing is queued inside the
                // store, so no other component can consume these and none of
                // theirs can arrive here.
                auto const utxo = chain.get_utxo(op, height);
                if (utxo) {
                    db_resolved.insert({key, db_prevout{utxo->output, utxo->height, utxo->coinbase}});
                    continue;
                }
                if (utxo.error() != database::result_code::not_resolved) {
                    // A storage failure, not an answer about this prevout.
                    // Reporting it as a missing input would reject a block over
                    // a disk that did not respond.
                    spdlog::error("[batch_validate] the UTXO store failed reading {}:{}; "
                        "refusing to judge these blocks",
                        encode_hash(op.hash()), op.index());
                    return error::operation_failed;
                }
                pending.emplace_back(to_raw_outpoint(op), height);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Second phase: resolve THIS call's batch against the older versions.
    //
    // The batch is ours and stays ours — resolve() borrows the span and keeps
    // nothing. That is what makes the `absent` list below a statement about
    // these prevouts and no one else's: before UTXO-Z 0.10.0 this drained a
    // global queue, so a lookup emitted by a concurrent transaction validation
    // could be consumed here, come back absent, and reject a valid block.
    // -----------------------------------------------------------------------
    if ( ! pending.empty()) {
        auto resolved = chain.utxo_resolve(pending);
        if ( ! resolved) {
            // version_unreadable / catalog_unreadable. A LOCAL storage failure,
            // never a consensus verdict: missing_previous_output says the block
            // spends something that does not exist, which would reject a block
            // that may be perfectly valid and is the kind of answer that gets
            // cached. Nothing was consumed, so this is retryable.
            spdlog::error("[batch_validate] resolving {} prevout lookup(s) failed "
                "(batch {}-{}); refusing to judge these blocks rather than calling "
                "unresolved prevouts missing",
                pending.size(), start_height, start_height + blocks.size() - 1);
            return error::operation_failed;
        }

        // Matched back BY KEY. The lists are deduplicated over distinct keys and
        // are not parallel to `pending`.
        for (auto const& [rk, entry] : resolved->found) {
            bv_outpoint key;
            std::memcpy(key.hash.data(), rk.data(), key.hash.size());
            std::memcpy(&key.index, rk.data() + key.hash.size(), sizeof(key.index));
            db_resolved.insert({key, db_prevout{entry.output(), entry.height(), entry.coinbase()}});
        }

        // `absent` is proven absence and only that — a version that could not be
        // read is an error above, not an entry here. Every one of these keys is
        // a prevout of THIS batch, so a missing one is an invalid block.
        if ( ! resolved->absent.empty()) {
            auto const& rk = resolved->absent.front();
            hash_digest h;
            uint32_t idx;
            std::memcpy(h.data(), rk.data(), h.size());
            std::memcpy(&idx, rk.data() + h.size(), sizeof(idx));
            spdlog::error("[batch_validate] prevout not found {}:{} ({} missing total, "
                "batch {}-{})", encode_hash(h), idx, resolved->absent.size(),
                start_height, start_height + blocks.size() - 1);
            return error::missing_previous_output;
        }
    }

    // -----------------------------------------------------------------------
    // Phase 2 (serial): now that every prevout is resolved, check coinbase
    // maturity, fees and coinbase value, and populate each input's prevout cache.
    // -----------------------------------------------------------------------
    for (size_t i = 0; i < blocks.size(); ++i) {
        auto const height = start_height + i;
        uint64_t block_fees = 0;
        uint64_t coinbase_value = 0;

        for (auto const& tx : blocks[i]->transactions()) {
            if (tx.is_coinbase()) {
                for (auto const& o : tx.outputs()) {
                    coinbase_value += o.value();
                }
                continue;
            }

            uint64_t in_value = 0;
            uint64_t out_value = 0;
            for (auto const& o : tx.outputs()) {
                out_value += o.value();
            }

            auto const& inputs = tx.inputs();
            for (uint32_t ii = 0; ii < inputs.size(); ++ii) {
                auto const& op = inputs[ii].previous_output();
                bv_outpoint const key{op.hash(), op.index()};

                output const* out = nullptr;
                size_t prevout_height = 0;
                bool prevout_coinbase = false;

                if (auto const it = created.find(key);
                    it != created.end() && it->second.height <= height) {
                    out = it->second.out;
                    prevout_height = it->second.height;
                    prevout_coinbase = it->second.coinbase;
                } else if (auto const it2 = db_resolved.find(key); it2 != db_resolved.end()) {
                    out = &it2->second.out;
                    prevout_height = it2->second.height;
                    prevout_coinbase = it2->second.coinbase;
                } else {
                    // Neither created in this batch nor resolved from the store,
                    // and the resolution above already returned every absence it
                    // proved. Reaching here means this prevout was never asked
                    // about, which is a defect in the collection loop rather
                    // than a fact about the block — so it is reported as a local
                    // failure, not as a consensus verdict.
                    spdlog::error("[batch_validate] prevout {}:{} spent@{} was never resolved "
                        "(batch_start={}, utxo_size={}); refusing to judge these blocks",
                        encode_hash(op.hash()), op.index(), height, start_height,
                        chain.utxo_size());
                    return error::operation_failed;
                }

                if (prevout_coinbase && height < prevout_height + coinbase_maturity) {
                    return error::coinbase_maturity;
                }

                op.validation.cache = *out;
                op.validation.coinbase = prevout_coinbase;
                op.validation.height = prevout_height;
                op.validation.spent = false;
                op.validation.confirmed = true;

                in_value += out->value();
            }

            if (in_value < out_value) {
                return error::spend_exceeds_value;
            }
            block_fees += (in_value - out_value);

            // One script-verification unit per transaction (all its inputs share
            // one signature-checker context).
            tasks.push_back(verify_task{&tx, block_flags[i], height});
        }

        auto const reward = block::subsidy(height, settings.retarget) + block_fees;
        if (coinbase_value > reward) {
            return error::coinbase_value_limit;
        }
    }

    // -----------------------------------------------------------------------
    // Phase 2 (parallel): script/signature verification, one transaction per task
    // (its inputs share a signature-checker context). Independent now that every
    // prevout cache is populated; split across the hardware. Each transaction's
    // total SigChecks is recorded and enforced against the per-tx / per-block
    // limits in Phase 3 below.
    // -----------------------------------------------------------------------
    if (tasks.empty()) {
        return error::success;
    }

    unsigned hw = std::thread::hardware_concurrency();
    size_t const workers = std::max<size_t>(1, std::min<size_t>(hw ? hw : 1, tasks.size()));

    std::atomic<bool> failed{false};
    std::mutex err_mutex;
    code first_error = error::success;
    // Total SigChecks per transaction; each worker writes its own slot (no contention).
    std::vector<size_t> task_sigchecks(tasks.size(), 0);

    auto run_bucket = [&](size_t bucket) {
      try {
        for (size_t t = bucket; t < tasks.size() && ! failed.load(std::memory_order_relaxed); t += workers) {
            auto const& task = tasks[t];
            auto const res = validate_input::verify_transaction(*task.tx, task.flags);
            if (res.first != error::success) {
                std::lock_guard<std::mutex> lk(err_mutex);
                if ( ! failed.exchange(true)) {
                    first_error = res.first;
                    // Re-run per input to pinpoint the culprit and emit a decisive
                    // diagnostic (everything needed to reproduce it in a standalone
                    // verify_script test). Only on failure, so it is off the hot path.
                    auto const& inputs = task.tx->inputs();
                    for (uint32_t ii = 0; ii < inputs.size(); ++ii) {
                        if (validate_input::verify_script(*task.tx, ii, task.flags).first == error::success) {
                            continue;
                        }
                        auto const& pv = inputs[ii].previous_output();
                        spdlog::error("[batch_validate] script FAIL h={} err={} flags={:#x} tx={} in={} prevout={}:{} value={} prevout_script={} input_script={}",
                            task.height, res.first.message(),
                            static_cast<uint64_t>(task.flags),
                            encode_hash(task.tx->hash()), ii,
                            encode_hash(pv.hash()), pv.index(),
                            pv.validation.cache.value(),
                            encode_base16(to_data_chunk(pv.validation.cache.script(), false)),
                            encode_base16(to_data_chunk(inputs[ii].script(), false)));
                        break;
                    }
                }
                return;
            }
            task_sigchecks[t] = res.second;
        }
      } catch (std::exception const& e) {
        // A worker thread must not let an exception escape (std::terminate).
        std::lock_guard<std::mutex> lk(err_mutex);
        if ( ! failed.exchange(true)) {
            first_error = error::operation_failed;
            spdlog::error("[batch_validate] script verification threw: {}", e.what());
        }
      }
    };

    std::vector<std::thread> pool;
    pool.reserve(workers - 1);
    for (size_t w = 1; w < workers; ++w) {
        pool.emplace_back(run_bucket, w);
    }
    run_bucket(0);
    for (auto& th : pool) {
        th.join();
    }

    if (first_error != error::success) {
        return first_error;
    }

    // -----------------------------------------------------------------------
    // Phase 3 (serial): enforce the SigChecks consensus limits from the per-tx
    // totals — max_tx_sigchecks per transaction and the dynamic ABLA limit per
    // block. Each task is one transaction, so there is one entry per task.
    // (BCHN stops as soon as a limiter is exceeded; validating the whole batch
    // first and checking the sums is equivalent for accept/reject, only less
    // DoS-optimal, which is acceptable on the IBD path.)
    // -----------------------------------------------------------------------
    std::vector<sigcheck_entry> entries;
    entries.reserve(tasks.size());
    for (size_t t = 0; t < tasks.size(); ++t) {
        entries.push_back(sigcheck_entry{
            tasks[t].height - start_height,
            t,   // one task is one transaction
            static_cast<uint64_t>(task_sigchecks[t])});
    }
    return enforce_sigcheck_limits(entries, block_sigcheck_limit, max_tx_sigchecks);
}

} // namespace kth::blockchain
