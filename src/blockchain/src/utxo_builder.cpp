// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/utxo_builder.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <spdlog/spdlog.h>

#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <utxoz/utils.hpp>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/database/databases/utxoz_database.hpp>
#include <kth/database/native_file.hpp>
#include <kth/domain/chain/block.hpp>
#include <kth/domain/machine/opcode.hpp>
namespace kth::blockchain {

// =============================================================================
// Minimal raw block parser for UTXO indexing
// =============================================================================

expect<utxo_compact_block> parse_utxo_block(byte_span raw_block) {
    byte_reader r(raw_block);

    // Skip block header (80 bytes)
    if (auto s = r.skip(80); !s) return std::unexpected(s.error());

    auto const tx_count_exp = r.read_variable_little_endian();
    if (!tx_count_exp) return std::unexpected(tx_count_exp.error());
    auto const tx_count = *tx_count_exp;

    utxo_compact_block result;

    for (uint64_t tx_i = 0; tx_i < tx_count; ++tx_i) {
        bool const is_coinbase = (tx_i == 0);
        auto const tx_start = r.position();

        // Skip version (4 bytes)
        if (auto s = r.skip(4); !s) return std::unexpected(s.error());

        // --- Inputs ---
        auto const input_count_exp = r.read_variable_little_endian();
        if (!input_count_exp) return std::unexpected(input_count_exp.error());

        for (uint64_t in_i = 0; in_i < *input_count_exp; ++in_i) {
            // Read prev_hash (32 bytes) + prev_index (4 bytes)
            auto const prev_hash = r.read_array<32>();
            if (!prev_hash) return std::unexpected(prev_hash.error());

            auto const prev_index = r.read_little_endian<uint32_t>();
            if (!prev_index) return std::unexpected(prev_index.error());

            if (!is_coinbase) {
                result.inputs.push_back({
                    utxoz::make_outpoint(std::span<uint8_t const, 32>{prev_hash->data(), 32}, *prev_index)
                });
            }

            // Skip input script
            auto const script_len = r.read_variable_little_endian();
            if (!script_len) return std::unexpected(script_len.error());
            if (auto s = r.skip(static_cast<size_t>(*script_len)); !s) return std::unexpected(s.error());

            // Skip sequence (4 bytes)
            if (auto s = r.skip(4); !s) return std::unexpected(s.error());
        }

        // --- Outputs ---
        auto const output_count_exp = r.read_variable_little_endian();
        if (!output_count_exp) return std::unexpected(output_count_exp.error());

        struct pending_output {
            uint32_t original_index;
            std::span<uint8_t const> raw;
            bool coinbase;
            uint32_t tx_start;
        };
        std::vector<pending_output> tx_outputs;

        for (uint64_t out_i = 0; out_i < *output_count_exp; ++out_i) {
            auto const out_start = r.position();

            // Skip value (8 bytes)
            if (auto s = r.skip(8); !s) return std::unexpected(s.error());

            // Read script length and remember where the script starts
            auto const script_len = r.read_variable_little_endian();
            if (!script_len) return std::unexpected(script_len.error());

            auto const script_start = r.position();
            if (auto s = r.skip(static_cast<size_t>(*script_len)); !s) return std::unexpected(s.error());

            auto const out_end = r.position();

            // Skip OP_RETURN outputs — provably unspendable, don't belong in the UTXO set
            bool const is_op_return = *script_len > 0 &&
                raw_block[script_start] == static_cast<uint8_t>(domain::machine::opcode::return_);
            if (is_op_return) continue;

            tx_outputs.push_back({
                static_cast<uint32_t>(out_i),
                raw_block.subspan(out_start, out_end - out_start),
                is_coinbase,
                static_cast<uint32_t>(tx_start)
            });
        }

        // Skip locktime (4 bytes)
        if (auto s = r.skip(4); !s) return std::unexpected(s.error());

        auto const tx_end = r.position();

        // Compute txid = SHA256d of the raw tx bytes
        auto const txid = bitcoin_hash(raw_block.subspan(tx_start, tx_end - tx_start));

        // Build raw_outpoint keys using the original output index and add to result
        for (auto const& out : tx_outputs) {
            result.outputs.push_back({
                utxoz::make_outpoint(std::span<uint8_t const, 32>{txid.data(), 32}, out.original_index),
                out.raw,
                out.coinbase,
                out.tx_start
            });
        }
    }

    return result;
}

// =============================================================================
// utxo_raw_value serialization
// =============================================================================
// Format: raw_output_bytes (wire) + height(4) + mtp(4) + coinbase(1)
// =============================================================================

namespace {

inline constexpr size_t utxo_raw_prefix_size = sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t); // height + mtp + coinbase

[[nodiscard]]
std::vector<uint8_t> serialize_utxo_raw_value(
    std::span<uint8_t const> raw_output,
    uint32_t height,
    uint32_t median_time_past,
    bool coinbase
) {
    // Layout MUST match utxo_entry::from_data (the database read path via
    // bytes_to_entry, i.e. get_utxo/find): the wire output first (self-delimiting),
    // then the fixed metadata. Writing the metadata first made from_data misparse
    // the value as an output and fail with "not found" for every full-mode UTXO
    // lookup.
    size_t const total = raw_output.size() + utxo_raw_prefix_size;
    std::vector<uint8_t> result(total);
    auto* p = result.data();

    // raw output bytes (value + varint + script)
    std::memcpy(p, raw_output.data(), raw_output.size());
    p += raw_output.size();

    // height (4 bytes LE)
    std::memcpy(p, &height, 4);
    p += 4;

    // median_time_past (4 bytes LE)
    std::memcpy(p, &median_time_past, 4);
    p += 4;

    // coinbase (1 byte)
    *p = coinbase ? uint8_t{1} : uint8_t{0};

    return result;
}

} // anonymous namespace

