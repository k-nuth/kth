// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/blockchain/utxo_deletion_sweep.hpp>

#include <kth/blockchain/utxo_builder.hpp>
#include <kth/database/flat_file_pos.hpp>

#include <algorithm>
#include <mutex>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <functional>
#include <latch>
#include <memory>
#include <numeric>
#include <string>
#include <unordered_set>
#include <utility>

#include <kth/blockchain/pools/mempool_config.hpp>
#include <kth/blockchain/populate/populate_chain_state.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/database.hpp>
#include <kth/domain.hpp>
#include <kth/domain/multi_crypto_support.hpp>

#include <kth/infrastructure/math/sip_hash.hpp>
#include <kth/infrastructure/utility/limits.hpp>
#include <kth/infrastructure/utility/timer.hpp>

#include <asio/co_spawn.hpp>
#include <asio/awaitable.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/experimental/concurrent_channel.hpp>

#include <utxoz/logging.hpp>

namespace kth::blockchain {

using spent_value_type = std::pair<hash_digest, uint32_t>;
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


namespace blockchain {

using namespace kd::config;
using namespace kd::message;
using namespace kth::database;
using namespace std::placeholders;

namespace {

// TODO(mempool): KTH has no working mempool. The abandoned mining::mempool and
// the LMDB "transaction unconfirmed" storage that used to back the mempool
// queries and the tx sink have been removed to rebuild the mempool from
// scratch (concurrent hashmap + on-disk persistence). Until it lands, every
// mempool read/query and the tx sink abort loudly instead of silently
// returning wrong (empty) results. Tracked in issue #491.
[[noreturn]]
void mempool_not_implemented(char const* fn) {
    spdlog::critical(
        "[blockchain] {} was called but the mempool is not implemented "
        "(the LMDB unconfirmed-tx path was removed pending the new mempool; "
        "see TODO / issue #491). Aborting.", fn);
    std::abort();
}

} // anonymous namespace

#define NAME "block_chain"

static auto const hour_seconds = 3600u;

// =============================================================================
// CONSTRUCTION
// =============================================================================

block_chain::block_chain(blockchain::settings const& chain_settings,
                         database::settings const& database_settings,
                         domain::config::network network,
                         bool relay_transactions)
    : stopped_(true)
    , settings_(chain_settings)
    , notify_limit_seconds_(chain_settings.notify_limit_hours * hour_seconds)
    , chain_state_populator_(*this, chain_settings, network)
    , database_(database_settings)
    , validation_mutex_(relay_transactions)
    , priority_pool_("priority", std::min(size_t(8), thread_ceiling(chain_settings.cores)))
    , transaction_organizer_(validation_mutex_, priority_pool_.get_executor(), priority_pool_.size(), priority_pool_, *this, chain_settings)
    , block_organizer_(validation_mutex_, priority_pool_.get_executor(), priority_pool_.size(), priority_pool_, *this, chain_settings, network, relay_transactions)
    , utxoz_db_(utxo_gate_)
{
    spdlog::debug("[blockchain] block_chain constructor completed successfully");
    spdlog::info("[blockchain] Mempool backend: {}", mempool_backend_name);
}

block_chain::~block_chain() {
    (void)close();
}

// =============================================================================
// LIFECYCLE
// =============================================================================

bool block_chain::check_last_transition() const {
    auto const check = read_transition_record();

    switch (check.status) {
        case database::transition_status::clean:
            // The key is absent, which is the only thing that reads as clean.
            return true;

        case database::transition_status::unreadable:
            // "Could not read" is never reported as "clean". That conflation is
            // the failure this record exists to prevent, and its own reader
            // would be a poor place to reintroduce it.
            spdlog::critical("[blockchain] The transition record could not be read. Whether the "
                "last chain transition finished is unknown, so the UTXO set cannot be trusted. "
                "Rebuild or reindex the database; this node will not open it");
            return false;

        case database::transition_status::corrupt:
            spdlog::critical("[blockchain] The transition record is present and unreadable: {}. "
                "It cannot say whether the last chain transition finished. Rebuild or reindex "
                "the database; this node will not open it",
                check.decode_error ? database::to_string(*check.decode_error)
                                   : "no reason recorded");
            return false;

        case database::transition_status::recovery_required:
            // The type, the id and the range, because a diagnosis starts by
            // matching this against the log line the failing run wrote.
            spdlog::critical("[blockchain] The last {} (operation {:#018x}) over heights {}-{} "
                "started and was never recorded as finished. The UTXO set may be half-applied: "
                "it cannot be rolled back, because the delta mutates it in place, and it must "
                "not be resumed, because the built height names the transition BEFORE this one. "
                "Rebuild or reindex the database from an authoritative point; recovery is NOT "
                "automatic and this node will not open it",
                check.record ? database::to_string(check.record->type) : "transition",
                check.record ? check.record->operation_id : 0u,
                check.record ? check.record->first_height : 0u,
                check.record ? check.record->intended_last_height : 0u);
            return false;
    }

    // A status this build does not know is not a status this build may pass.
    spdlog::critical("[blockchain] The transition record reported a state this build does not "
        "know; refusing to open the database");
    return false;
}

bool block_chain::start(uint32_t disk_magic) {
    stopped_ = false;

    if ( ! database_.open()) {
        spdlog::error("[blockchain] Failed to open database.");
        return false;
    }

    // Before anything else is opened, and before a single height is believed:
    // did the last run finish what it started? (#600)
    //
    // Here rather than in the UTXO build task, because the answer decides
    // whether this database may be opened for operation at all. A node that
    // came up and only refused once it reached the build would already have
    // served reads off a set that may be half-applied.
    //
    // Nothing is repaired. The record says a transition did not finish; it
    // never says how to fix one, and there is no rollback to perform — the
    // delta mutates the UTXO maps in place, so an interrupted transition can be
    // neither reversed nor resumed. Rebuilding from an authoritative point is
    // the only sound answer, and until that procedure exists refusing beats
    // continuing on a set nothing can vouch for.
    if ( ! check_last_transition()) {
        return false;
    }

    // Open UTXO-Z database (in a subdirectory of the main database)
    utxoz::set_log_prefix("UTXO-Z");
    auto utxoz_path = database_.internal_db_dir.parent_path() / "utxoz";
    // Scoped to the open itself. Held for the rest of start() it would deadlock
    // against the reference-mode wiring below, which takes a window of its own —
    // and only in reference mode, so a full-mode build would never show it and
    // the node would hang at startup on the other one.
    {
        auto const lifecycle = utxo_gate_.write();
        if ( ! lifecycle) {
            spdlog::error("[blockchain] The UTXO gate has latched; the store cannot be opened "
                "({}). Restart the node so the transition record is consulted",
                database::result_code_name(lifecycle.error()));
            return false;
        }
        // Opening applies nothing, so it is never marked: a failed open leaves
        // the gate exactly as it found it.
        if ( ! utxoz_db_.with_write(*lifecycle, [&](auto& db) { return db.open(utxoz_path); })) {
            spdlog::error("[blockchain] Failed to open UTXO-Z database at {}",
                utxoz_path.string());
            return false;
        }
    }
    spdlog::info("[blockchain] UTXO-Z database opened at {}", utxoz_path.string());

#ifdef KTH_UTXOZ_REFERENCE_MODE
    // Reference mode find resolution requires block_store and header_index.
    // block_store_ is not yet initialized here — we wire it below after initialization.
#endif

    // Initialize flat file block storage
    // Convert disk magic to little-endian bytes for file header
    auto blocks_path = database_.internal_db_dir.parent_path() / "blocks";
    auto magic = to_little_endian(disk_magic);
    block_store_ = std::make_unique<database::block_store>(blocks_path, magic);
    if ( ! block_store_->initialize()) {
        spdlog::error("[blockchain] Failed to initialize block store at {}", blocks_path.string());
        return false;
    }
    spdlog::info("[blockchain] Block store initialized at {}", blocks_path.string());

    // From the connected tip, never the header tip: the two differ by the whole
    // of an unfinished sync, and the state describes what is connected.
    auto const connected = get_last_heights();
    if ( ! connected) {
        spdlog::error("[blockchain] Failed to read the last heights.");
        return false;
    }
    auto const reconciled = reconcile_connected_tip(connected->block);
    if ( ! reconciled) {
        // Fail-closed. An unreadable marker is not an absent one: continuing would
        // publish chain state at a height nothing vouches for, and disconnect_block
        // would then refuse or accept rewinds against it.
        spdlog::error("[blockchain] Cannot establish the connected tip: the UTXO "
            "height marker could not be read");
        return false;
    }
    if (auto const ec = publish_chain_view(*reconciled); ec) {
        spdlog::error("[blockchain] Failed to initialize chain state: {}", ec.message());
        return false;
    }

    if ( ! transaction_organizer_.start()) {
        spdlog::error("[blockchain] Failed to start transaction organizer.");
        return false;
    }

    if ( ! block_organizer_.start()) {
        spdlog::error("[blockchain] Failed to start block organizer.");
        return false;
    }

    // Load all headers from database into header_index
    // This allows resuming sync from where we left off
    // The chain by height as the database holds it, filled while headers load and
    // used below to check undo coverage. Declared out here because the check and
    // the load are in different scopes.
    std::vector<header_index::index_t> persisted_chain_indices;

    auto const heights = get_last_heights();
    if ( ! heights) {
        spdlog::error("[blockchain] Failed to get last heights from database.");
        return false;
    }

    spdlog::info("[blockchain] Database state: header_height={}, block_height={}", heights->header, heights->block);

    if (heights->header == 0) {
        // Only genesis in DB - just add genesis to index
        auto const genesis = get_header(0);
        if (genesis) {
            auto const hash = domain::chain::hash(*genesis);
            auto const [inserted, idx, capacity_warning] = header_index_.add(hash, *genesis);
            if ( ! inserted) {
                spdlog::error("[blockchain] Failed to initialize header index with genesis block.");
                return false;
            }
            spdlog::info("[blockchain] Header index initialized with genesis: {}", encode_hash(hash));
        }
    } else {
        // Load all headers from DB into header_index
        spdlog::info("[blockchain] Loading {} headers from database into header_index...", heights->header + 1);

        auto const load_start = std::chrono::steady_clock::now();

        // Load in batches to avoid memory spikes
        constexpr size_t batch_size = 10000;
        size_t loaded = 0;

        // The chain by height as LMDB holds it, recorded as it is loaded.
        //
        // Deliberately not called the active chain: the organizer publishes that
        // view after startup, so active_at() answers nothing here — a fact this
        // code learned by asking it and being told height one does not exist.
        // The persisted by-height table is the authoritative source available
        // during startup, and it is the one the UTXO set was built against, so
        // it is what undo coverage is checked against below.
        persisted_chain_indices.assign(heights->header + 1, header_index::null_index);

        for (size_t from = 0; from <= heights->header; from += batch_size) {
            auto const to = std::min(from + batch_size - 1, size_t(heights->header));
            auto const headers_result = get_headers(from, to);

            if ( ! headers_result) {
                spdlog::error("[blockchain] Failed to load headers from {} to {}", from, to);
                return false;
            }

            size_t height = from;
            for (auto const& header : *headers_result) {
                auto const hash = domain::chain::hash(header);
                auto const [inserted, idx, capacity_warning] = header_index_.add(hash, header);
                if ( ! inserted && height > 0) {
                    // Genesis might already be added, ignore that case
                    spdlog::warn("[blockchain] Failed to add header at height {} to index", height);
                }
                if (idx == header_index::null_index) {
                    spdlog::critical("[blockchain] The header at height {} has no index after "
                        "loading; the persisted chain cannot be reconstructed", height);
                    return false;
                }
                if (height >= persisted_chain_indices.size()) {
                    spdlog::critical("[blockchain] Loaded a header at height {}, past the {} the "
                        "database reports; the by-height table is inconsistent",
                        height, heights->header);
                    return false;
                }
                persisted_chain_indices[height] = idx;
                ++height;
                ++loaded;
            }

            // Log progress every 100k headers
            if (loaded % 100000 < batch_size && loaded > 0) {
                spdlog::info("[blockchain] Loaded {}/{} headers into index...", loaded, heights->header + 1);
            }
        }

        auto const elapsed = std::chrono::steady_clock::now() - load_start;
        auto const elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        spdlog::info("[blockchain] Loaded {} headers into header_index in {}ms", loaded, elapsed_ms);
    }

    // Restore block file positions in header_index from flat files.
    //
    // Unconditional now, and `heights->block > 0` is gone on purpose: the walks
    // are what authorise the store to append (#668), so skipping them on a fresh
    // database left it unable to write its first block. On a database with no
    // files they visit nothing and cost nothing.
    if (block_store_) {
        spdlog::info("[blockchain] Scanning flat files to restore block positions...");
        auto const scan_start = std::chrono::steady_clock::now();

        size_t restored = 0;
        auto const block_scan = block_store_->scan_block_positions(
            [this, &restored](int32_t file_num, uint32_t data_pos, hash_digest const& hash) {
                auto const idx = header_index_.find(hash);
                if (idx != header_index::null_index) {
                    header_index_.set_block_pos(idx, static_cast<int16_t>(file_num), data_pos);
                    header_index_.add_status(idx, header_status::have_data);
                    ++restored;
                }
            }
        );

        // The walk governs the write cursor now, so an anomaly is a refusal
        // rather than a shorter answer. It used to stop quietly on any of eight
        // conditions and report only how many records it had managed to read —
        // which, once the cursor comes from where it stopped, is the difference
        // between appending after the last block and appending on top of data
        // nobody could parse (#668).
        if ( ! block_scan.clean()) {
            spdlog::critical("[blockchain] The block files could not be read back "
                "(status {}, file {}, offset {}). Refusing to start on a chain whose block "
                "data is unaccounted for.",
                static_cast<int>(block_scan.status), block_scan.file_number,
                block_scan.position);
            return false;
        }
        auto const scanned = block_scan.found;

        // And the undo records, the same way and for the same reason: their
        // positions live only in this in-memory index, so without this pass a
        // restart leaves every block without undo and no reorganization can
        // disconnect anything (#603).
        //
        // Nothing is applied until the whole scan succeeds. A half-restored index
        // is worse than an empty one, because it looks complete: the blocks it
        // reached would report undo data and the rest would not, with no way to
        // tell which case a missing record is.
        auto const undo_scan = block_store_->scan_undo_positions(
            [this](hash_digest const& block_hash) -> std::optional<hash_digest> {
                auto const idx = header_index_.find(block_hash);
                if (idx == header_index::null_index) {
                    return std::nullopt;
                }
                auto const parent = header_index_.get_parent_index(idx);
                if (parent == header_index::null_index) {
                    return std::nullopt;
                }
                return header_index_.get_hash(parent);
            });

        if (undo_scan.status != block_store::undo_scan_status::clean_eof) {
            if (undo_scan.status == block_store::undo_scan_status::legacy_format) {
                spdlog::critical("[blockchain] This database's undo records predate the format "
                    "that records which block each one belongs to, and the association cannot be "
                    "recovered from what is on disk. Without it no reorganization can disconnect "
                    "a block connected before this start. The UTXO set and undo data have to be "
                    "rebuilt.");
            } else {
                spdlog::critical("[blockchain] The undo records could not be read back "
                    "(status {}, file {}, offset {}). Refusing to start on a chain whose undo "
                    "data is unaccounted for.",
                    static_cast<int>(undo_scan.status), undo_scan.file_number, undo_scan.position);
            }
            return false;
        }

        for (auto const& location : undo_scan.found) {
            auto const idx = header_index_.find(location.block_hash);
            if (idx == header_index::null_index) {
                // The scan already refused an unknown block, so this cannot
                // happen; if it somehow does, the index and the files disagree.
                spdlog::critical("[blockchain] An undo record survived the scan for a block the "
                    "index does not hold");
                return false;
            }
            header_index_.set_undo_pos(idx, location.position);
            header_index_.add_status(idx, header_status::have_undo);
        }
        spdlog::info("[blockchain] Restored {} undo positions ({} records belong to blocks this "
            "index no longer holds, which is ordinary after a reorganization)",
            undo_scan.found.size(), undo_scan.unattributed);

        // Skipping a record whose block is unknown is right for an abandoned
        // branch and would be wrong for a live one — and locally the two look the
        // same, since a single flipped bit in a stored hash makes an active
        // record unattributable. What separates them is coverage: every block on
        // the active chain that was connected with undo must have it now. Below
        // the checkpoint none is captured, and above the built height nothing is
        // connected, so the range between is exactly what has to be complete.
        auto const built = get_utxo_built_height();
        if ( ! built) {
            if (built.error() != database::result_code::key_not_found) {
                // Not knowing how far the set was built is not the same as
                // nothing having been built, and taking it for that would skip
                // the coverage check entirely — the failure this check exists to
                // catch, arriving through the check itself.
                spdlog::critical("[blockchain] The built height could not be read, so whether the "
                    "chain's undo data is complete cannot be established. Refusing to start.");
                return false;
            }
        } else {
            auto const first = static_cast<int32_t>(settings_.max_checkpoint_height) + 1;
            for (int32_t height = first; height <= static_cast<int32_t>(*built); ++height) {
                auto const at = static_cast<size_t>(height);
                if (at >= persisted_chain_indices.size() ||
                        persisted_chain_indices[at] == header_index::null_index) {
                    // The range is the part of the persisted chain that was
                    // connected. A height in it with no block means the coverage
                    // cannot be established rather than that there is nothing to
                    // cover.
                    spdlog::critical("[blockchain] No block at height {}, which the built height "
                        "says was connected. Refusing to start on a chain whose undo coverage "
                        "cannot be established.", height);
                    return false;
                }
                auto const idx = persisted_chain_indices[at];
                if (header_index_.has_undo_data(idx)) continue;

                spdlog::critical("[blockchain] The block at height {} is connected but has no undo "
                    "data. Its record is missing or names a block this index does not hold, which "
                    "means it cannot be disconnected and no reorganization below this height can "
                    "run. The UTXO set and undo data have to be rebuilt.", height);
                return false;
            }
        }

        auto const scan_elapsed = std::chrono::steady_clock::now() - scan_start;
        auto const scan_ms = std::chrono::duration_cast<std::chrono::milliseconds>(scan_elapsed).count();
        spdlog::info("[blockchain] Scanned {} blocks, restored {} positions in {}ms",
            scanned, restored, scan_ms);
    }

#ifdef KTH_UTXOZ_REFERENCE_MODE
    // Wire block_store and header_index to UTXO-Z for reference mode find resolution
    // Scoped to the wiring it authorises, and for the same reason the open above
    // is: a window that outlived its callback once already met another one and
    // hung the node at startup, in reference mode only. Nothing follows this here
    // today — but the previous version of that sentence was also true.
    {
        auto const configure = utxo_gate_.write();
        if ( ! configure) {
            spdlog::error("[blockchain] The UTXO gate has latched; reference-mode wiring "
                "cannot be applied ({})", database::result_code_name(configure.error()));
            return false;
        }
        utxoz_db_.with_write(*configure, [&](auto& db) {
            db.set_block_store(block_store_.get());
            db.set_header_index(&header_index_);
        });
    }
    spdlog::info("[blockchain] UTXO-Z reference mode: block_store and header_index wired for find resolution");
#endif

    return true;
}

bool block_chain::stop() {
    stopped_ = true;

    validation_mutex_.lock_high_priority();
    auto result = transaction_organizer_.stop() && block_organizer_.stop();
    priority_pool_.stop();
    validation_mutex_.unlock_high_priority();

    return result;
}

bool block_chain::close() {
    auto const result = stop();
    // Persist the mempool now that admission is quiesced and before the DB dir
    // is torn down (the path is a plain sibling of the DB, still valid here).
    dump_mempool_to_disk();
    priority_pool_.join();
    {
        // close() needs the same exclusion any mutation does — a close running
        // alongside a reader unmaps what that reader is holding — but it must
        // work on a LATCHED gate too, which refuses every window. Hence the
        // administrative capability: it waits for exclusion exactly as a window
        // does and skips only the poison check, and it never clears the latch,
        // so a restart still finds the gate closed to everything else.
        auto closing = utxo_gate_.authorise_close();
        utxoz_db_.with_close(closing, [](auto& db) { db.close(); });
    }
    return result && database_.close();
}

bool block_chain::stopped() const {
    return stopped_;
}

// =============================================================================
// ORGANIZERS (Core blockchain operations)
// =============================================================================

::asio::awaitable<code> block_chain::organize(block_const_ptr block, bool headers_pre_validated) {
    // Single-block acceptance was implemented on the pre-v1 LMDB storage
    // (block_organizer -> populate_block over the LMDB utxo_pool, reorg pool).
    // That storage is dead in the v1 node (flat files + UTXO-Z; IBD validates via
    // validate_block_batch + utxo_build), so this path can no longer accept a
    // block and is stubbed while the dead LMDB code is removed. Reimplementing it
    // on the v1 model is tracked in issue #563.
    (void)block;
    (void)headers_pre_validated;
    co_return error::not_implemented;
}

::asio::awaitable<code> block_chain::organize_fast(std::shared_ptr<domain::chain::light_block const> block, size_t height) {
    // Fast IBD: merkle validation + (later) store block data to flat files.
    //
    // Threading model:
    // - Work is posted to priority_pool_ (appears parallel)
    // - But caller does co_await before proceeding (sequential from caller's perspective)
    // - Since there's only ONE caller (block_validation_task), operations are effectively sequential
    // - Therefore block_store_ and header_index_ don't need mutex protection for these operations
    //
    // Why post to pool instead of running inline?
    // - Avoids blocking the main executor while doing I/O
    // - Allows the coroutine to suspend without consuming a system thread
    //
    using result_channel = ::asio::experimental::concurrent_channel<void(std::error_code, code)>;
    auto channel = std::make_shared<result_channel>(priority_pool_.get_executor(), 1);

    ::asio::post(priority_pool_.get_executor(), [this, block, height, channel]() {
        // Validate merkle root (ensures transactions match header)
        // This is the only validation needed since header was already validated
        if ( ! block->is_valid_merkle_root()) {
            spdlog::error("[blockchain] Merkle mismatch at height {}", height);
            channel->try_send(std::error_code{}, error::merkle_mismatch);
            return;
        }

        // Stage 3 (flat-file block storage on top of header_index) is not
        // wired in yet; blocks live in LMDB via the database layer. Code
        // path for save_block + header_index.set_block_pos belongs in a
        // separate feature PR once flat_file_seq is ready.

        channel->try_send(std::error_code{}, error::success);
    });

    auto [ec, result] = co_await channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
    if (ec) {
        co_return error::operation_failed;
    }
    co_return result;
}

::asio::awaitable<code> block_chain::store_chunk(
    std::vector<std::shared_ptr<domain::chain::light_block const>> const& blocks,
    uint32_t start_height
) {
    auto const n = blocks.size();
    if (n == 0) co_return error::success;

    using result_channel = ::asio::experimental::concurrent_channel<void(std::error_code, code)>;

    // Phase 1: Allocate positions for all N blocks (single post, serial, fast)
    // allocate_block_space() must be called serially — guaranteed because
    // block_storage_task is a single coroutine doing co_await store_chunk() one at a time.
    auto positions = std::make_shared<std::vector<database::flat_file_pos>>(n);
    auto alloc_ch = std::make_shared<result_channel>(priority_pool_.get_executor(), 1);

    ::asio::post(priority_pool_.get_executor(), [this, &blocks, start_height, n, positions, alloc_ch]() {
        for (size_t i = 0; i < n; ++i) {
            auto const height = start_height + static_cast<uint32_t>(i);
            auto const& block = blocks[i];
            auto const raw_size = static_cast<uint32_t>(block->raw_data().size());

            auto pos = block_store_->allocate_block_space(raw_size, height, block->header().timestamp());
            if (pos.is_null()) {
                spdlog::error("[blockchain] Failed to allocate space for block {}", height);
                alloc_ch->try_send(std::error_code{}, error::operation_failed);
                return;
            }
            (*positions)[i] = pos;
        }
        alloc_ch->try_send(std::error_code{}, error::success);
    });

    {
        auto [ec, result] = co_await alloc_ch->async_receive(::asio::as_tuple(::asio::use_awaitable));
        if (ec || result) co_return result ? result : error::operation_failed;
    }

    // Phase 2: Write all N blocks in parallel (N posts, each opens its own FILE*)
    auto write_ch = std::make_shared<result_channel>(priority_pool_.get_executor(), n);

    for (size_t i = 0; i < n; ++i) {
        ::asio::post(priority_pool_.get_executor(), [this, &blocks, i, positions, write_ch]() {
            auto data_pos = block_store_->write_block_at(blocks[i]->raw_data(), (*positions)[i]);
            if (data_pos.is_null()) {
                write_ch->try_send(std::error_code{}, error::operation_failed);
                return;
            }
            // Store the data position (after header) for header_index
            (*positions)[i] = data_pos;
            write_ch->try_send(std::error_code{}, error::success);
        });
    }

    // Wait for all writes to complete
    code first_error;
    for (size_t i = 0; i < n; ++i) {
        auto [ec, result] = co_await write_ch->async_receive(::asio::as_tuple(::asio::use_awaitable));
        if (ec && !first_error) first_error = error::operation_failed;
        if (result && !first_error) first_error = result;
    }
    if (first_error) co_return first_error;

    // Phase 3: Update header_index with data positions (no I/O, fast)
    // Each block writes to its own index entry — safe without locks.
    for (size_t i = 0; i < n; ++i) {
        auto const& block = blocks[i];
        auto const block_hash = domain::chain::hash(block->header());
        auto const idx = header_index_.find(block_hash);
        if (idx != header_index::null_index) {
            auto const& data_pos = (*positions)[i];
            header_index_.set_block_pos(idx, static_cast<int16_t>(data_pos.file), data_pos.pos);
            header_index_.add_status(idx, header_status::have_data);
        }
    }

    co_return error::success;
}

// Toggle between validation strategies:
//   true  = 1 task per chunk (serial merkle on single pool thread)
//   false = 1 task per block (parallel merkle across pool threads)
static constexpr bool chunk_serial_validation = true;

::asio::awaitable<code> block_chain::validate_chunk(
    std::vector<std::shared_ptr<domain::chain::light_block const>> const& blocks,
    uint32_t start_height
) {
    auto const n = blocks.size();
    if (n == 0) co_return error::success;

    using result_channel = ::asio::experimental::concurrent_channel<void(std::error_code, code)>;
    auto channel = std::make_shared<result_channel>(priority_pool_.get_executor(), 1);

    auto const chunk_start_time = std::chrono::steady_clock::now();

    if constexpr (chunk_serial_validation) {
        // =====================================================================
        // Variant A: 1 task per chunk — serial merkle on a single pool thread
        // =====================================================================
        ::asio::post(priority_pool_.get_executor(), [&blocks, start_height, n, channel]() {
            auto const tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
            auto const t0 = std::chrono::steady_clock::now();

            code first_error;
            for (size_t i = 0; i < n; ++i) {
                if ( ! blocks[i]->is_valid_merkle_root()) {
                    spdlog::error("[validate_chunk:serial] Merkle MISMATCH height {} thread {}",
                        start_height + i, tid);
                    if ( ! first_error) {
                        first_error = error::merkle_mismatch;
                    }
                }
            }

            auto const elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - t0).count();
            spdlog::debug("[validate_chunk:serial] {} blocks at {} on thread {} in {}us ({:.1f}us/blk)",
                n, start_height, tid, elapsed_us, static_cast<double>(elapsed_us) / n);

            channel->try_send(std::error_code{}, first_error);
        });

        auto [ec, result] = co_await channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
        if (ec) {
            co_return error::operation_failed;
        }

        auto const chunk_elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - chunk_start_time).count();
        spdlog::debug("[validate_chunk:serial] chunk at {} wall time {}us", start_height, chunk_elapsed_us);

