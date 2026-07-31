// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/validate/batch_validate.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
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

// One script-verification unit, filled during the serial pass and run in parallel.
// Flags are the per-height consensus flags for the block the tx belongs to.
struct verify_task {
    transaction const* tx;
    uint32_t input_index;
    domain::script_flags_t flags;
    size_t height;   // for diagnostics on failure
};

} // namespace

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

    boost::unordered_flat_map<bv_outpoint, created_output, bv_outpoint_hasher> created;
    boost::unordered_flat_set<bv_outpoint, bv_outpoint_hasher> spent;
    std::vector<verify_task> tasks;

    boost::unordered_flat_map<bv_outpoint, db_prevout, bv_outpoint_hasher> db_resolved;

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
    // key_not_found only means "not in the active file, queued as a deferred
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

                // Otherwise it must come from UTXO-Z: emit the lookup. A hit is
                // recorded now; a miss is queued and resolved after the loop.
                if (auto const utxo = chain.get_utxo(op, height); utxo) {
                    db_resolved.insert({key, db_prevout{utxo->output, utxo->height, utxo->coinbase}});
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Second phase of the find() contract: resolve every queued lookup by
    // sweeping the older UTXO-Z file versions. This MUST run before any deferred
    // deletion (block_tasks applies deletions only after this returns). Only keys
    // absent here truly do not exist.
    // -----------------------------------------------------------------------
    {
        auto [found, failed] = chain.utxo_process_pending_lookups();
        for (auto const& [rk, entry] : found) {
            bv_outpoint key;
            std::memcpy(key.hash.data(), rk.data(), key.hash.size());
            std::memcpy(&key.index, rk.data() + key.hash.size(), sizeof(key.index));
            db_resolved.insert({key, db_prevout{entry.output(), entry.height(), entry.coinbase()}});
        }
        // `failed` = outpoints absent from every UTXO-Z generation (the sweep is
        // authoritative). Every deferred lookup came from a batch input's prevout,
        // so a failed key is a genuinely-missing prevout -> invalid block.
        if ( ! failed.empty()) {
            auto const& rk = failed.front();
            hash_digest h;
            uint32_t idx;
            std::memcpy(h.data(), rk.data(), h.size());
            std::memcpy(&idx, rk.data() + h.size(), sizeof(idx));
            spdlog::error("[batch_validate] prevout not found {}:{} (deferred lookup failed; {} missing total, batch {}-{})",
                encode_hash(h), idx, failed.size(), start_height, start_height + blocks.size() - 1);
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
                    // Absent after the deferred-lookup sweep: the prevout truly does
                    // not exist -> invalid block.
                    spdlog::error("[batch_validate] prevout not found {}:{} spent@{} (batch_start={}, utxo_size={}, deferred_lookups_left={})",
                        encode_hash(op.hash()), op.index(), height, start_height,
                        chain.utxo_size(), chain.utxo_deferred_lookups_size());
                    return error::missing_previous_output;
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
                tasks.push_back(verify_task{&tx, ii, block_flags[i], height});
            }

            if (in_value < out_value) {
                return error::spend_exceeds_value;
            }
            block_fees += (in_value - out_value);
        }

        auto const reward = block::subsidy(height, settings.retarget) + block_fees;
        if (coinbase_value > reward) {
            return error::coinbase_value_limit;
        }
    }

    // -----------------------------------------------------------------------
    // Phase 2 (parallel): script/signature verification per input. Independent
    // now that every prevout cache is populated; split across the hardware.
    // TODO: enforce the per-block dynamic sigchecks limit (ABLA); per-tx limit is
    // enforced inside verify_script accumulation.
    // -----------------------------------------------------------------------
    if (tasks.empty()) {
        return error::success;
    }

    unsigned hw = std::thread::hardware_concurrency();
    size_t const workers = std::max<size_t>(1, std::min<size_t>(hw ? hw : 1, tasks.size()));

    std::atomic<bool> failed{false};
    std::mutex err_mutex;
    code first_error = error::success;

    auto run_bucket = [&](size_t bucket) {
      try {
        for (size_t t = bucket; t < tasks.size() && ! failed.load(std::memory_order_relaxed); t += workers) {
            auto const& task = tasks[t];
            auto const res = validate_input::verify_script(*task.tx, task.input_index, task.flags);
            if (res.first != error::success) {
                std::lock_guard<std::mutex> lk(err_mutex);
                if ( ! failed.exchange(true)) {
                    first_error = res.first;
                    // Decisive diagnostic: everything needed to reproduce the failing
                    // input in a standalone verify_script test.
                    auto const& in = task.tx->inputs()[task.input_index];
                    auto const& pv = in.previous_output();
                    spdlog::error("[batch_validate] script FAIL h={} err={} flags={:#x} tx={} in={} prevout={}:{} value={} prevout_script={} input_script={}",
                        task.height, res.first.message(),
                        static_cast<uint64_t>(task.flags),
                        encode_hash(task.tx->hash()), task.input_index,
                        encode_hash(pv.hash()), pv.index(),
                        pv.validation.cache.value(),
                        encode_base16(to_data_chunk(pv.validation.cache.script(), false)),
                        encode_base16(to_data_chunk(in.script(), false)));
                }
                return;
            }
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

    return first_error;
}

} // namespace kth::blockchain