// =============================================================================
// utxo_raw_delta implementation
// =============================================================================

void utxo_raw_delta::merge(utxo_raw_delta&& other) {
    bloom_skipped_inserts += other.bloom_skipped_inserts;
    bloom_skipped_deletes += other.bloom_skipped_deletes;

    for (auto& [point, raw] : other.inserts) {
        inserts.emplace(point, std::move(raw));
    }

    for (auto const& [point, height] : other.deletes) {
        auto it = inserts.find(point);
        if (it != inserts.end()) {
            // Internal spend — created and spent within same batch
            inserts.erase(it);
        } else {
            deletes.emplace(point, height);
        }
    }
}

void utxo_raw_delta::clear() {
    inserts.clear();
    deletes.clear();
    bloom_skipped_inserts = 0;
    bloom_skipped_deletes = 0;
}

bool utxo_raw_delta::empty() const {
    return inserts.empty() && deletes.empty();
}

// =============================================================================
// Compact block processing (zero-copy path)
// =============================================================================

expect<utxo_raw_delta> process_compact_block_utxos(
    utxo_compact_block const& block,
    uint32_t height,
    uint32_t median_time_past,
    int16_t file_number,
    uint32_t block_data_pos,
    database::utxo_bloom_filter const* bloom
) {
#ifdef KTH_UTXOZ_REFERENCE_MODE
    // Checked once, before anything is built. Gated on the mode because this is
    // the only mode that stores the file number: full mode keeps the whole
    // output and never reads this parameter, so rejecting a block there would
    // refuse work over a field nothing uses.
    if (file_number < 0) {
        spdlog::error("[utxo_builder] block at height {} has no file number in the header "
            "index (-1); the index and the block store disagree, and building references "
            "against it would write UINT32_MAX into every entry", height);
        return std::unexpected(error::operation_failed);
    }
#endif

    utxo_raw_delta delta;

    // 1. Process all outputs (inserts)
    for (auto const& out : block.outputs) {
        // Bloom skip: output not in final UTXO set → will be spent before checkpoint
        if (bloom && ! bloom->may_contain(out.key)) {
            ++delta.bloom_skipped_inserts;
            continue;
        }

#ifdef KTH_UTXOZ_REFERENCE_MODE
        // Reference: 8-byte ref = {file_number, data_pos + tx_start}
        reference_utxo_ref ref{
            static_cast<uint32_t>(file_number),
            block_data_pos + out.tx_start
        };
        std::vector<uint8_t> value(8);
        std::memcpy(value.data(), &ref, 8);
#else
        auto value = serialize_utxo_raw_value(out.raw, height, median_time_past, out.coinbase);
#endif

        delta.inserts.emplace(
            out.key,
            utxo_raw_value{std::move(value), height}
        );
    }

    // 2. Process all inputs (deletes)
    for (auto const& in : block.inputs) {
        // Bloom skip: prevout not in final set → was never inserted
        if (bloom && ! bloom->may_contain(in.prev_key)) {
            ++delta.bloom_skipped_deletes;
            continue;
        }
        auto it = delta.inserts.find(in.prev_key);
        if (it != delta.inserts.end()) {
            // Internal spend within this block
            delta.inserts.erase(it);
        } else {
            delta.deletes.emplace(in.prev_key, height);
        }
    }

    return delta;
}

// =============================================================================
// utxo_delta implementation
// =============================================================================