        co_return result;
    } else {
        // =====================================================================
        // Variant B: 1 task per block — parallel merkle across pool threads
        // =====================================================================
        auto parallel_channel = std::make_shared<result_channel>(priority_pool_.get_executor(), n);

        for (size_t i = 0; i < n; ++i) {
            ::asio::post(priority_pool_.get_executor(), [block = blocks[i], height = start_height + i, parallel_channel]() {
                auto const t0 = std::chrono::steady_clock::now();
                auto const tid = std::hash<std::thread::id>{}(std::this_thread::get_id());

                bool valid = block->is_valid_merkle_root();

                auto const elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - t0).count();

                if ( ! valid) {
                    spdlog::error("[validate_chunk:parallel] Merkle MISMATCH height {} thread {} ({}us)", height, tid, elapsed_us);
                    parallel_channel->try_send(std::error_code{}, error::merkle_mismatch);
                    return;
                }
                spdlog::trace("[validate_chunk:parallel] height {} thread {} ({}us)", height, tid, elapsed_us);
                parallel_channel->try_send(std::error_code{}, error::success);
            });
        }

        code final_result;
        for (size_t i = 0; i < n; ++i) {
            auto [ec, result] = co_await parallel_channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
            if (ec) {
                co_return error::operation_failed;
            }
            if (result && !final_result) {
                final_result = result;
            }
        }

        auto const chunk_elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - chunk_start_time).count();
        spdlog::debug("[validate_chunk:parallel] chunk at {} ({} blocks) wall time {}us ({:.1f}us/blk)",
            start_height, n, chunk_elapsed_us, static_cast<double>(chunk_elapsed_us) / n);

        co_return final_result;
    }
}