void utxo_delta::merge(utxo_delta&& other) {
    // 1. Add other's inserts to our inserts
    for (auto& [point, entry] : other.inserts) {
        inserts.emplace(point, std::move(entry));
    }

    // 2. Process other's deletes
    for (auto const& [point, height] : other.deletes) {
        // If it's in our inserts, it's an internal spend within the batch
        auto it = inserts.find(point);
        if (it != inserts.end()) {
            // Internal spend detected - UTXO created and spent within same batch
            // spdlog::debug("[utxo_delta::merge] Internal spend: UTXO created at height {}, spent at height {} - {}:{}",
            //     it->second.height(), height, encode_hash(point.hash()), point.index());
            inserts.erase(it);
        } else {
            // Not in inserts -> real delete from existing UTXO set
            // spdlog::debug("[utxo_delta::merge] External delete: spent at height {} - {}:{}",
            //     height, encode_hash(point.hash()), point.index());
            deletes.emplace(point, height);
        }
    }
}

void utxo_delta::clear() {
    inserts.clear();
    deletes.clear();
}

bool utxo_delta::empty() const {
    return inserts.empty() && deletes.empty();
}

// =============================================================================
// Block Processing
// =============================================================================

utxo_delta process_block_utxos(
    domain::chain::block const& block,
    uint32_t height,
    uint32_t median_time_past
) {
    utxo_delta delta;

    auto const& txs = block.transactions();
    if (txs.empty()) {
        return delta;
    }

    // 1. FIRST: Process all outputs (inserts)

    // Coinbase transaction
    auto const& coinbase = txs.front();
    auto const coinbase_hash = coinbase.hash();
    uint32_t idx = 0;
    for (auto const& output : coinbase.outputs()) {
        delta.inserts.emplace(
            domain::chain::point{coinbase_hash, idx++},
            database::utxo_entry{output, height, median_time_past, true}
        );
    }

    // Non-coinbase transactions - outputs only
    for (size_t i = 1; i < txs.size(); ++i) {
        auto const& tx = txs[i];
        auto const tx_hash = tx.hash();
        idx = 0;
        for (auto const& output : tx.outputs()) {
            delta.inserts.emplace(
                domain::chain::point{tx_hash, idx++},
                database::utxo_entry{output, height, median_time_past, false}
            );
        }
    }

    // 2. THEN: Process all inputs (deletes)
    // Skip coinbase (index 0) - it has no real inputs
    for (size_t i = 1; i < txs.size(); ++i) {
        auto const& tx = txs[i];
        for (auto const& input : tx.inputs()) {
            auto const& prevout = input.previous_output();

            // If in inserts, it's an internal spend within this block
            auto it = delta.inserts.find(prevout);
            if (it != delta.inserts.end()) {
                // spdlog::debug("[process_block_utxos] Block {} internal spend: {}:{}",
                //     height, encode_hash(prevout.hash()), prevout.index());
                delta.inserts.erase(it);
            } else {
                // Not in inserts -> delete from existing UTXO set
                delta.deletes.emplace(prevout, height);
            }
        }
    }

    return delta;
}

// =============================================================================
// Batch Processing with thread pool (parallel)
// =============================================================================

::asio::awaitable<utxo_delta> process_blocks_parallel(
    ::asio::thread_pool& pool,
    std::vector<block_with_context> const& blocks
) {
    if (blocks.empty()) {
        co_return utxo_delta{};
    }

    auto const n = blocks.size();

    // Storage for results (each slot written by exactly one worker)
    std::vector<utxo_delta> results(n);

    // Atomic counter to track completion
    auto completed = std::make_shared<std::atomic<size_t>>(0);

    // Post each block processing to the thread pool
    for (size_t i = 0; i < n; ++i) {
        auto const& ctx = blocks[i];
        ::asio::post(pool, [&ctx, &results, completed, i]() {
            results[i] = process_block_utxos(*ctx.block, ctx.height, ctx.median_time_past);
            completed->fetch_add(1, std::memory_order_release);
        });
    }

    // Wait for all workers to complete (yield-friendly polling)
    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer timer(executor);
    while (completed->load(std::memory_order_acquire) < n) {
        timer.expires_after(std::chrono::microseconds(100));
        co_await timer.async_wait(::asio::use_awaitable);
    }

    // Merge all results in order (sequential)
    utxo_delta merged = std::move(results[0]);
    for (size_t i = 1; i < n; ++i) {
        merged.merge(std::move(results[i]));
    }

    co_return merged;
}

// =============================================================================
// Batch Processing sequential (no thread pool)
// =============================================================================

// =============================================================================
// Apply Delta to Database
// DEPRECATED: UTXO storage moved to UTXOZ - this function is no longer used
// =============================================================================