::asio::awaitable<code> block_chain::store_block(
    std::shared_ptr<domain::chain::light_block const> const& block,
    uint32_t height
) {
    // Same post+await pattern as validate_chunk() and organize_fast():
    // Post disk I/O to priority_pool_ so the network executor stays free.
    using result_channel = ::asio::experimental::concurrent_channel<void(std::error_code, code)>;
    auto channel = std::make_shared<result_channel>(priority_pool_.get_executor(), 1);

    ::asio::post(priority_pool_.get_executor(), [this, block, height, channel]() {
        // 1. Save block to flat files (sequential I/O)
        auto pos = block_store_->save_block_raw(block->raw_data(), height, block->header().timestamp());
        if (pos.is_null()) {
            spdlog::error("[blockchain] Failed to save block {} to flat files", height);
            channel->try_send(std::error_code{}, error::operation_failed);
            return;
        }

        // 2. Update header_index with block file position
        auto const block_hash = domain::chain::hash(block->header());
        auto const idx = header_index_.find(block_hash);
        if (idx != header_index::null_index) {
            header_index_.set_block_pos(idx, static_cast<int16_t>(pos.file), pos.pos);
            header_index_.add_status(idx, header_status::have_data);
        }

        // The connected-tip marker is deliberately NOT written here (#653).
        // Storing is not connecting: the bytes are in a stdio buffer, this index
        // entry is in memory, and no barrier has run. Only the batch that applied
        // the UTXO delta may move that height, and it does so with the barrier
        // and the transition record.
        //
        // This function currently has no callers — it is the single-block
        // counterpart of store_chunk. Left in place rather than removed here, but
        // it must not carry a rule the rest of the code no longer follows.

        channel->try_send(std::error_code{}, error::success);
    });

    auto [ec, result] = co_await channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
    if (ec) {
        co_return error::operation_failed;
    }
    co_return result;
}

::asio::awaitable<code> block_chain::organize(transaction_const_ptr tx) {
    co_return co_await transaction_organizer_.organize(tx);
}

::asio::awaitable<code> block_chain::organize(double_spend_proof_const_ptr ds_proof) {
    co_return co_await transaction_organizer_.organize(ds_proof);
}

::asio::awaitable<code> block_chain::organize_header(header_const_ptr header) {
    if (stopped()) {
        co_return error::service_stopped;
    }

    // Get current header height
    auto const heights = get_last_heights();
    if ( ! heights) {
        co_return error::operation_failed;
    }

    auto const next_height = heights->header + 1;

    // Store header in database with ABLA state = 0
    // The correct ABLA state will be set when the full block arrives via push_block
    auto const ec = database_.push_header(*header, next_height);
    if (ec) {
        co_return ec;
    }

    co_return error::success;
}

code block_chain::organize_headers_batch(domain::chain::header::list const& headers, size_t start_height) {
    if (stopped()) {
        return error::service_stopped;
    }

    if (headers.empty()) {
        return error::success;
    }

    return database_.push_headers_batch(headers, start_height);
}

database::transition_check block_chain::read_transition_record() const {
    return database_.internal_db().read_transition_record();
}

#if ! defined(KTH_DB_READONLY)

code block_chain::replace_headers_from(domain::chain::header::list const& headers, size_t start_height) {
    if (stopped()) {
        return error::service_stopped;
    }

    return database_.replace_headers_from(headers, start_height);
}

::asio::awaitable<code> block_chain::push(transaction_const_ptr tx) {
    using result_channel = ::asio::experimental::concurrent_channel<void(std::error_code, code)>;
    auto channel = std::make_shared<result_channel>(priority_pool_.get_executor(), 1);

    ::asio::post(priority_pool_.get_executor(), [this, tx, channel]() {
        auto const result = push_sync(tx);
        channel->try_send(std::error_code{}, result);
    });

    auto [ec, result] = co_await channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
    if (ec) {
        co_return error::operation_failed;
    }
    co_return result;
}

code block_chain::push_sync(transaction_const_ptr /*tx*/) {
    // TODO(mempool): the tx sink used to write to the LMDB unconfirmed-tx
    // store, which has been removed. Reimplement against the new mempool.
    // See issue #491.
    mempool_not_implemented("block_chain::push_sync");
}

bool block_chain::insert(block_const_ptr block, size_t height) {
    // DEPRECATED: Block storage moved to flat files (blk*.dat)
    (void)block;
    (void)height;
    return false;
}

std::expected<uint32_t, database::result_code> block_chain::get_utxo_built_height() const {
    return database_.internal_db().get_utxo_built_height();
}

database::result_code block_chain::set_utxo_built_height(uint32_t height) {
    return database_.internal_db().set_utxo_built_height(height);
}

database::result_code block_chain::begin_transition_record(
    database::utxo_transition_record const& record) {
    return database_.internal_db().begin_transition_record(record);
}

database::result_code block_chain::set_heights(
    database::transition_heights const& heights) {
    return database_.internal_db().set_heights(heights);
}

database::result_code block_chain::publish_transition(
    database::transition_heights const& heights) {
    return database_.internal_db().publish_transition(heights);
}

database::result_code block_chain::env_sync() {
    return database_.internal_db().env_sync();
}

std::expected<void, database::block_store::undo_flush_error>
block_chain::flush_undo(std::span<int32_t const> file_numbers) {
    if ( ! block_store_) {
        // Asking for a barrier over a store that is not there is not an empty
        // set of files; -1 is the store itself rather than any one file.
        return std::unexpected(database::block_store::undo_flush_error{
            database::result_code::other, -1});
    }
    return block_store_->flush_undo(file_numbers);
}

database::barrier_outcome block_chain::utxo_sync(utxo_write_window const& window) {
    return utxoz_db_.with_write(window, [](auto& db) { return db.sync(); });
}

database::durability_level block_chain::durability() const {
    return database::node_durability_level();
}

// The reconciliation decision, pure so that every input is reachable from a
// test — including the operational failure, which no fixture setup can force out
// of LMDB on demand.
//
// Four inputs, and they are genuinely different states:
//
//   * a value reconciles: the UTXO height wins over the marker, in both
//     directions, because it is what describes the set;
//   * `key_not_found` WITH AN EMPTY UTXO STORE is a database that has never
//     built — a fresh datadir, or one that only ever downloaded — so nothing is
//     connected and the answer is zero, whatever the marker claims;
//   * `key_not_found` with a NON-EMPTY store is a materialised UTXO set whose
//     height nothing records. The property has existed since v1.0, but a base
//     from any release can reach this if it was written before the marker was,
//     and the set itself cannot be asked how far it goes: entries carry creation
//     heights, and the highest surviving one is a lower bound, not the built
//     height, because spent outputs are erased. Answering zero would rebuild
//     from genesis over a populated store — re-sending inserts that are already
//     there. So it is refused;
//   * any other code is a read that FAILED, which is not an absent marker.
//
// The last two answer nullopt and the caller fails closed, with different
// diagnostics because they call for different repairs.
std::optional<uint32_t> reconcile_tip(
    uint32_t marker_height,
    std::expected<uint32_t, database::result_code> const& built,
    bool utxo_set_is_empty) {
    if (built) {
        return *built;
    }
    if (built.error() != database::result_code::key_not_found) {
        return std::nullopt;
    }
    if (utxo_set_is_empty) {
        return 0u;
    }
    return std::nullopt;
}

std::optional<uint32_t> block_chain::reconcile_connected_tip(uint32_t marker_height) {
    // What the two markers mean, and why the UTXO one wins in BOTH directions.
    //
    // `utxo_built_height` is written only by the batch that applied the delta,
    // after its barrier, in the transaction that clears the transition record. It
    // therefore describes the UTXO set itself. `last_block_height` is the
    // connected tip its readers assume — but a released version also let the
    // storage task write the DOWNLOADED height into it (#653), so it can be wrong
    // in either direction and cannot be trusted on its own.
    //
    // min() would be wrong. Before this fix the storage marker was written only
    // on a clean stop, so a node that crashed mid-sync has last_block_height at 0
    // with a UTXO set describing hundreds of thousands of blocks: taking the
    // lower would throw away everything that IS connected. The set is the
    // evidence, so wherever the two disagree, the UTXO height wins.
    auto const built_marker = get_utxo_built_height();
    auto const count = utxo_count();
    if ( ! count) {
        // The enclosing function answers with an optional height. A store that
        // will not answer is not a height, and it is not "no marker" either —
        // nullopt is the honest one: nothing was established.
        spdlog::error("[blockchain] The UTXO store will not answer its size ({}); the connected "
            "tip cannot be reconciled", database::result_code_name(count.error()));
        return std::nullopt;
    }
    auto const utxo_empty = *count == 0;
    auto const decided = reconcile_tip(marker_height, built_marker, utxo_empty);
    if ( ! decided) {
        if ( ! built_marker && built_marker.error() == database::result_code::key_not_found) {
            spdlog::error("[blockchain] The UTXO set holds entries but no height marker "
                "records how far it was built; that height cannot be recovered from the "
                "set itself, so the node refuses to start rather than rebuild from "
                "genesis over a populated store. Rebuild the UTXO set for this datadir.");
        }
        return std::nullopt;   // the caller fails closed
    }
    auto const built = *decided;

    if (built == marker_height) {
        return marker_height;
    }

    // The height has to be one the node can stand on. publish_chain_view reads
    // the header and the block at the tip it is given, so a marker naming a
    // height with no header behind it is not a tip but a claim — and starting on
    // it would build chain state out of nothing. Refused rather than repaired:
    // this is a database that disagrees with itself in a way no rule here can
    // settle.
    if ( ! get_header(built)) {
        spdlog::error("[blockchain] The UTXO set claims height {} but no header is "
            "stored there; refusing to start on a tip that cannot be read", built);
        return std::nullopt;
    }

    spdlog::warn("[blockchain] The connected-tip marker says {} and the UTXO set "
        "describes {}; taking the UTXO height, which is what the set actually "
        "holds, and correcting the marker", marker_height, built);

    // Corrected DURABLY, and before any state is published from it: a restart
    // must read the reconciled value directly rather than repeat this every time,
    // and a reader that arrives between the two would otherwise still see the
    // stale claim.
    if (auto const written = set_last_block_height(built);
        written != database::result_code::success) {
        spdlog::error("[blockchain] Could not persist the reconciled connected tip {}",
            built);
        return std::nullopt;
    }
    return built;
}

database::result_code block_chain::set_last_block_height(uint32_t height) {
    return database_.internal_db().set_last_block_height(height);
}

utxoz::deletion_progress block_chain::utxo_apply_deletes(
    utxo_write_window const& window, std::span<utxoz::deferred_deletion_entry const> requests) {
    return utxoz_db_.with_write(window,
        [&](auto& db) { return db.apply_deletes(requests); });
}

// A store that reports it has latched closes the gate behind it.
//
// Translating the code and nothing else would leave every later caller queueing
// for a store that has already stopped answering — the gate would keep admitting
// them one refused call at a time. Applied wherever a boundary can observe it,
// the read paths included: a lease cannot latch on release, because a read
// leaves nothing half-applied, so the fact is published where it is seen.
template <typename T>
std::expected<T, database::result_code> latch_if_store_reports_recovery(
    std::expected<T, database::result_code> result, utxo_gate& gate) {
    if ( ! result && database::needs_recovery(result.error())) {
        gate.latch_observed();
    }
    return result;
}

std::expected<database::utxoz_database::entry_resolution, database::result_code>
block_chain::utxo_resolve(std::span<utxoz::lookup_request const> requests) const {
    auto lease = utxo_gate_.read();
    if ( ! lease) {
        return std::unexpected(lease.error());
    }
    return latch_if_store_reports_recovery(
        utxoz_db_.with_read(*lease, [&](auto const& db) { return db.resolve(requests); }),
        utxo_gate_);
}

// =============================================================================
// REORG UNDO
// =============================================================================

std::expected<database::utxoz_database::raw_stored, database::result_code>
block_chain::find_utxo_raw(utxoz::raw_outpoint const& key, uint32_t height) const {
    auto lease = utxo_gate_.read();
    if ( ! lease) {
        return std::unexpected(lease.error());
    }
    return latch_if_store_reports_recovery(
        utxoz_db_.with_read(*lease, [&](auto const& db) { return db.find_raw(key, height); }),
        utxo_gate_);
}

std::expected<database::utxoz_database::raw_resolution, database::result_code>
block_chain::utxo_resolve_raw(std::span<utxoz::lookup_request const> requests) const {
    auto lease = utxo_gate_.read();
    if ( ! lease) {
        return std::unexpected(lease.error());
    }
    return latch_if_store_reports_recovery(
        utxoz_db_.with_read(*lease, [&](auto const& db) { return db.resolve_raw(requests); }),
        utxo_gate_);
}

#if ! defined(KTH_DB_READONLY)
std::optional<int32_t> block_chain::store_block_undo(database::header_index::index_t idx,
                                                    database::block_undo const& undo,
                                                    hash_digest const& prev_hash) {
    // The undo record lives in the rev*.dat file matching the block's blk*.dat,
    // and the header index stores only the offset — so the block must be on disk.
    auto const file_number = header_index_.get_file_number(idx);
    if (file_number < 0) {
        spdlog::error("[blockchain] store_block_undo: no block data for index {}", idx);
        return std::nullopt;
    }

    // The record carries the owning block's hash so a restart can attribute it.
    // Nothing else can: the checksum is seeded with the parent hash, so every
    // sibling validates the same record (#603).
    auto const pos = block_store_->write_undo(undo, file_number,
        header_index_.get_hash(idx), prev_hash);
    if (pos.is_null()) {
        spdlog::error("[blockchain] store_block_undo: failed to write undo for index {}", idx);
        return std::nullopt;
    }

    header_index_.set_undo_pos(idx, pos.pos);
    header_index_.add_status(idx, database::header_status::have_undo);

    // Which rev file the record landed in, so the caller can put exactly the
    // files this transition wrote on stable storage. Derived here rather than
    // recomputed at the call site: the same lookup twice is the same lookup
    // twice only until one of them changes.
    return file_number;
}
#endif

std::expected<database::block_undo, database::result_code>
block_chain::read_block_undo(database::header_index::index_t idx, hash_digest const& prev_hash) const {
    if ( ! header_index_.has_undo_data(idx)) {
        return std::unexpected(database::result_code::key_not_found);
    }

    database::flat_file_pos const pos{
        header_index_.get_file_number(idx),
        header_index_.get_undo_pos(idx)
    };
    return block_store_->read_undo(pos, header_index_.get_hash(idx), prev_hash);
}

#if ! defined(KTH_DB_READONLY)
database::disconnect_result block_chain::disconnect_block(uint32_t height,
    utxo_write_window const& window,
    boost::unordered_flat_map<utxoz::raw_outpoint, bool>& absence_tolerated,
    std::vector<utxoz::deferred_deletion_entry>& pending_deletes) {
    // Genesis anchors the chain and has no parent to roll back to.
    if (height == 0) {
        spdlog::error("[blockchain] disconnect_block: refusing to disconnect genesis");
        return database::disconnect_result::failed;
    }

    // Blocks are disconnected newest-first, so `height` must be the validated tip.
    // Disconnecting out of order would restore outputs that a later block still
    // spends, silently corrupting the UTXO set. Fail closed: if the tip cannot be
    // read, we cannot prove this is the tip, so we must not proceed.
    auto const heights = database_.internal_db().get_last_heights();
    if ( ! heights) {
        spdlog::error("[blockchain] disconnect_block: cannot read the validated tip");
        return database::disconnect_result::failed;
    }
    if (height != heights->block) {
        spdlog::error("[blockchain] disconnect_block: height {} is not the validated tip ({})",
            height, heights->block);
        return database::disconnect_result::failed;
    }

    auto const idx = header_index_.active_at(static_cast<int32_t>(height));
    if (idx == database::header_index::null_index) {
        spdlog::error("[blockchain] disconnect_block: height {} is not on the active chain", height);
        return database::disconnect_result::failed;
    }

    if ( ! header_index_.has_block_data(idx)) {
        spdlog::error("[blockchain] disconnect_block: no block data at height {}", height);
        return database::disconnect_result::failed;
    }

    // The outputs the block created are not stored in the undo record: they are
    // recomputed here by re-reading the block, which is why undo data only needs
    // to carry what was spent.
    database::flat_file_pos const pos{
        header_index_.get_file_number(idx),
        header_index_.get_data_pos(idx)
    };
    auto raw = block_store_->read_block_raw(pos);
    if ( ! raw) {
        spdlog::error("[blockchain] disconnect_block: cannot read block at height {}", height);
        return database::disconnect_result::failed;
    }

    auto parsed = parse_utxo_block(byte_span{raw->data(), raw->size()});
    if ( ! parsed) {
        spdlog::error("[blockchain] disconnect_block: cannot parse block at height {}", height);
        return database::disconnect_result::failed;
    }

    auto undo = read_block_undo(idx, header_index_.get_prev_block_hash(idx));
    if ( ! undo) {
        spdlog::error("[blockchain] disconnect_block: no undo data at height {} (block below the "
            "checkpoint, or connected before undo data was recorded)", height);
        return database::disconnect_result::failed;
    }

    // The inverse of a delta is another delta: restore what was spent, remove
    // what was created.
    utxo_raw_delta inverse;
    inverse.inserts.reserve(undo->spent.size());
    inverse.deletes.reserve(parsed->outputs.size());

    for (auto const& entry : undo->spent) {
        inverse.inserts.emplace(entry.key, utxo_raw_value{entry.value, entry.height});
    }
    for (auto const& out : parsed->outputs) {
        // A key this block created that the undo ALSO carries a previous value
        // for is a BIP30 replacement: the block overwrote a live entry, and the
        // undo kept what it overwrote. Restoring it and then deleting it would
        // leave nothing -- the restore above runs first and the deletions are
        // applied at the end of the rewind -- so the key is simply not deleted.
        //
        // Nothing else produces that overlap. capture_block_undo records the
        // previous value of what a block SPENDS, and an output created and spent
        // inside one block is netted out of the delta before it can be recorded:
        // process_compact_block_utxos inserts every output of the block before it
        // examines any input, so the pairing does not depend on the order the
        // transactions happen to be in. The overlap therefore needs no marker in
        // the undo format to be read unambiguously.
        if (inverse.inserts.contains(out.key)) {
            continue;
        }
        inverse.deletes.emplace(out.key, height);
    }

    // Which of those deletes are allowed to come back unapplied, decided HERE,
    // from the two exact key sets this block's inverse delta was built from.
    //
    // An output created AND spent inside this same block never entered the set:
    // the connect-side delta netted it out. So erasing it now is a no-op, it is
    // absent from the undo record by construction, and UTXO-Z — finding it in no
    // mapped version — will defer the deletion and then report it as failed.
    // That report is indistinguishable from the report it gives for a version
    // file it could not read, which is why the set of keys entitled to it has to
    // be known independently of the store.
    //
    // PER BLOCK, and that is the whole subtlety. An output created at this
    // height and spent by a LATER block of the same abandoned branch is not in
    // this block's inputs, so it does not land here — correctly, because the
    // later block was disconnected first and its own inverse delta restored the
    // output. It is in the set when this erase runs, and the erase must succeed.
    // Intersecting the branch's outputs with the branch's inputs as a whole
    // would sweep that key in and license exactly the failure this exists to
    // catch.
    //
    // Both sides are the delta's own keys — the outpoints being erased and the
    // outpoints this block spends — not a per-transaction argument about which
    // output some transaction consumed. An outpoint names one output, so the
    // intersection is the answer rather than an approximation of it.
    // This block's own view: an outpoint it erases that it ALSO spends was
    // created and spent inside itself, so it never entered the set.
    boost::unordered_flat_set<utxoz::raw_outpoint> spent_here;
    spent_here.reserve(parsed->inputs.size());
    for (auto const& in : parsed->inputs) {
        if (inverse.deletes.contains(in.prev_key)) {
            spent_here.insert(in.prev_key);
        }
    }

    // Folded into the rewind's obligation CONSERVATIVELY, per key. The batch is
    // applied once at the end and UTXO-Z deduplicates by key keeping the first
    // occurrence, so one key can be contributed by several blocks at several
    // heights and will come back as a single answer. A union of "tolerable"
    // would let one block's no-op excuse another block's real deletion.
    //
    // So the obligations AND together: a key is tolerated absent only if EVERY
    // appearance is structurally tolerable, and a single appearance that
    // requires the key to exist and be deleted dominates every other.
    for (auto const& [key, at_height] : inverse.deletes) {
        fold_absence_tolerance(absence_tolerated, key, spent_here.contains(key));
    }

    auto const result = utxoz_db_.with_write(window,
        [&](auto& db) { return db.apply_inserts_raw(inverse.inserts); });
    if (result != database::result_code::success) {
        spdlog::error("[blockchain] disconnect_block: failed to restore spent outputs at height {}",
            height);
        return database::disconnect_result::unclean;
    }

    // The deletions are NOT applied here. They are appended to a batch this
    // rewind owns and carries to the end, because apply_deletes() is one
    // exclusive, descending walk whose cost is paid per batch rather than per
    // key — and because the whole rewind is a single transition that publishes
    // nothing until it completes, so the set holding them transiently is the
    // same window the transition record already covers.
    //
    // Order still holds within the batch: an output created at this height and
    // spent by a LATER block of the branch was restored by that block's
    // disconnect, which ran first, so it is present when the batch is applied.
    for (auto const& [key, at_height] : inverse.deletes) {
        pending_deletes.emplace_back(key, at_height);
    }

    // Roll the markers back to the parent block. The UTXO set is already reverted
    // at this point, so a failed marker write leaves the two disagreeing — report
    // it rather than claiming a clean disconnect.
    //
    // ONE transaction for both. They used to be two, which left an instant where
    // the stored-block height named one block and the built height named
    // another: a start landing there reads a chain whose two markers describe
    // different blocks, and neither of them is wrong on its own.
    auto const parent = height - 1;
    if (auto const markers = database_.internal_db().set_heights(
            database::transition_heights{
                .last_block_height = parent,
                .utxo_built_height = parent});
        markers != database::result_code::success) {
        spdlog::error("[blockchain] disconnect_block: UTXO set rolled back to {} but the height "
            "markers could not be updated", parent);
        return database::disconnect_result::unclean;
    }

    spdlog::info("[blockchain] Disconnected block at height {} ({} outputs removed, {} restored)",
        height, parsed->outputs.size(), undo->spent.size());

    return database::disconnect_result::ok;
}
#endif

#if ! defined(KTH_DB_READONLY)
block_chain::switch_result block_chain::switch_to_branch(
    database::header_index::index_t branch_head, uint32_t fork_height) {

    auto const heights = database_.internal_db().get_last_heights();
    if ( ! heights) {
        spdlog::error("[blockchain] switch_to_branch: cannot read the validated tip");
        return {};
    }

    // Validate the target before touching anything: re-pointing the active chain
    // at a bad index after the UTXO set is already rolled back would leave the
    // node with no coherent chain at all.
    if (branch_head == database::header_index::null_index) {
        spdlog::error("[blockchain] switch_to_branch: null branch head");
        return {false, heights->block};
    }
    if (header_index_.get_height(branch_head) <= int32_t(fork_height)) {
        spdlog::error("[blockchain] switch_to_branch: branch head at height {} is not above the "
            "fork at {}", header_index_.get_height(branch_head), fork_height);
        return {false, heights->block};
    }

    // Do not take the caller's word for where the branch forks: derive it. A
    // fork_height lower than the real one would disconnect fewer blocks than the
    // switch needs, leaving UTXO effects of abandoned blocks active while the
    // active tip moves onto the branch anyway.
    auto const active_tip = header_index_.active_at(header_index_.active_tip_height());
    auto const fork_idx = header_index_.find_fork(branch_head, active_tip);
    if (fork_idx == database::header_index::null_index) {
        spdlog::error("[blockchain] switch_to_branch: branch shares no ancestor with the active chain");
        return {false, heights->block};
    }

    auto const real_fork = header_index_.get_height(fork_idx);
    if (real_fork != int32_t(fork_height)) {
        spdlog::error("[blockchain] switch_to_branch: fork height mismatch — caller says {}, the "
            "branch actually forks at {}", fork_height, real_fork);
        return {false, heights->block};
    }

    // Where the validated tip ends up. A fork above the validated tip disconnects
    // nothing, so the tip does not move — reporting the fork height there would
    // strand the still-missing blocks below it, never requested again.
    uint32_t validated_tip = heights->block;

    // Whether any block was actually disconnected is no longer tracked here. The
    // window carries it — `mark_mutated()` on evidence, `has_mutated()` to read
    // it back — so there is one fact and not two that can drift apart. It is
    // deliberately NOT the same fact as the one governing poison: this one says
    // something WAS applied and drives republishing the chain view, while the
    // conservative one says something MIGHT have been and drives the latch.
    // Merging them would republish on every rejected switch, moving the
    // generation and dropping the template cache for a switch that touched
    // nothing.

    // Correlates the log lines this switch writes with the record a failed run
    // leaves behind. `record_written` and not `operation_id != 0`: the id comes
    // from system entropy, and zero is a value it may legitimately draw.
    uint64_t operation_id = 0;
    bool record_written = false;

    // Accumulated across every block this rewind disconnects, and consumed once
    // by the sweep below. Per-block by construction: each disconnect adds only
    // the keys ITS own inverse delta created and spent within itself.
    // ONE window for the whole rewind: the restorations, every disconnect, and
    // the final sweep. A reader admitted between them would see a set holding
    // outputs of blocks this switch is abandoning, and it would not consult the
    // transition record to know better. Synchronous throughout — nothing below
    // suspends — so the capability never crosses a co_await (#649).
    auto window = utxo_gate_.write();
    if ( ! window) {
        spdlog::error("[blockchain] Reorg: the UTXO gate has latched; the switch cannot run ({})",
            database::result_code_name(window.error()));
        return {false, std::nullopt, /*mutated*/ false, /*fatal*/ true};
    }

    // Per key, whether a proven absence is tolerable: true only while every
    // block that asked for this key's deletion created and spent it itself.
    boost::unordered_flat_map<utxoz::raw_outpoint, bool> absence_tolerated;

    // The rewind's deletion obligation, owned here and handed to apply_deletes()
    // as a span. It lives exactly as long as this switch: #602 retries within
    // the operation and never across a restart — an interrupted transition is
    // refused at the next start, not resumed.
    std::vector<utxoz::deferred_deletion_entry> pending_deletes;

    if (fork_height > heights->block) {
        spdlog::info("[blockchain] Reorg: fork at {} is above the validated tip {}, nothing to disconnect",
            fork_height, heights->block);
    } else {
        validated_tip = fork_height;

        auto const to_disconnect = heights->block - fork_height;
        spdlog::warn("[blockchain] Reorg: disconnecting {} block(s), {} down to {}",
            to_disconnect, heights->block, fork_height + 1);

        // Step 2. The same record the connect batch writes, with the same
        // meaning: this transition is about to mutate the stores and has not
        // been recorded as finished. A reorganization rewrites the UTXO state of
        // every height above the fork, so that is the range it names — the
        // lowest it touches and the tip it starts from.
        //
        // Written here and not above: the rejections before this point return
        // without touching anything, and a record over a transition that never
        // began would refuse the next start for nothing.
        operation_id = database::make_operation_id();
        if (auto const recorded = database_.internal_db().begin_transition_record(
                database::utxo_transition_record{
                    .format_version = database::utxo_transition_record::current_format_version,
                    .type = database::transition_type::reorg,
                    .operation_id = operation_id,
                    .first_height = fork_height + 1,
                    .intended_last_height = heights->block,
                    .state = database::transition_state::in_progress});
            recorded != database::result_code::success) {
            spdlog::critical("[blockchain] Reorg: could not record that the switch over {}-{} is "
                "in flight; refusing to rewind the UTXO set without it",
                fork_height + 1, heights->block);
            // Nothing was mutated, so the stores are as they were — but a
            // properties write that failed is a database this process must stop
            // writing to, not a switch to shrug off and retry.
            return {false, heights->block, /*mutated*/ false, /*fatal*/ true};
        }

        // Step 3. The environment is opened MDB_NOSYNC, so the record above has
        // not reached the disk. Without this it describes a window it does not
        // cover.
        if (auto const synced = database_.internal_db().env_sync();
            synced != database::result_code::success) {
            spdlog::critical("[blockchain] Reorg: the transition record for {}-{} could not be "
                "put on stable storage; refusing to rewind the UTXO set behind a record a "
                "restart may not find", fork_height + 1, heights->block);
            // The record is left where it is. Whether it reached the disk is
            // exactly what just failed to be established, and clearing it would
            // be a second write through the same barrier — asserting "clean" on
            // the strength of the mechanism that has already refused to answer.
            // Fail closed: this run stops and the next start decides.
            return {false, heights->block, /*mutated*/ false, /*fatal*/ true};
        }
        record_written = true;

        // Newest first: disconnecting out of order would restore outputs that a
        // later block still spends.
        //
        // Declared BEFORE the first disconnect, not after it. A disconnect
        // writes as it goes, so from the moment the loop is entered the set may
        // hold part of an inverse delta — and if this stretch is left by an
        // exception rather than by a return, the destructor is the only thing
        // that runs. It says "something might have been applied", which is a
        // weaker claim than mark_mutated() below and a different one.
        window->mark_mutating();
        for (uint32_t h = heights->block; h > fork_height; --h) {
            auto const result = disconnect_block(h, *window, absence_tolerated, pending_deletes);

            if (result == database::disconnect_result::unclean) {
                // The delta was applied, so this counts as a mutation even
                // though the switch is abandoned. Evidence, so it is recorded on
                // the window rather than only in the returned struct.
                window->mark_mutated();
                // The inverse delta was applied but the markers could not be
                // written: the UTXO set is at h-1 while the markers still say h.
                // Reporting h as a resumable tip would re-download from a height
                // whose UTXO state is already inconsistent, and nothing would ever
                // repair it. Report "unknown" so the caller stops instead.
                // And the record stays. This is precisely the state it exists
                // for: the stores disagree with each other, nothing here can
                // reconcile them, and the next start must refuse rather than
                // build on a set that no longer describes itself.
                spdlog::error("[blockchain] Reorg: UTXO set and height markers diverged at {} "
                    "(set is at {}, markers say {}); aborting without a resumable tip. The "
                    "transition record for operation {:#018x} is left in place: the next start "
                    "will refuse and ask for a rebuild",
                    h, h - 1, h, operation_id);
                // Fatal, and said here rather than inferred downstream. The
                // record is deliberately left in place, which is the condition
                // `fatal` documents; reorg.cpp reaches the same conclusion from
                // an empty validated_tip, but that inference lives in one caller
                // and this result is public.
                return {false, std::nullopt, window->has_mutated(), /*fatal*/ true};
            }

            if (result != database::disconnect_result::ok) {
                // Clean failure: nothing was applied for this block, so the markers
                // and the set agree at h. Report it so the caller resyncs there
                // instead of assuming nothing moved (which would strand the rewound
                // range, never re-downloaded).
                //
                // The switch did not reach the fork, but it did reach a state
                // the stores agree on — which is what the record's clearing
                // asserts, and all it asserts. Publishing it here is what keeps
                // the next start from refusing over a database that is whole.
                spdlog::error("[blockchain] Reorg: failed to disconnect block at height {}, "
                    "aborting the switch (validated tip is now {})", h, h);
                if ( ! publish_reorg_transition(h, operation_id, *window, absence_tolerated, pending_deletes)) {
                    return {false, std::nullopt, window->has_mutated(), /*fatal*/ true};
                }
                return {false, h, window->has_mutated()};
            }

            // Applied. From here the chain has moved, whatever happens next.
            window->mark_mutated();
        }
    }

    // Steps 7 to 11, once the whole rewind has landed. Skipped when nothing was
    // disconnected: no record was written, so there is nothing to publish and
    // nothing to clear, and asking four stores for a barrier over no mutation
    // would only cost the fsyncs.
    if (record_written && ! publish_reorg_transition(validated_tip, operation_id, *window,
            absence_tolerated, pending_deletes)) {
        // The chain has moved and the transition could not be recorded as
        // finished. No tip is reported: the caller reads that as fatal, leaves
        // capture shut and winds the node down, and the record stays for the
        // next start to refuse on.
        return {false, std::nullopt, window->has_mutated(), /*fatal*/ true};
    }

    // The safe boundary for a rewind, and it is `utxo_sync` that defines it: the
    // transition record in LMDB and the UTXO-Z mutations are two stores that
    // have to agree on disk, and until the barrier returns they may not. Past
    // this point abandoning the window leaves nothing for a restart to
    // reconcile, so the gate is not latched.
    //
    // Only reached when a record was written; a switch that disconnected nothing
    // never marked the window, so it has nothing to complete.
    if (record_written) {
        window->complete();
    }

    // The active chain is NOT re-pointed here. Publishing the new tip is the
    // organizer's (see header_organizer::adopt_tip), because the header path
    // publishes there too: a batch that validated against the old tip decides
    // whether to publish under the organizer's lock, and a switch that published
    // the height mapping outside that lock could land between that decision and
    // its write — leaving the tip on one branch and the height mapping on the
    // other. The caller adopts the branch head as soon as this returns.
    spdlog::warn("[blockchain] Reorg: UTXO state rewound to the fork at {}; the branch head is "
        "published by the caller", fork_height);
    return {true, validated_tip, window->has_mutated()};
}