database::result_code apply_utxo_delta(
    database::internal_database& db,
    utxo_delta const& delta
) {
    // LMDB UTXO storage removed - use UTXOZ instead via block_chain::apply_utxo_delta
    (void)db;
    (void)delta;
    spdlog::warn("[utxo_builder] apply_utxo_delta(internal_database) is deprecated - use block_chain::apply_utxo_delta instead");
    return database::result_code::other;
}

// =============================================================================
// UTXO Set Builder
// =============================================================================

namespace {

using block_const_ptr = domain::message::block::const_ptr;

constexpr size_t mtp_interval = 11;
constexpr size_t batch_size = 1000;

// Calculate median of timestamps (MTP algorithm)
[[nodiscard]]
uint32_t calculate_mtp(std::deque<uint32_t> const& timestamps) {
    if (timestamps.empty()) {
        return 0;
    }

    // Copy and sort
    std::vector<uint32_t> sorted(timestamps.begin(), timestamps.end());
    std::sort(sorted.begin(), sorted.end());

    // Return middle element (integer division gives floor)
    return sorted[sorted.size() / 2];
}

} // anonymous namespace

// =============================================================================
// Embedded Bloom Filter
// =============================================================================
// When KTH_HAS_EMBEDDED_BLOOM is defined, the bloom filter binary is embedded
// into the executable via .incbin (asm). Data lives in .rodata — no file I/O.
// When not defined, load_utxo_bloom() returns nullptr and IBD runs without
// bloom optimization (slower but correct).
// =============================================================================
// File format (inside the embedded blob):
//   magic        (4 bytes) = "KBLM"
//   version      (4 bytes) = 1
//   checkpoint_h (4 bytes) = checkpoint height
//   capacity     (8 bytes) = bloom filter capacity (bits)
//   array_size   (8 bytes) = bloom array byte count
//   array_data   (array_size bytes)
// =============================================================================

#ifdef KTH_HAS_EMBEDDED_BLOOM
extern "C" {
    extern const uint8_t kth_utxo_bloom_data[];
    extern const uint8_t kth_utxo_bloom_data_end[];
}
#endif

namespace {

constexpr std::array<char, 4> bloom_magic = {'K', 'B', 'L', 'M'};
constexpr uint32_t bloom_version = 1;
constexpr size_t bloom_header_size = 4 + 4 + 4 + 8 + 8;  // magic + version + height + capacity + array_size

} // anonymous namespace

bool save_utxo_bloom(
    block_chain& chain,
    std::filesystem::path const& data_dir,
    uint32_t checkpoint_height
) {
    auto const t0 = std::chrono::steady_clock::now();

    auto const utxo_count = chain.utxo_size();
    if (utxo_count == 0) {
        spdlog::warn("[bloom] No UTXOs to build bloom filter from");
        return false;
    }

    spdlog::info("[bloom] Building bloom filter from {} UTXOs (target FPR 1%)...", utxo_count);

    // Construct bloom filter with capacity = utxo_count and FPR = 0.01
    auto bloom = std::make_shared<database::utxo_bloom_filter>(utxo_count, 0.01);

    size_t inserted = 0;
    auto const window = chain.begin_utxo_write();
    if ( ! chain.utxo_for_each(window, [&](utxoz::raw_outpoint const& key) {
            bloom->insert(key);
            ++inserted;
        })) {
        // A filter built from part of the set is worse than none. This one is
        // consulted to decide which keys apply_delta_raw may SKIP, so every key
        // the walk did not reach becomes a delete that is never applied — a
        // spent output left in the set, from a file that failed to be read once.
        spdlog::error("[bloom] The UTXO set could not be walked in full ({} of {} key(s) "
            "visited); refusing to write a filter that would license skipping the rest",
            inserted, utxo_count);
        return false;
    }

    auto const build_elapsed = std::chrono::steady_clock::now() - t0;
    auto const build_ms = std::chrono::duration_cast<std::chrono::milliseconds>(build_elapsed).count();
    spdlog::info("[bloom] Filter built: {} keys inserted, capacity={} bits, in {}ms",
        inserted, bloom->capacity(), build_ms);

    // Serialize to disk
    auto const path = data_dir / fmt::format("utxo_bloom_{}.dat", checkpoint_height);
    std::filesystem::create_directories(data_dir);

    auto* fp = database::open_native(path, "wb");
    if ( ! fp) {
        spdlog::error("[bloom] Failed to open {} for writing", path.string());
        return false;
    }

    auto const arr = bloom->array();
    uint64_t const capacity = bloom->capacity();
    uint64_t const array_size = arr.size();

    // Write header
    std::fwrite(bloom_magic.data(), 1, bloom_magic.size(), fp);
    std::fwrite(&bloom_version, sizeof(bloom_version), 1, fp);
    std::fwrite(&checkpoint_height, sizeof(checkpoint_height), 1, fp);
    std::fwrite(&capacity, sizeof(capacity), 1, fp);
    std::fwrite(&array_size, sizeof(array_size), 1, fp);

    // Write bloom array
    std::fwrite(arr.data(), 1, array_size, fp);
    std::fclose(fp);

    auto const file_size = std::filesystem::file_size(path);
    auto const total_elapsed = std::chrono::steady_clock::now() - t0;
    auto const total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_elapsed).count();

    spdlog::info("[bloom] Saved bloom filter to {} ({} MB, {} keys, {}ms)",
        path.string(), file_size / (1024 * 1024), inserted, total_ms);

    return true;
}