bool block_chain::sweep_reorg_deletions(uint64_t operation_id,
    utxo_write_window const& window,
    boost::unordered_flat_map<utxoz::raw_outpoint, bool> const& absence_tolerated,
    std::span<utxoz::deferred_deletion_entry const> pending_deletes) {
    // This function deliberately touches NOTHING that is shared with a reader.
    //
    // It used to prove absence through find() and process_pending_lookups(),
    // and that could not be made correct. UTXO-Z's pending-lookup set is global
    // and drained wholesale, so borrowing it means owning it for the whole
    // sequence — and one producer cannot be excluded: the c-api/JSON-RPC single
    // transaction validate path resolves prevouts lock-free, by its own comment
    // in transaction_organizer, and the reorg barrier parks only REGISTERED
    // writers, which it is not. Checking the queue was empty and then draining
    // it left a window in between: a concurrent validate could queue there, have
    // its keys consumed by this sweep (so it reads its own prevouts as absent,
    // which it reports as spent), and put a key into this sweep's results that
    // this sweep would read as a rewind that did not finish. Two ways to be
    // wrong, from one window.
    //
    // Adding a lock does not close it. The exclusion primitive has no shared
    // mode and is already held across suspension points, which is undefined
    // behaviour the synchronization document calls live rather than latent; the
    // validate path suspends three times. So the fix is not to coordinate the
    // producers of that queue but to stop being one of them. What replaces it is
    // the caller's own knowledge of the delta it applied — see disconnect_block,
    // where structurally_absent is derived from the exact keys of each inverse
    // delta. Nothing here reads the store, so nothing here can race with a
    // reader, whatever that reader does.
    if (pending_deletes.empty()) {
        return true;
    }

    spdlog::info("[blockchain] Reorg: applying {} deletion(s) owed by the rewind "
        "(operation {:#018x})", pending_deletes.size(), operation_id);

    // Bounded, and small. A retry only helps a fault that has already cleared;
    // repeating a walk against a version file that is still unreadable buys
    // nothing, and an unbounded loop would hold the switch open forever with the
    // transition record in place. Within THIS operation and this process: an
    // interrupted transition is refused at the next start, never resumed.
    constexpr int max_deletion_attempts = 3;

    utxoz::deferred_deletion_entry offender{utxoz::raw_outpoint{}, 0};

    auto const outcome = run_deletion_sweep(
        std::vector<utxoz::deferred_deletion_entry>(pending_deletes.begin(), pending_deletes.end()),
        absence_tolerated,
        [this, &window](std::span<utxoz::deferred_deletion_entry const> batch) {
            return utxo_apply_deletes(window, batch);
        },
        max_deletion_attempts,
        [&](int attempt, utxoz::deletion_progress const& progress) {
            if ( ! progress.unresolved.empty() || progress.error) {
                spdlog::warn("[blockchain] Reorg: attempt {} of {} applied {}, proved {} absent, "
                    "left {} owed (operation {:#018x}){}", attempt, max_deletion_attempts,
                    progress.erased.size(), progress.absent.size(), progress.unresolved.size(),
                    operation_id,
                    progress.error
                        ? fmt::format(", fault: {}", database::utxoz_error_name(*progress.error))
                        : "");
            }
        },
        &offender);

    switch (outcome) {
        case deletion_sweep_outcome::applied:
            spdlog::info("[blockchain] Reorg: every deletion the rewind owed is accounted for "
                "(operation {:#018x})", operation_id);
            return true;

        case deletion_sweep_outcome::absent_unaccounted:
            spdlog::critical("[blockchain] Reorg: {} (from block {}) is proven absent, and at "
                "least one block of this rewind required it to exist and be deleted "
                "(operation {:#018x}); it was in the UTXO set and is unaccounted for now",
                utxoz::outpoint_to_string(offender.key), offender.height, operation_id);
            return false;

        case deletion_sweep_outcome::fault_reported:
            spdlog::critical("[blockchain] Reorg: the deletion walk reported a fault with nothing "
                "left unresolved (operation {:#018x}); refusing to publish over a store that "
                "reported one", operation_id);
            return false;

        case deletion_sweep_outcome::attempts_exhausted:
            spdlog::critical("[blockchain] Reorg: deletions could not be applied in {} attempts "
                "(operation {:#018x}); the UTXO set still holds outputs of blocks that are no "
                "longer on the chain, and this switch will not be published",
                max_deletion_attempts, operation_id);
            return false;
    }

    return false;
}

bool block_chain::publish_reorg_transition(uint32_t connected_height, uint64_t operation_id,
    utxo_write_window const& window,
    boost::unordered_flat_map<utxoz::raw_outpoint, bool> const& absence_tolerated,
    std::span<utxoz::deferred_deletion_entry const> pending_deletes) {
    // Step 6. The deletions the rewind's inverse deltas deferred. Before the
    // barriers, because a barrier over a set that still owes them makes the
    // outputs of abandoned blocks durable instead of removing them, and before
    // the publication, because clearing the record asserts the stores agree.
    if ( ! sweep_reorg_deletions(operation_id, window, absence_tolerated, pending_deletes)) {
        return false;
    }

    // Step 7 and 8. A reorganization READS undo records and writes none, so
    // there is no rev file of its own to cover. The call is made anyway, with
    // the empty set: it still runs the directory barrier, it costs one fsync on
    // a path that runs once per chain switch, and it keeps this path and the
    // connect batch running the same protocol rather than one of them carrying
    // a footnote about why it does not.
    //
    // Through the member wrapper, which reports an absent block_store as
    // file_number -1 rather than dereferencing it.
    if (auto const flushed = flush_undo(std::span<int32_t const>{}); ! flushed) {
        spdlog::critical("[blockchain] Reorg: the undo directory could not be put on stable "
            "storage after the switch (operation {:#018x})", operation_id);
        return false;
    }

    // Step 9. The rewind's inverse deltas live in UTXO-Z, and close() does not
    // sync.
    switch (utxo_sync(window)) {
        case database::barrier_outcome::crossed:
            break;
        case database::barrier_outcome::unsupported:
            if (durability() != database::durability_level::none) {
                spdlog::critical("[blockchain] Reorg: the UTXO store reports no durability "
                    "barrier while this node claims '{}'; the two disagree about the same "
                    "machine", database::to_string(durability()));
                return false;
            }
            spdlog::warn("[blockchain] Reorg: this platform exposes no durability barrier; the "
                "switch is published without one");
            break;
        case database::barrier_outcome::failed:
            spdlog::critical("[blockchain] Reorg: the UTXO store's durability barrier failed "
                "after the switch (operation {:#018x}); what it rewound is not known to be on "
                "disk", operation_id);
            return false;
    }

    // Step 10. Both heights and the clearing of the record, in one transaction.
    // A reorganization moves both: the stored-block height and the built height
    // come back to the same block, and a start that found them apart could not
    // tell which of the two described the set.
    if (auto const published = publish_transition(
            database::transition_heights{
                .last_block_height = connected_height,
                .utxo_built_height = connected_height});
        published != database::result_code::success) {
        spdlog::critical("[blockchain] Reorg: could not publish the switch at height {} "
            "(operation {:#018x}); the UTXO set has moved and nothing records where to",
            connected_height, operation_id);
        return false;
    }

    // Step 11. And onto the disk. Until this returns, a restart can still find
    // the record over a rewind that is already complete.
    if (auto const synced = env_sync(); synced != database::result_code::success) {
        spdlog::critical("[blockchain] Reorg: the published state at height {} could not be put "
            "on stable storage (operation {:#018x})", connected_height, operation_id);
        return false;
    }

    return true;
}
#endif

::asio::awaitable<block_chain::switch_result> block_chain::switch_to_branch_async(
    database::header_index::index_t branch_head, uint32_t fork_height) {

    using result_channel = ::asio::experimental::concurrent_channel<void(std::error_code, switch_result)>;
    result_channel done(co_await ::asio::this_coro::executor, 1);

    ::asio::post(priority_pool_.get_executor(), [this, branch_head, fork_height, &done]() {
        auto result = switch_to_branch(branch_head, fork_height);
        std::ignore = done.try_send(std::error_code{}, result);
    });

    auto [ec, result] = co_await done.async_receive(::asio::as_tuple(::asio::use_awaitable));
    if (ec) {
        co_return switch_result{};
    }
    co_return result;
}

std::expected<void, database::result_code> block_chain::compact_utxo() {
    auto window = utxo_gate_.write();
    if ( ! window) {
        return std::unexpected(window.error());
    }

    // Compaction merges entries out of older version files and removes the ones
    // left empty. It writes, and a fault partway leaves the store having
    // published a merge it could not finish retiring — which is exactly the
    // state UTXO-Z latches on. So it is declared BEFORE the call.
    window->mark_mutating();

    auto const compacted = utxoz_db_.with_write(*window,
        [](auto& db) { return db.compact_utxo(); });

    if ( ! compacted) {
        // The window poisons on the way out because it is left incomplete; this
        // says the same thing from the other direction, so the two agree even if
        // one of them is ever removed.
        if (database::needs_recovery(compacted.error())) {
            utxo_gate_.latch_observed();
        }
        // Left incomplete, so the window poisons on the way out. Deliberate even
        // for the ordinary failures: the library documents a compaction fault as
        // fatal — it reports rather than repairs, and it will not choose between
        // duplicate entries — so "probably intact" is not a state to carry on
        // from.
        return std::unexpected(compacted.error());
    }

    // The safe boundary for THIS operation, and it is not the same one a connect
    // batch has. Compaction changes which files hold the set, not what the set
    // contains, so there is no second store for it to agree with and no barrier
    // it owes; UTXO-Z's own contract attaches no durability requirement to it.
    // Success is completion.
    window->mark_mutated();
    window->complete();
    return {};
}

std::expected<void, database::result_code> block_chain::print_utxo_statistics() {
    // The exclusive window, and deliberately not a lease: UTXO-Z recomputes the
    // fragmentation counters here and documents that it must not overlap a
    // mutation nor another statistics call. The two reports below are const on
    // both sides and are genuine observations.
    //
    // Never marked. Recomputing a counter changes no entry, so a window that
    // dies here leaves nothing half-applied and must not latch the gate.
    auto window = utxo_gate_.write();
    if ( ! window) {
        return std::unexpected(window.error());
    }
    utxoz_db_.with_write(*window, [](auto& db) { db.print_statistics(); });
    return {};
}

std::expected<void, database::result_code> block_chain::print_utxo_sizing_report() {
    auto lease = utxo_gate_.read();
    if ( ! lease) {
        return std::unexpected(lease.error());
    }
    utxoz_db_.with_read(*lease, [](auto const& db) { db.print_sizing_report(); });
    return {};
}

std::expected<void, database::result_code> block_chain::print_utxo_height_range_stats() {
    auto lease = utxo_gate_.read();
    if ( ! lease) {
        return std::unexpected(lease.error());
    }
    utxoz_db_.with_read(*lease, [](auto const& db) { db.print_height_range_stats(); });
    return {};
}

std::expected<size_t, database::result_code> block_chain::utxo_count() const {
    auto lease = utxo_gate_.read();
    if ( ! lease) {
        return std::unexpected(lease.error());
    }
    // No latch to observe here: UTXO-Z documents size() as answering even on a
    // latched instance, precisely so a database in trouble can still be
    // described. The gate refusing the lease above is the only way this fails.
    return utxoz_db_.with_read(*lease, [](auto const& db) { return db.size(); });
}

// Signatures untouched: what these should be is the subject of the
// save_utxo_bloom audit (#661), and neither has a caller today. They install an
// in-memory filter and apply nothing, so they are never marked — a latched gate
// simply means the filter is not installed.
void block_chain::set_utxo_bloom(std::shared_ptr<database::utxo_bloom_filter const> bloom) {
    auto window = utxo_gate_.write();
    if ( ! window) {
        return;
    }
    utxoz_db_.with_write(*window, [&](auto& db) { db.set_utxo_bloom(std::move(bloom)); });
}

void block_chain::clear_utxo_bloom() {
    auto window = utxo_gate_.write();
    if ( ! window) {
        return;
    }
    utxoz_db_.with_write(*window, [](auto& db) { db.clear_utxo_bloom(); });
}

#endif // ! defined(KTH_DB_READONLY)

// =============================================================================
// CHAIN STATE
// =============================================================================

bool block_chain::begin_transition() {
    return capture_gate_.begin_transition();
}

void block_chain::end_transition() {
    capture_gate_.end_transition();
}

bool block_chain::transition_in_progress() const {
    return capture_gate_.transition_in_progress();
}

block_chain::sync_status block_chain::synchronization() const {
    auto const view = chain_view();
    if ( ! view) {
        return {false, false};
    }

    // The connected tip against the header tip. The stored block marker is not
    // this: during a sync it runs thousands of blocks ahead of what is
    // connected, so a node with fresh headers and an unbuilt UTXO set would
    // read as caught up (#605 was the same confusion one level down).
    auto const header_tip = header_index_.active_tip_height();
    bool const caught_up = header_tip >= 0 &&
        view->connected_tip_height == size_t(header_tip);

    // A limit of zero disables the clock half. It does not say the node is at
    // the head — that is what caught_up is for, and it still has to hold.
    if (notify_limit_seconds_ == 0) {
        return {caught_up, true};
    }

    auto const tip_header = get_header(view->connected_tip_height);
    if ( ! tip_header) {
        return {caught_up, false};
    }

    auto const now = static_cast<uint32_t>(zulu_time());
    auto const stamp = tip_header->timestamp();

    // Subtract from now rather than add to the stamp. The sum is one addition
    // away from wrapping wherever it lands in 32 bits, and a wrapped sum reads
    // as fresh — the direction that keeps a stale node serving mining work.
    // Subtracting cannot wrap: it floors at zero, where every timestamp is
    // fresh, which is also the honest answer for a limit longer than the epoch.
    // Both operands are uint32_t so the floor is zero and not a signed minimum.
    auto const limit = notify_limit_seconds_ > time_t(std::numeric_limits<uint32_t>::max())
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(notify_limit_seconds_);

    bool const fresh = stamp >= floor_subtract(now, limit);
    return {caught_up, fresh};
}

block_chain::chain_view_ptr block_chain::chain_view() const {
    return chain_view_.load();
}

code block_chain::publish_chain_view(size_t connected_tip_height) {
    // Both halves come from the one height, under whatever exclusion the caller
    // holds — so the state and the hash cannot describe different chains, and
    // there is no combination for a caller to assemble.
    auto state = chain_state_populator_.populate(connected_tip_height);
    if ( ! state) {
        spdlog::error("[blockchain] Could not build the chain state at height {}", connected_tip_height);
        return error::pool_state_failed;
    }

    auto const tip_hash = get_block_hash(connected_tip_height);
    if ( ! tip_hash) {
        spdlog::error("[blockchain] Could not resolve the block hash at height {}", connected_tip_height);
        return error::pool_state_failed;
    }

    // Allocate before the counter moves, and take the number last. Reversed —
    // reading the counter while building the argument — an allocation that threw
    // would leave a generation that no published view carries: a reader
    // comparing numbers would see one it can never be given, and conclude
    // something was published that was not.
    boost::shared_ptr<published_chain_view> built;
    try {
        built = boost::make_shared<published_chain_view>();
    } catch (std::exception const& e) {
        // The only throwing step here, and the reason this returns a code rather
        // than letting it escape: the callers publish at the close of a batch or
        // a reorganization and decide what to do about a failure. An exception
        // out of an API that promises a code would bypass that decision.
        spdlog::error("[blockchain] Could not allocate the chain view at height {}: {}",
                      connected_tip_height, e.what());
        return error::pool_state_failed;
    }

    built->state = std::move(state);
    built->connected_tip_height = connected_tip_height;
    built->tip_hash = *tip_hash;
    built->generation = view_generation_.fetch_add(1, std::memory_order_relaxed) + 1;

    // One swap. A reader holds the previous triple or this one.
    chain_view_.store(boost::shared_ptr<published_chain_view const>(std::move(built)));
    return error::success;
}

domain::chain::chain_state::ptr block_chain::chain_state() const {
    auto const view = chain_view();
    return view ? view->state : nullptr;
}

domain::chain::chain_state::ptr block_chain::chain_state(branch::const_ptr branch) const {
    // The branch's state is seeded from the published one, and `populate`
    // dereferences that seed. Before the first publication there is nothing to
    // seed from, so this answers null rather than reading through it — the one
    // window is a chain constructed but not started, which start() closes by
    // publishing.
    auto const seed = chain_state();
    if ( ! seed) {
        spdlog::error("[blockchain] A branch state was asked for before any chain view was published");
        return {};
    }
    return chain_state_populator_.populate(seed, branch);
}

block_validation_store& block_chain::block_validations() const {
    return block_validations_;
}

transaction_validation_store& block_chain::transaction_validations() const {
    return transaction_validations_;
}

namespace {

// Holds the validation mutex at high priority for a scope. The exclusion the
// mempool depends on must not rest on the body never throwing.
struct validation_high_priority_lock {
    explicit validation_high_priority_lock(prioritized_mutex& mutex) : mutex_(mutex) {
        mutex_.lock_high_priority();
    }
    ~validation_high_priority_lock() { mutex_.unlock_high_priority(); }
    validation_high_priority_lock(validation_high_priority_lock const&) = delete;
    validation_high_priority_lock& operator=(validation_high_priority_lock const&) = delete;
private:
    prioritized_mutex& mutex_;
};

} // namespace

code block_chain::mempool_remove_for_block(byte_span raw) {
    // Lock-order control (#649). The organizer takes validation_mutex_ and THEN a
    // UTXO read lease inside validator_.accept(); arriving here still holding the
    // UTXO window would take the same two in the opposite order, and two threads
    // doing both at once is the AB-BA deadlock. Refused by name, at the site,
    // rather than found later as a node that stopped for no stated reason.
    if (utxo_gate_.held_by_current_thread()) {
        // Logged before it is thrown, because the throw does not survive the
        // trip: it leaves a coroutine, and what an operator ends up reading is
        // whatever downstream notices the batch died — "two chain transitions
        // overlapped", which names a consequence and not this cause.
        spdlog::critical("[blockchain] The mempool update was reached while this thread still "
            "holds the UTXO write window; the window must end after the durability barrier "
            "and before the mempool update, or this deadlocks against transaction validation");
        throw utxo_lock_order_error(
            "mempool_remove_for_block was called while this thread holds the UTXO "
            "write window; the window must end before the mempool update");
    }

    validation_high_priority_lock const lock(validation_mutex_);

    // Under the lock, so an admission cannot land between finding the pool empty
    // and deciding there is nothing to do. Empty is the whole of initial sync,
    // where parsing every block for this would be the only reason to parse it.
    if (mempool_.size() == 0) {
        return error::success;
    }

    byte_reader reader(raw);
    auto block = domain::message::block::from_data(reader, 0u);
    if ( ! block) {
        return error::operation_failed;
    }

    mempool_.remove_for_block(*block);
    return error::success;
}

void block_chain::mempool_remove_for_block(domain::chain::block const& block) {
    // Lock-order control (#649). The organizer takes validation_mutex_ and THEN a
    // UTXO read lease inside validator_.accept(); arriving here still holding the
    // UTXO window would take the same two in the opposite order, and two threads
    // doing both at once is the AB-BA deadlock. Refused by name, at the site,
    // rather than found later as a node that stopped for no stated reason.
    if (utxo_gate_.held_by_current_thread()) {
        // Logged before it is thrown, because the throw does not survive the
        // trip: it leaves a coroutine, and what an operator ends up reading is
        // whatever downstream notices the batch died — "two chain transitions
        // overlapped", which names a consequence and not this cause.
        spdlog::critical("[blockchain] The mempool update was reached while this thread still "
            "holds the UTXO write window; the window must end after the durability barrier "
            "and before the mempool update, or this deadlocks against transaction validation");
        throw utxo_lock_order_error(
            "mempool_remove_for_block was called while this thread holds the UTXO "
            "write window; the window must end before the mempool update");
    }

    validation_high_priority_lock const lock(validation_mutex_);
    mempool_.remove_for_block(block);
}

blockchain::mempool& block_chain::mempool_ref() {
    return mempool_;
}

blockchain::mempool const& block_chain::mempool_ref() const {
    return mempool_;
}

// =============================================================================
// SUBSCRIPTIONS
// =============================================================================

block_chain::block_channel_ptr block_chain::subscribe_blockchain() {
    return block_organizer_.subscribe();
}

block_chain::transaction_channel_ptr block_chain::subscribe_transaction() {
    return transaction_organizer_.subscribe();
}

block_chain::ds_proof_channel_ptr block_chain::subscribe_ds_proof() {
    return transaction_organizer_.subscribe_ds_proof();
}

void block_chain::unsubscribe_blockchain(block_channel_ptr const& channel) {
    block_organizer_.unsubscribe(channel);
}

void block_chain::unsubscribe_transaction(transaction_channel_ptr const& channel) {
    transaction_organizer_.unsubscribe(channel);
}

void block_chain::unsubscribe_ds_proof(ds_proof_channel_ptr const& channel) {
    transaction_organizer_.unsubscribe_ds_proof(channel);
}

// =============================================================================
// VALIDATION
// =============================================================================

::asio::awaitable<code> block_chain::transaction_validate(transaction_const_ptr tx) const {
    co_return co_await transaction_organizer_.transaction_validate(tx);
}

// =============================================================================
// PROPERTIES
// =============================================================================

// The timestamp of the validated header tip, or nullopt when it cannot be
// established. THREE distinct ways to fail, kept apart because they are
// different states and each is separately reachable:
//
//   * no active chain at all — the index before anything populates it;
//   * a null index for a height the tip read just reported, which is a reorg
//     truncating the active chain between the two reads. header_index publishes
//     the lowered size FIRST precisely so a concurrent reader sees a shorter
//     chain rather than a stale entry, so this is a real race, not a
//     hypothetical one;
//   * a zero timestamp, which no valid header carries.
//
// Read from the in-memory validated index rather than the by-height table: that
// table is written by the header-persist task and lags the index by whole sync
// cycles, which would make recency depend on an unrelated schedule.
std::optional<uint32_t> block_chain::active_tip_timestamp() const {
    auto const tip_height = header_index_.active_tip_height();
    if (tip_height < 0) {
        return std::nullopt;
    }
    auto const idx = header_index_.active_at(tip_height);
    if (idx == database::header_index::null_index) {
        return std::nullopt;
    }
    auto const timestamp = header_index_.get_timestamp(idx);
    if (timestamp == 0) {
        return std::nullopt;
    }
    return timestamp;
}

bool recency_is_stale(std::optional<uint32_t> tip_timestamp, time_t limit_seconds) {
    if (limit_seconds == 0) {
        return false;
    }

    // Unknown answers STALE, on every one of the paths above. Being wrongly
    // considered current is what relaxes batching and lets the node act as if it
    // had caught up; being wrongly considered behind only costs a poll.
    if ( ! tip_timestamp) {
        return true;
    }

    auto const now = static_cast<uint32_t>(zulu_time());
    auto const limit = limit_seconds > time_t(std::numeric_limits<uint32_t>::max())
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(limit_seconds);

    // Keep both operands unsigned so floor_subtract saturates at zero. The old
    // local time_t overload saturated at TIME_MIN and bypassed the constrained
    // helper entirely; its comparison happened to produce the expected boolean
    // while expressing the wrong cutoff.
    return *tip_timestamp < floor_subtract(now, limit);
}

bool block_chain::is_stale() const {
    // The VALIDATED HEADER TIP, and deliberately not the connected tip (#653).
    //
    // Recency answers "is this node behind the network", which the header chain
    // knows long before the UTXO set does — in a mainnet sync the headers reach
    // the tip in about a minute and the build takes an hour. Asking the connected
    // tip instead makes the answer depend on the very progress it gates: the
    // builder drains a remainder shorter than one batch only when NOT stale, so a
    // connected tip days behind keeps it stale, keeps the remainder unbuilt, and
    // keeps the tip where it was. That livelock parked a node 941 blocks short of
    // the tip for over an hour, with no error.
    //
    // Headers are validated — proof of work and difficulty — so a recent
    // timestamp here costs real work and is not something a peer can simply
    // assert. No new durable marker is needed: recency is a question about now,
    // not about what survives a crash.
    //
    // Every failure to establish the tip answers STALE — see the two functions
    // above, which is where those paths live and where they are tested.
    return recency_is_stale(active_tip_timestamp(), notify_limit_seconds_);
}

settings const& block_chain::chain_settings() const {
    return settings_;
}

block_chain::executor_type block_chain::executor() const {
    return priority_pool_.get_executor();
}

std::filesystem::path block_chain::data_dir() const {
    return database_.internal_db_dir.parent_path();
}


// =============================================================================
// DATABASE READERS (Low-level, NOT thread safe)
// =============================================================================

// bool block_chain::get_last_height(size_t& out_height) const {
//     auto result = database_.internal_db().get_last_heights();
//     if ( ! result) {
//         return false;
//     }
//     out_height = result->first;  // header_height
//     return true;
// }

std::expected<heights_t, database::result_code> block_chain::get_last_heights() const {
    return database_.internal_db().get_last_heights();
}

std::expected<domain::chain::header, database::result_code> block_chain::get_header(size_t height) const {
    return database_.internal_db().get_header(height);
}

std::expected<database::header_with_abla_state_t, database::result_code> block_chain::get_header_and_abla_state(size_t height) const {
    return database_.internal_db().get_header_and_abla_state(height);
}

std::expected<domain::chain::header::list, database::result_code> block_chain::get_headers(size_t from, size_t to) const {
    return database_.internal_db().get_headers(from, to);
}

std::expected<size_t, database::result_code> block_chain::get_height(hash_digest const& block_hash) const {
    auto result = database_.internal_db().get_header(block_hash);
    if ( ! result) {
        return std::unexpected(result.error());
    }
    return result->second;
}

std::expected<uint32_t, database::result_code> block_chain::get_bits(size_t height) const {
    auto result = database_.internal_db().get_header(height);
    if ( ! result) {
        return std::unexpected(result.error());
    }
    return result->bits();
}

std::expected<uint32_t, database::result_code> block_chain::get_timestamp(size_t height) const {
    auto result = database_.internal_db().get_header(height);
    if ( ! result) {
        return std::unexpected(result.error());
    }
    return result->timestamp();
}

std::expected<uint32_t, database::result_code> block_chain::get_version(size_t height) const {
    auto result = database_.internal_db().get_header(height);
    if ( ! result) {
        return std::unexpected(result.error());
    }
    return result->version();
}

std::expected<hash_digest, database::result_code> block_chain::get_block_hash(size_t height) const {
    auto const result = database_.internal_db().get_header(height);
    if ( ! result) {
        return std::unexpected(result.error());
    }
    return domain::chain::hash(*result);
}

bool block_chain::header_exists(hash_digest const& block_hash) const {
    return database_.internal_db().get_header(block_hash).has_value();
}