std::shared_ptr<database::utxo_bloom_filter const> load_utxo_bloom() {
#ifndef KTH_HAS_EMBEDDED_BLOOM
    spdlog::info("[bloom] No embedded bloom filter — IBD will run without bloom optimization");
    return nullptr;
#else
    auto const t0 = std::chrono::steady_clock::now();

    auto const data_size = static_cast<size_t>(kth_utxo_bloom_data_end - kth_utxo_bloom_data);
    spdlog::info("[bloom] Embedded bloom data size: {} bytes", data_size);
    if (data_size < bloom_header_size) {
        spdlog::error("[bloom] Embedded bloom data too small: {} bytes", data_size);
        return nullptr;
    }

    auto const* p = kth_utxo_bloom_data;

    // Validate magic
    std::array<char, 4> magic{};
    std::memcpy(magic.data(), p, 4);
    p += 4;
    if (magic != bloom_magic) {
        spdlog::error("[bloom] Invalid magic in embedded bloom data");
        return nullptr;
    }

    // Validate version
    uint32_t version = 0;
    std::memcpy(&version, p, sizeof(version));
    p += sizeof(version);
    if (version != bloom_version) {
        spdlog::error("[bloom] Unsupported embedded bloom version: {}", version);
        return nullptr;
    }

    // Read checkpoint height (informational)
    uint32_t stored_height = 0;
    std::memcpy(&stored_height, p, sizeof(stored_height));
    p += sizeof(stored_height);

    // Read capacity and array size
    uint64_t capacity = 0;
    std::memcpy(&capacity, p, sizeof(capacity));
    p += sizeof(capacity);

    uint64_t array_size = 0;
    std::memcpy(&array_size, p, sizeof(array_size));
    p += sizeof(array_size);

    if (bloom_header_size + array_size > data_size) {
        spdlog::error("[bloom] Embedded array size {} exceeds data bounds", array_size);
        return nullptr;
    }

    // Reconstruct filter and copy array data from .rodata into filter's storage
    auto bloom = std::make_shared<database::utxo_bloom_filter>(capacity);

    auto arr = bloom->array();
    if (arr.size() != array_size) {
        spdlog::error("[bloom] Array size mismatch: embedded={}, filter={}", array_size, arr.size());
        return nullptr;
    }

    std::memcpy(arr.data(), p, array_size);

    auto const elapsed = std::chrono::steady_clock::now() - t0;
    auto const elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    spdlog::info("[bloom] Loaded embedded bloom filter: capacity={} bits, {} MB, height={}, in {}ms",
        capacity, array_size / (1024 * 1024), stored_height, elapsed_ms);

    return bloom;
#endif
}

bool embedded_bloom_available() {
#ifdef KTH_HAS_EMBEDDED_BLOOM
    return true;
#else
    return false;
#endif
}

uint32_t embedded_bloom_checkpoint_height() {
#ifndef KTH_HAS_EMBEDDED_BLOOM
    return 0;
#else
    auto const data_size = static_cast<size_t>(kth_utxo_bloom_data_end - kth_utxo_bloom_data);
    if (data_size < bloom_header_size) {
        return 0;
    }
    auto const* p = kth_utxo_bloom_data;

    std::array<char, 4> magic{};
    std::memcpy(magic.data(), p, 4);
    p += 4;
    if (magic != bloom_magic) {
        return 0;
    }

    uint32_t version = 0;
    std::memcpy(&version, p, sizeof(version));
    p += sizeof(version);
    if (version != bloom_version) {
        return 0;
    }

    uint32_t stored_height = 0;
    std::memcpy(&stored_height, p, sizeof(stored_height));
    return stored_height;
#endif
}

} // namespace kth::blockchain