bool block_chain::block_exists(hash_digest const& block_hash) const {
    // Check if full block exists (not just header)
    // With headers-first sync, headers may exist without full blocks
    auto const header_result = database_.internal_db().get_header(block_hash);
    if (!header_result) {
        return false;  // Header doesn't exist, so block doesn't exist
    }

    // Header exists - check if we have the full block
    auto const heights = database_.internal_db().get_last_heights();
    if (!heights) {
        return false;
    }

    auto const this_block_height = header_result->second;

    // Block exists only if its height <= block_height
    return this_block_height <= heights->block;
}

std::expected<uint256_t, database::result_code> block_chain::get_branch_work(uint256_t const& maximum, size_t from_height) const {
    auto const heights = get_last_heights();
    if ( ! heights) {
        return std::unexpected(heights.error());
    }
    // Use block_height (not header_height) for work comparison
    // With headers-first sync, we may have headers without full blocks
    auto const top = heights->block;

    uint256_t out_work = 0;
    for (uint32_t height = from_height; height <= top && out_work < maximum; ++height) {
        auto const result = database_.internal_db().get_header(height);
        if ( ! result) {
            return std::unexpected(result.error());
        }
        out_work += domain::chain::header::proof(result->bits());
    }

    return out_work;
}

std::expected<block_chain::output_info, database::result_code> block_chain::get_utxo(
    domain::chain::output_point const& outpoint, size_t branch_height) const {

    // Use UTXO-Z high-performance database
    auto lease = utxo_gate_.read();
    if ( ! lease) {
        return std::unexpected(lease.error());
    }
    auto entry = latch_if_store_reports_recovery(
        utxoz_db_.with_read(*lease,
            [&](auto const& db) { return db.find(outpoint, static_cast<uint32_t>(branch_height)); }),
        utxo_gate_);
    if ( ! entry) {
        return std::unexpected(entry.error());
    }
    if (entry->height() > branch_height) {
        return std::unexpected(database::result_code::key_not_found);
    }

    return output_info{
        entry->output(),
        entry->height(),
        entry->median_time_past(),
        entry->coinbase()
    };
}

// =============================================================================
// FETCH OPERATIONS (Thread safe, coroutine-based)
// =============================================================================

// DEPRECATED: Block storage moved to flat files (blk*.dat)
// These functions will return not_found until we implement reading from flat files
awaitable_expected<std::pair<block_const_ptr, size_t>>
block_chain::fetch_block(size_t height) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    // LMDB block storage removed - blocks now in flat files
    (void)height;
    co_return std::unexpected(error::not_found);
}

awaitable_expected<std::pair<block_const_ptr, size_t>>
block_chain::fetch_block(hash_digest const& hash) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    // LMDB block storage removed - blocks now in flat files
    (void)hash;
    co_return std::unexpected(error::not_found);
}

std::expected<domain::chain::block::list, database::result_code>
block_chain::fetch_blocks(uint32_t from, uint32_t to) const {
    // LMDB block storage removed - blocks now in flat files
    (void)from;
    (void)to;
    return std::unexpected(database::result_code::other);
}

std::expected<std::vector<data_chunk>, database::result_code>
block_chain::fetch_blocks_raw(uint32_t from, uint32_t to) const {
    // Read blocks from flat files using positions stored in header_index
    // NOTE: During IBD, blocks are stored sequentially, so index == height
    std::vector<database::flat_file_pos> positions;
    positions.reserve(to - from + 1);

    for (uint32_t h = from; h <= to; ++h) {
        // Resolve through the active chain — the index also holds side branches,
        // numbered in arrival order, so an entry's index is not its height.
        auto const idx = header_index_.active_at(static_cast<int32_t>(h));

        if (idx == header_index::null_index) {
            spdlog::error("[blockchain] fetch_blocks_raw: Height {} is not on the active chain", h);
            return std::unexpected(database::result_code::key_not_found);
        }

        if (!header_index_.has_block_data(idx)) {
            spdlog::error("[blockchain] fetch_blocks_raw: No block data for height {}", h);
            return std::unexpected(database::result_code::key_not_found);
        }

        auto file_num = header_index_.get_file_number(idx);
        auto data_pos = header_index_.get_data_pos(idx);
        positions.emplace_back(file_num, data_pos);
    }

    // Read all blocks from flat files
    auto result = block_store_->read_blocks_raw(positions);
    if (!result) {
        return std::unexpected(database::result_code::other);
    }

    return std::move(*result);
}

awaitable_expected<std::pair<header_ptr, size_t>>
block_chain::fetch_block_header(size_t height) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto result = database_.internal_db().get_header(height);
    if ( ! result) {
        co_return std::unexpected(error::not_found);
    }

    co_return std::pair{std::make_shared<header>(std::move(*result)), height};
}

awaitable_expected<std::pair<header_ptr, size_t>>
block_chain::fetch_block_header(hash_digest const& hash) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto result = database_.internal_db().get_header(hash);
    if ( ! result) {
        co_return std::unexpected(error::not_found);
    }

    co_return std::pair{std::make_shared<header>(std::move(result->first)), result->second};
}

awaitable_expected<size_t>
block_chain::fetch_block_height(hash_digest const& hash) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto const result = database_.internal_db().get_header(hash);
    if ( ! result) {
        co_return std::unexpected(error::not_found);
    }

    co_return result->second;
}

awaitable_expected<std::tuple<hash_digest, uint32_t, size_t>>
block_chain::fetch_block_hash_timestamp(size_t height) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto const result = database_.internal_db().get_header(height);
    if ( ! result) {
        co_return std::unexpected(error::not_found);
    }

    co_return std::tuple{domain::chain::hash(*result), result->timestamp(), height};
}

// DEPRECATED: Block storage moved to flat files (blk*.dat)
awaitable_expected<std::tuple<header_const_ptr, size_t, std::shared_ptr<hash_list>, uint64_t>>
block_chain::fetch_block_header_txs_size(hash_digest const& hash) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    // LMDB block storage removed - blocks now in flat files
    (void)hash;
    co_return std::unexpected(error::not_found);
}

awaitable_expected<heights_t>
block_chain::fetch_last_height() const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto result = database_.internal_db().get_last_heights();
    if ( ! result) {
        co_return std::unexpected(error::not_found);
    }

    co_return *result;
}

// DEPRECATED: Block storage moved to flat files (blk*.dat)
awaitable_expected<std::pair<merkle_block_ptr, size_t>>
block_chain::fetch_merkle_block(size_t height) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    // LMDB block storage removed - blocks now in flat files
    (void)height;
    co_return std::unexpected(error::not_found);
}

// DEPRECATED: Block storage moved to flat files (blk*.dat)
awaitable_expected<std::pair<merkle_block_ptr, size_t>>
block_chain::fetch_merkle_block(hash_digest const& hash) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    // LMDB block storage removed - blocks now in flat files
    (void)hash;
    co_return std::unexpected(error::not_found);
}

awaitable_expected<std::pair<compact_block_ptr, size_t>>
block_chain::fetch_compact_block(size_t /*height*/) const {
    co_return std::unexpected(error::not_implemented);
}

awaitable_expected<std::pair<compact_block_ptr, size_t>>
block_chain::fetch_compact_block(hash_digest const& hash) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto block_result = co_await fetch_block(hash);
    if ( ! block_result.has_value()) {
        co_return std::unexpected(block_result.error());
    }

    auto const& [blk, height] = block_result.value();
    auto compact = std::make_shared<compact_block>(compact_block::factory_from_block(*blk));

    co_return std::pair{compact, height};
}

awaitable_expected<std::tuple<transaction_const_ptr, size_t, size_t>>
block_chain::fetch_transaction(hash_digest const& hash, bool require_confirmed) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    // Confirmed transactions are no longer indexed by hash (the LMDB transaction
    // store was removed; blocks live in flat files). Reading a tx by hash will be
    // reimplemented over the flat-file block store. See issue #491.
    (void)hash;
    (void)require_confirmed;
    co_return std::unexpected(error::not_found);
}

awaitable_expected<std::pair<size_t, size_t>>
block_chain::fetch_transaction_position(hash_digest const& hash, bool require_confirmed) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    // Confirmed transactions are no longer indexed by hash (the LMDB transaction
    // store was removed; blocks live in flat files). Reading a tx position by hash
    // will be reimplemented over the flat-file block store. See issue #491.
    (void)hash;
    (void)require_confirmed;
    co_return std::unexpected(error::not_found);
}

awaitable_expected<transaction_const_ptr>
block_chain::fetch_unconfirmed_transaction(hash_digest const& hash) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto const tx = mempool_.get(hash);
    if ( ! tx) {
        co_return std::unexpected(error::not_found);
    }

    co_return tx;
}

// Modified to use get_header instead of get_block (blocks now in flat files)
awaitable_expected<inventory_ptr>
block_chain::fetch_locator_block_hashes(get_blocks_const_ptr locator,
                                        hash_digest const& threshold,
                                        size_t limit) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    uint32_t start = 0;
    for (auto const& hash : locator->start_hashes()) {
        auto const result = database_.internal_db().get_header(hash);
        if (result) {
            start = result->second;
            break;
        }
    }

    auto begin = *safe_add(start, uint32_t(1));
    auto end = *safe_add(begin, uint32_t(limit));

    if (locator->stop_hash() != null_hash) {
        auto const result = database_.internal_db().get_header(locator->stop_hash());
        if (result) {
            end = std::min(result->second, end);
        }
    }

    if (threshold != null_hash) {
        auto const result = database_.internal_db().get_header(threshold);
        if (result) {
            begin = std::max(result->second, begin);
        }
    }

    inventory_vector::list inventories;
    inventories.reserve(floor_subtract(end, begin));

    for (auto height = begin; height < end; ++height) {
        auto const result = database_.internal_db().get_header(height);
        if ( ! result) {
            inventories.shrink_to_fit();
            break;
        }
        static auto const id = inventory::type_id::block;
        inventories.emplace_back(id, domain::chain::hash(*result));
    }

    auto hashes = inventory::create(std::move(inventories));
    if ( ! hashes) {
        co_return std::unexpected(hashes.error());
    }

    co_return std::make_shared<inventory>(std::move(*hashes));
}

awaitable_expected<headers_ptr>
block_chain::fetch_locator_block_headers(get_headers_const_ptr locator,
                                         hash_digest const& threshold,
                                         size_t limit) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    size_t start = 0;
    for (auto const& hash : locator->start_hashes()) {
        auto const result = database_.internal_db().get_header(hash);
        if (result) {
            start = result->second;
            break;
        }
    }

    auto begin = *safe_add(start, size_t(1));
    auto end = *safe_add(begin, limit);

    if (locator->stop_hash() != null_hash) {
        auto const result = database_.internal_db().get_header(locator->stop_hash());
        if (result) {
            end = std::min(size_t(result->second), end);
        }
    }

    if (threshold != null_hash) {
        auto const result = database_.internal_db().get_header(threshold);
        if (result) {
            begin = std::max(size_t(result->second), begin);
        }
    }

    domain::message::header::list elements;
    elements.reserve(floor_subtract(end, begin));

    for (auto height = begin; height < end; ++height) {
        auto const result = database_.internal_db().get_header(height);
        if ( ! result) {
            elements.shrink_to_fit();
            break;
        }
        elements.push_back(*result);
    }

    auto message = domain::message::headers::create(std::move(elements));
    if ( ! message) {
        co_return std::unexpected(message.error());
    }

    co_return std::make_shared<domain::message::headers>(std::move(*message));
}

awaitable_expected<get_headers_ptr>
block_chain::fetch_block_locator(block::indexes const& heights) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto message = std::make_shared<domain::message::get_headers>();
    auto& hashes = message->start_hashes();
    hashes.reserve(heights.size());

    for (auto const height : heights) {
        auto const result = database_.internal_db().get_header(height);
        if ( ! result) {
            co_return std::unexpected(error::not_found);
        }
        hashes.push_back(domain::chain::hash(*result));
    }

    co_return message;
}

// The confirmed address index (history / spend) was backed by the LMDB history
// and spend stores, which the v1 node never populates (blocks live in flat files,
// the UTXO set in UTXO-Z). The stores and these queries were removed; a confirmed
// address index will be reintroduced on the v1 model if/when it is needed.

awaitable_expected<double_spend_proof_const_ptr>
block_chain::fetch_ds_proof(hash_digest const& hash) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }
    co_return co_await transaction_organizer_.fetch_ds_proof(hash);
}

// =============================================================================
// MEMPOOL / TRANSACTION POOL
// =============================================================================

// ############################################################################
// TODO(mempool): KTH has no working mempool. The abandoned mining::mempool and
// the LMDB "transaction unconfirmed" storage that used to back these queries
// have been removed to rebuild the mempool from scratch (concurrent hashmap +
// on-disk persistence). Until the new mempool lands, every mempool read/query
// and the tx sink abort loudly (via mempool_not_implemented, defined near the
// top of this file) instead of silently returning wrong (empty) results.
// Tracked in issue #491.
// ############################################################################

awaitable_expected<blockchain::block_template>
block_chain::fetch_template() const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto const ready = synchronization();
    if ( ! ready.synchronized()) {
        co_return std::unexpected(ready.caught_up ? error::node_stale : error::node_behind);
    }

    // Capture inside the gate, build outside it. Asking whether a transition is
    // running and then capturing are two moments; entry is what joins them.
    chain_view_ptr published;
    blockchain::pool_view view;
    {
        captured_lease const lease(capture_gate_);
        if ( ! lease) {
            co_return std::unexpected(error::transition_in_progress);
        }

        published = chain_view();
        if ( ! published) {
            co_return std::unexpected(error::not_found);
        }
        view = lease_pool_view(validation_mutex_, mempool_);
    }

    auto const& state = published->state;

    co_return build_block_template(view.entries, block_template_context{
        state->dynamic_max_block_size(),
        state->dynamic_max_block_sigchecks(),
        state->height(),
        state->median_time_past()});
}

awaitable_expected<blockchain::mining_template>
block_chain::fetch_mining_template(uint64_t coinbase_reserve_size) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto const ready = synchronization();
    if ( ! ready.synchronized()) {
        co_return std::unexpected(ready.caught_up ? error::node_stale : error::node_behind);
    }

    // This capture decides only whether an ALREADY BUILT template can be served.
    // That is a weaker requirement than building one: a template is a coherent
    // copy, so a request admitted before a transition may legitimately finish
    // with it. Nothing here may feed a rebuild — see the recapture below.
    hash_digest served_previous;
    {
        captured_lease const lease(capture_gate_);
        if ( ! lease) {
            co_return std::unexpected(error::transition_in_progress);
        }

        auto const published = chain_view();
        if ( ! published) {
            co_return std::unexpected(error::not_found);
        }

        served_previous = published->tip_hash;
    }

    // Only decides whether to rebuild, so it is read without the lease: a value
    // that has already moved on costs one extra rebuild, and paying the
    // exclusion on every cache hit would cost far more. The label stored with a
    // rebuilt template comes from inside the lease instead — see below.
    auto const generation = mempool_.generation();
    auto now = static_cast<uint32_t>(zulu_time());

    // A snapshot is usable while the tip is unchanged and the mempool is either
    // unchanged or its last change is still within the refresh window; a new tip
    // always forces a rebuild. The tip and generation are parameters rather than
    // captures because this is asked twice, against two different captures.
    auto const usable = [&](boost::shared_ptr<template_snapshot> const& s,
                            hash_digest const& tip, uint64_t gen, uint32_t at) {
        return s &&
               s->previous == tip &&
               s->coinbase_reserve_size == coinbase_reserve_size &&
               (s->generation == gen ||
                at - s->time < settings_.gbt_template_refresh_seconds);
    };

    // Lock-free read: rebuilds are expensive (full mempool scan + fee-rate
    // ordering), so serve the published snapshot when it is still usable.
    auto snapshot = template_cache_.load();
    if (usable(snapshot, served_previous, generation, now)) {
        co_return snapshot->value;
    }

    // Stale or cold. Coalesce rebuilds: exactly one thread rebuilds. If another
    // thread already holds the rebuild lock, do not block on it — serve the
    // previous (bounded-stale) snapshot if we have one. Only a cold start with no
    // snapshot at all waits for the first build.
    //
    // NOTE: there is no co_await between here and co_return; the rebuild is fully
    // synchronous, so holding this std::mutex across it is safe. Do not introduce
    // a suspension point inside this section.
    std::unique_lock<std::mutex> lock(template_rebuild_mutex_, std::try_to_lock);
    if ( ! lock) {
        // Another thread is rebuilding. If our stale snapshot is for the SAME tip
        // (only the mempool moved on), it is still a valid template — serve it
        // rather than block. Never serve a snapshot from a previous tip: a
        // wrong-parent template would orphan the miner's block, so wait for the
        // rebuild in that case (a cold start with no snapshot also waits).
        // The same tip AND the same coinbase budget. A snapshot built for a
        // different reserve leaves a different amount of room for the coinbase,
        // so serving it here would answer this request with someone else's
        // budget — and the caller asked for that number precisely because its
        // coinbase does not fit the default. Wait for the rebuild instead.
        if (snapshot && snapshot->previous == served_previous &&
            snapshot->coinbase_reserve_size == coinbase_reserve_size) {
            co_return snapshot->value;
        }
        lock.lock();
    }

    // Everything a rebuild consumes is captured HERE, inside one lease: the
    // chain state, the tip it names, the height, and the mempool. Reading the
    // chain view under one lease and the pool under another is not the same
    // thing, however briefly each half was admitted — a whole transition can
    // run between the two, and the template would then pair a pre-transition
    // parent and height with a post-transition pool. Each half is coherent with
    // the gate and the pair is coherent with nothing.
    //
    // Lock order: template_rebuild_mutex_ (held above), then the capture gate,
    // then the validation mutex. Nothing takes them the other way round — the
    // organizers hold the validation mutex and know nothing about the template
    // cache, and a transition drains the gate without taking either.
    chain_view_ptr published;
    blockchain::pool_view view;
    {
        captured_lease const lease(capture_gate_);
        if ( ! lease) {
            co_return std::unexpected(error::transition_in_progress);
        }

        published = chain_view();
        if ( ! published) {
            co_return std::unexpected(error::not_found);
        }

        // Re-check against the publication just captured rather than the one
        // the fast path used. The wait on the rebuild mutex may have spanned a
        // whole transition, in which case that earlier check decided nothing —
        // and asking before copying the pool keeps a hit from paying for a copy
        // it will discard.
        now = static_cast<uint32_t>(zulu_time());
        snapshot = template_cache_.load();
        if (usable(snapshot, published->tip_hash, mempool_.generation(), now)) {
            co_return snapshot->value;
        }

        view = lease_pool_view(validation_mutex_, mempool_);
    }

    auto const& state = published->state;

    // The parent comes from the same capture as the height and the state, not
    // from a second lookup keyed on it. That pairing is what used to be wrong:
    // the height was frozen at startup and the hash was read now, so the
    // template could name a parent the chain had left behind — and the cache's
    // guard against exactly that is keyed on this value, so it could never fire.
    hash_digest const previous = published->tip_hash;
    auto const height = state->height();

    // Everything below runs on the copy, outside the gate: holding it across
    // the graph, the ordering and the selection would make a transition wait on
    // work that cannot be affected by it.
    auto selection = build_block_template(view.entries, block_template_context{
        state->dynamic_max_block_size(),
        state->dynamic_max_block_sigchecks(),
        height,
        state->median_time_past(),
        coinbase_reserve_size});

    // 0x20000000: the BIP9 version base. BCH has no active version-bits signaling,
    // and miners routinely override this, so it is only a sensible default.
    auto built = make_mining_template(
        0x20000000U,
        previous,
        height,
        state->work_required(),
        state->median_time_past(),
        now,
        state->dynamic_max_block_size(),
        state->dynamic_max_block_sigchecks(),
        std::move(selection));

    auto next = boost::make_shared<template_snapshot>(
        template_snapshot{std::move(built), previous, view.generation, now, coinbase_reserve_size});
    template_cache_.store(next);
    co_return next->value;
}

awaitable_expected<blockchain::mining_info>
block_chain::fetch_mining_info() const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    auto const published = chain_view();
    if ( ! published) {
        co_return std::unexpected(error::not_found);
    }
    auto const& state = published->state;

    // Every field from the one publication. Reading the height separately is
    // how this reported the current tip beside the difficulty in force when the
    // node started.
    auto const ready = synchronization();

    co_return blockchain::mining_info{
        uint32_t(published->connected_tip_height),
        difficulty_from_bits(state->work_required()),
        mempool_.size(),
        state->network(),
        transition_in_progress(),
        ready.caught_up,
        ready.fresh};
}

awaitable_expected<inventory_ptr>
block_chain::fetch_mempool(size_t count_limit, uint64_t /*minimum_fee*/) const {
    if (stopped()) {
        co_return std::unexpected(error::service_stopped);
    }

    inventory_vector::list inventories;
    mempool_.for_each([&](mempool_entry const& e) {
        if (inventories.size() >= count_limit) {
            return;
        }
        inventories.emplace_back(inventory_vector::type_id::transaction, e.tx->hash());
    });

    auto inv = domain::message::inventory::create(std::move(inventories));
    if ( ! inv) {
        co_return std::unexpected(inv.error());
    }

    co_return std::make_shared<domain::message::inventory>(std::move(*inv));
}

hash_list block_chain::get_mempool_txids() const {
    return mempool_.all_txids();
}

blockchain::mempool_totals block_chain::get_mempool_info() const {
    return mempool_.summary();
}

std::optional<mempool_entry_info> block_chain::get_mempool_entry(hash_digest const& txid) const {
    auto const e = mempool_.entry(txid);
    if ( ! e) {
        return std::nullopt;
    }
    return mempool_entry_info{e->fee, e->size, e->time_seen};
}

hash_list block_chain::get_mempool_depends(hash_digest const& txid) const {
    return mempool_.parents(txid);
}

hash_list block_chain::get_mempool_spentby(hash_digest const& txid) const {
    return mempool_.children(txid);
}

hash_list block_chain::get_mempool_ancestors(hash_digest const& txid) const {
    return mempool_.ancestors(txid);
}

hash_list block_chain::get_mempool_descendants(hash_digest const& txid) const {
    return mempool_.descendants(txid);
}

std::filesystem::path block_chain::mempool_dat_path() const {
    return database_.internal_db_dir.parent_path() / "mempool.dat";
}

bool block_chain::dump_mempool_to_disk() const {
    // Snapshot the pool; the database module owns the on-disk format.
    std::vector<database::mempool_stored_tx> txs;
    txs.reserve(mempool_.size());
    mempool_.for_each([&](mempool_entry const& e) {
        txs.push_back({e.tx, e.time_seen});
    });
    return database::store_mempool(mempool_dat_path(), std::move(txs));
}

::asio::awaitable<size_t> block_chain::load_mempool_from_disk() {
    auto const persisted = database::load_mempool(mempool_dat_path());

    size_t admitted = 0;
    for (auto const& p : persisted) {
        // Re-validate against the current tip and admit (recomputes fee/size/
        // sigchecks, enforces first-seen). Now-invalid txs are simply dropped.
        auto const ec = co_await organize(p.tx);
        if ( ! ec) {
            ++admitted;
        }
    }

    if ( ! persisted.empty()) {
        spdlog::info("[blockchain] Re-admitted {}/{} persisted mempool transactions", admitted, persisted.size());
    }
    co_return admitted;
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

std::vector<mempool_transaction_summary> block_chain::get_mempool_transactions(
    std::vector<std::string> const& payment_addresses, bool use_testnet_rules) const {

    auto const [encoding_p2kh, encoding_p2sh] = get_address_versions(use_testnet_rules);

    std::vector<mempool_transaction_summary> ret;

    std::unordered_set<kth::domain::wallet::payment_address> addrs;
    for (auto const& payment_address : payment_addresses) {
        if (auto address = kth::domain::wallet::payment_address::parse_from(payment_address); address) {
            addrs.insert(*address);
        }
    }

    mempool_.for_each([&](mempool_entry const& e) {
        auto const& tx = *e.tx;
        size_t i = 0;

        for (auto const& output : tx.outputs()) {
            auto const tx_addresses = kth::domain::wallet::payment_address::extract(
                output.script(), encoding_p2kh, encoding_p2sh);
            for (auto const tx_address : tx_addresses) {
                if (addrs.find(tx_address) != addrs.end()) {
                    ret.push_back(mempool_transaction_summary(
                        tx_address.encoded_cashaddr(false), kth::encode_hash(tx.hash()), "",
                        "", std::to_string(output.value()), i, e.time_seen));
                }
            }
            ++i;
        }

        // Input-side (debit) entries required resolving each spent prevout's value
        // from the confirmed transaction store, which no longer exists (blocks live
        // in flat files, the UTXO set in UTXO-Z). Only credit (output) entries are
        // reported here until this is reworked over the v1 stores. See issue #491.
    });

    return ret;
}

std::vector<mempool_transaction_summary> block_chain::get_mempool_transactions(
    std::string const& payment_address, bool use_testnet_rules) const {
    return get_mempool_transactions(std::vector<std::string>{payment_address}, use_testnet_rules);
}

std::vector<domain::chain::transaction> block_chain::get_mempool_transactions_from_wallets(
    std::vector<domain::wallet::payment_address> const& payment_addresses,
    bool use_testnet_rules) const {

    auto const [encoding_p2kh, encoding_p2sh] = get_address_versions(use_testnet_rules);

    std::vector<domain::chain::transaction> ret;

    mempool_.for_each([&](mempool_entry const& e) {
        auto const& tx = *e.tx;
        bool inserted = false;

        for (auto iter_output = tx.outputs().begin();
             iter_output != tx.outputs().end() && !inserted; ++iter_output) {

            auto const tx_addresses = kth::domain::wallet::payment_address::extract(
                iter_output->script(), encoding_p2kh, encoding_p2sh);

            for (auto iter_addr = tx_addresses.begin();
                 iter_addr != tx_addresses.end() && !inserted; ++iter_addr) {
                auto it = std::find(payment_addresses.begin(), payment_addresses.end(), *iter_addr);
                if (it != payment_addresses.end()) {
                    ret.push_back(tx);
                    inserted = true;
                }
            }
        }

        for (auto iter_input = tx.inputs().begin();
             iter_input != tx.inputs().end() && !inserted; ++iter_input) {

            auto const tx_addresses = kth::domain::wallet::payment_address::extract(
                iter_input->script(), encoding_p2kh, encoding_p2sh);

            for (auto iter_addr = tx_addresses.begin();
                 iter_addr != tx_addresses.end() && !inserted; ++iter_addr) {
                auto it = std::find(payment_addresses.begin(), payment_addresses.end(), *iter_addr);
                if (it != payment_addresses.end()) {
                    ret.push_back(tx);
                    inserted = true;
                }
            }
        }
    });

    return ret;
}

block_chain::mempool_mini_hash_map block_chain::get_mempool_mini_hash_map(
    domain::message::compact_block const& block) const {

    if (stopped()) {
        return mempool_mini_hash_map();
    }

    auto header_hash = hash(block);
    auto k0 = from_little_endian_unsafe<uint64_t>(header_hash);
    auto k1 = from_little_endian_unsafe<uint64_t>(std::span{header_hash}.subspan(sizeof(uint64_t)));

    mempool_mini_hash_map mempool;

    mempool_.for_each([&](mempool_entry const& e) {
        auto const& tx = *e.tx;
        // BIP-152 short id: the low 6 bytes (little-endian) of the SipHash.
        auto const sh = sip_hash_uint256(k0, k1, tx.hash());
        mini_hash short_id;
        std::memcpy(short_id.data(), &sh, short_id.size());
        mempool.emplace(short_id, tx);
    });

    return mempool;
}

void block_chain::fill_tx_list_from_mempool(domain::message::compact_block const& block,
                                            size_t& mempool_count,
                                            std::vector<domain::chain::transaction>& txn_available,
                                            std::unordered_map<uint64_t, uint16_t> const& shorttxids) const {

    std::vector<bool> have_txn(txn_available.size());

    auto header_hash = hash(block);
    auto k0 = from_little_endian_unsafe<uint64_t>(header_hash);
    auto k1 = from_little_endian_unsafe<uint64_t>(std::span{header_hash}.subspan(sizeof(uint64_t)));

    mempool_.for_each([&](mempool_entry const& e) {
        auto const& tx = *e.tx;

        uint64_t shortid = sip_hash_uint256(k0, k1, tx.hash()) & uint64_t(0xffffffffffff);

        auto idit = shorttxids.find(shortid);
        if (idit != shorttxids.end()) {
            if ( ! have_txn[idit->second]) {
                txn_available[idit->second] = tx;
                have_txn[idit->second] = true;
                ++mempool_count;
            } else {
                // A second transaction maps to the same short id, so the slot
                // is ambiguous and gets cleared. A cleared slot holds the null
                // transaction, so clear (and decrement) only once.
                if ( ! txn_available[idit->second].is_null()) {
                    txn_available[idit->second] = domain::chain::transaction::null();
                    --mempool_count;
                }
            }
        }
    });
}

// =============================================================================
// FILTERS
// =============================================================================

::asio::awaitable<code> block_chain::filter_blocks(get_data_ptr message) const {
    if (stopped()) {
        co_return error::service_stopped;
    }

    block_organizer_.filter(message);
    auto const& internal_db = database_.internal_db();

    message->erase_if([&internal_db](auto const& inv) {
        return inv.is_block_type() && internal_db.get_header(inv.hash());
    });

    co_return error::success;
}

::asio::awaitable<code> block_chain::filter_transactions(get_data_ptr message) const {
    if (stopped()) {
        co_return error::service_stopped;
    }

    // Drop inventory for transactions we already hold in the mempool so we do not
    // re-request them. Confirmed transactions are no longer indexed by hash (the
    // LMDB transaction store was removed); a confirmed-tx filter over the
    // flat-file block store is tracked by issue #491.
    message->erase_if([this](auto const& inv) {
        return inv.is_transaction_type() && mempool_.contains(inv.hash());
    });

    co_return error::success;
}

}} // namespace kth::blockchain
