// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/databases/utxoz_database.hpp>

#include <cstring>
#include <spdlog/spdlog.h>

#include <kth/infrastructure/utility/byte_reader.hpp>

#ifdef KTH_UTXOZ_REFERENCE_MODE
#include <kth/database/block_store.hpp>
#include <kth/database/header_index.hpp>
#endif

namespace kth::database {

bool utxoz_reference_mode() {
#ifdef KTH_UTXOZ_REFERENCE_MODE
    return true;
#else
    return false;
#endif
}

// UTXO-Z 0.9.0 grew its error enum from five values to twenty-one. Logging the
// integer, as this used to, turns a diagnosis into a lookup against whichever
// header version the reader happens to open — and the numbers moved, so an old
// note now names a different fault. Naming them here costs one switch and keeps
// the log readable on its own.
//
// The switch lists every code and has no `default`. That asks for a -Wswitch
// warning when UTXO-Z adds one, but this is a warning, not an error, and only
// where it is enabled and promoted — when 0.9.1 added version_unreadable, this
// file compiled clean and the new code would have printed "unrecognised". So the
// trailing return is the defence, and adding a case is a manual step on every
// UTXO-Z bump, not something the build will remind anyone about.
KD_API char const* utxoz_error_name(utxoz::error_code code) {
    switch (code) {
        case utxoz::error_code::not_found:              return "not_found";
        case utxoz::error_code::not_resolved:           return "not_resolved";
        case utxoz::error_code::closed:                 return "closed";
        case utxoz::error_code::storage_mode_mismatch:  return "storage_mode_mismatch";
        case utxoz::error_code::config_file_corrupt:    return "config_file_corrupt";
        case utxoz::error_code::value_too_large:        return "value_too_large";
        case utxoz::error_code::duplicate_key:          return "duplicate_key";
        case utxoz::error_code::catalog_unreadable:     return "catalog_unreadable";
        case utxoz::error_code::version_unreadable:     return "version_unreadable";
        case utxoz::error_code::removal_failed:         return "removal_failed";
        case utxoz::error_code::sync_unsupported:       return "sync_unsupported";
        case utxoz::error_code::sync_failed:            return "sync_failed";
        case utxoz::error_code::rename_failed:          return "rename_failed";
        case utxoz::error_code::database_in_use:        return "database_in_use";
        case utxoz::error_code::database_lock_unavailable: return "database_lock_unavailable";
        case utxoz::error_code::entropy_unavailable:    return "entropy_unavailable";
        case utxoz::error_code::file_open_failed:       return "file_open_failed";
        case utxoz::error_code::identity_collision:     return "identity_collision";
        case utxoz::error_code::insufficient_space:     return "insufficient_space";
        case utxoz::error_code::recovery_required:      return "recovery_required";
        case utxoz::error_code::recovery_failed:        return "recovery_failed";
        case utxoz::error_code::metadata_write_failed:  return "metadata_write_failed";
    }
    return "unrecognised";
}

namespace {

// UTXO-Z's find() can now fail for reasons that are not absence: the database
// is closed, the catalogue could not be read, recovery is required. Folding all
// of them into key_not_found is the sentinel conversion this migration exists
// to avoid — upstream reads key_not_found as absence, and a database that could
// not be read reported that way answers "absent", which the validator reads as
// "spent".
result_code find_failure_to_result_code(utxoz::error_code code) {
    // not_resolved is the ONLY non-error answer find() has in 0.10.0: the active
    // versions cannot say, and nothing was queued on anyone's behalf. It is
    // mapped to its own code rather than to key_not_found, because a caller that
    // reads it as absence reports an output as spent having stopped asking
    // halfway. Everything else is a genuine failure of this node's storage.
    return code == utxoz::error_code::not_resolved
         ? result_code::not_resolved
         : result_code::other;
}

char const* to_string(utxoz::durability_level level) {
    switch (level) {
        case utxoz::durability_level::full:          return "full";
        case utxoz::durability_level::contents_only: return "contents_only";
        case utxoz::durability_level::none:          return "none";
    }
    return "unrecognised";
}

} // namespace

utxoz::durability_level utxoz_platform_durability() {
    return utxoz::platform_durability();
}


utxoz_database::~utxoz_database() {
    close();
}

bool utxoz_database::open(std::filesystem::path const& path, bool remove_existing) {
    if (is_open_) {
        spdlog::warn("[utxoz_database] Database already open");
        return true;
    }

#ifdef KTH_UTXOZ_REFERENCE_MODE
    auto result = utxoz::reference_db::open(path.string(), remove_existing);
#else
    auto result = utxoz::full_db::open(path.string(), remove_existing);
#endif
    if ( ! result) {
        // UTXO-Z 0.9.0 claims the database exclusively. Four of the ways an open
        // can now fail are operational facts an operator can act on, and each
        // sends them somewhere different, so none of them may arrive as a bare
        // number.
        switch (result.error()) {
            case utxoz::error_code::database_in_use:
                spdlog::error("[utxoz_database] Another instance already holds {} — "
                    "UTXO-Z takes an exclusive claim, so a second node cannot open it",
                    path.string());
                break;
            case utxoz::error_code::database_lock_unavailable:
                spdlog::error("[utxoz_database] The claim on {} could not be attempted at all: "
                    "no permission, a filesystem without locking, or a lock file that is not "
                    "a regular file. This is NOT 'someone else has it'", path.string());
                break;
            case utxoz::error_code::recovery_required:
            case utxoz::error_code::recovery_failed:
                spdlog::error("[utxoz_database] {} did not open: {}. Recovery found an "
                    "interrupted operation it will not guess at; the database is left alone",
                    path.string(), utxoz_error_name(result.error()));
                break;
            case utxoz::error_code::storage_mode_mismatch:
                spdlog::error("[utxoz_database] {} was created in the other storage mode. "
                    "This build is {} mode; the files on disk are not", path.string(),
                    utxoz_reference_mode() ? "reference" : "full");
                break;
            default:
                spdlog::error("[utxoz_database] Failed to open database: {}",
                    utxoz_error_name(result.error()));
                break;
        }
        return false;
    }
    db_.emplace(std::move(*result));
    is_open_ = true;
    spdlog::info("[utxoz_database] Opened database at {}", path.string());
    return true;
}

void utxoz_database::close() {
    if (db_.has_value() && is_open_) {
        db_->close();
        db_.reset();
        is_open_ = false;
        spdlog::info("[utxoz_database] Database closed");
    }
}

bool utxoz_database::is_open() const {
    return is_open_ && db_.has_value();
}

size_t utxoz_database::size() const {
    if ( ! is_open()) {
        return 0;
    }
    return db_->size();
}

#ifndef KTH_UTXOZ_REFERENCE_MODE
result_code utxoz_database::insert(domain::chain::point const& point, utxo_entry const& entry) {
    if ( ! is_open()) {
        return result_code::other;
    }

    auto key = point_to_key(point);
    auto value = entry_to_bytes(entry);

    auto result = db_->insert(key, value, entry.height());
    if ( ! result) {
        return result_code::other;
    }
    return *result ? result_code::success : result_code::duplicated_key;
}
#endif

std::expected<utxo_entry, result_code> utxoz_database::find(domain::chain::point const& point, uint32_t height) const {
    if ( ! is_open()) {
        return std::unexpected(result_code::other);
    }

    auto key = point_to_key(point);

#ifdef KTH_UTXOZ_REFERENCE_MODE
    auto result = db_->find(key, height);
    if ( ! result) {
        // not_resolved is the ordinary outcome: the active versions cannot
        // answer, which every caller expects and resolves as a batch. Logging it
        // as an error made a routine miss look like a fault, once per prevout.
        // Anything else IS a fault and still says so.
        if ( ! is_ordinary_probe_miss(result.error())) {
            spdlog::error("[utxoz_database] find() failed for {}: {}; this is not absence",
                utxoz::outpoint_to_string(key), utxoz_error_name(result.error()));
        }
        return std::unexpected(find_failure_to_result_code(result.error()));
    }
    return resolve_reference_ref(*result, point.index());
#else
    auto result = db_->find(key, height);
    if ( ! result) {
        // not_resolved is the ordinary outcome: the active versions cannot
        // answer, which every caller expects and resolves as a batch. Logging it
        // as an error made a routine miss look like a fault, once per prevout.
        // Anything else IS a fault and still says so.
        if ( ! is_ordinary_probe_miss(result.error())) {
            spdlog::error("[utxoz_database] find() failed for {}: {}; this is not absence",
                utxoz::outpoint_to_string(key), utxoz_error_name(result.error()));
        }
        return std::unexpected(find_failure_to_result_code(result.error()));
    }
    return bytes_to_entry(result->data);
#endif
}

namespace {

#ifdef KTH_UTXOZ_REFERENCE_MODE
// Pack a reference the way apply_delta_raw expects it to arrive:
// {file_number(4 LE), tx_offset(4 LE)}.
std::vector<uint8_t> pack_reference_ref(uint32_t file_number, uint32_t offset) {
    std::vector<uint8_t> value(8);
    std::memcpy(value.data(), &file_number, 4);
    std::memcpy(value.data() + 4, &offset, 4);
    return value;
}
#endif

} // namespace

std::expected<utxoz_database::raw_stored, result_code>
utxoz_database::find_raw(utxoz::raw_outpoint const& key, uint32_t height) const {
    if ( ! is_open()) {
        return std::unexpected(result_code::other);
    }

    auto result = db_->find(key, height);
    if ( ! result) {
        // not_resolved means "the active versions cannot answer". It is NOT
        // absence: the caller keeps the request and hands it to resolve() as
        // part of a batch it owns. Any OTHER error is a read that failed, and
        // must not be dressed up as the two-phase contract.
        if ( ! is_ordinary_probe_miss(result.error())) {
            spdlog::error("[utxoz_database] find_raw() failed for {}: {}; this is not "
                "the key being unresolvable", utxoz::outpoint_to_string(key),
                utxoz_error_name(result.error()));
        }
        return std::unexpected(find_failure_to_result_code(result.error()));
    }

#ifdef KTH_UTXOZ_REFERENCE_MODE
    return raw_stored{pack_reference_ref(result->file_number, result->offset), result->block_height};
#else
    return raw_stored{result->data, result->block_height};
#endif
}

std::expected<utxoz_database::raw_resolution, result_code>
utxoz_database::resolve_raw(std::span<utxoz::lookup_request const> requests) const {
    raw_resolution out;

    if ( ! is_open()) {
        return std::unexpected(result_code::other);
    }
    if (requests.empty()) {
        return out;
    }

    // The batch is the caller's throughout: borrowed for this call, and nothing
    // of it survives the return. Two components resolving at once cannot take
    // each other's keys, which is the whole reason this takes an argument.
    auto resolved = db_->resolve(requests);
    if ( ! resolved) {
        // version_unreadable / catalog_unreadable. No lists come back at all,
        // and that is the correct shape: a sweep that could not cover every
        // version has established nothing, and reporting it as an empty
        // resolution would turn a storage fault into a set of absent UTXOs.
        // Nothing was consumed, so the same span can be retried.
        spdlog::error("[utxoz_database] resolving {} lookup(s) failed: {}; this is NOT "
            "the keys being absent", requests.size(), utxoz_error_name(resolved.error()));
        return std::unexpected(result_code::other);
    }

    out.found.reserve(resolved->found.size());
    for (auto const& [key, res] : resolved->found) {
#ifdef KTH_UTXOZ_REFERENCE_MODE
        out.found.emplace(key, raw_stored{pack_reference_ref(res.file_number, res.offset), res.block_height});
#else
        out.found.emplace(key, raw_stored{res.data, res.block_height});
#endif
    }

    out.absent.reserve(resolved->absent.size());
    for (auto const& request : resolved->absent) {
        out.absent.push_back(request.key);
    }

    return out;
}

std::expected<utxoz_database::entry_resolution, result_code>
utxoz_database::resolve(std::span<utxoz::lookup_request const> requests) const {
    entry_resolution out;

    if ( ! is_open()) {
        return std::unexpected(result_code::other);
    }
    if (requests.empty()) {
        return out;
    }

    auto resolved = db_->resolve(requests);
    if ( ! resolved) {
        spdlog::error("[utxoz_database] resolving {} lookup(s) failed: {}; this is NOT "
            "the keys being absent", requests.size(), utxoz_error_name(resolved.error()));
        return std::unexpected(result_code::other);
    }

    out.found.reserve(resolved->found.size());
    for (auto const& [key, res] : resolved->found) {
#ifdef KTH_UTXOZ_REFERENCE_MODE
        auto entry = resolve_reference_ref(res, key_to_point(key).index());
#else
        auto entry = bytes_to_entry(res.data);
#endif
        if ( ! entry) {
            // The resolution FOUND this key; materialising it is what failed.
            // Dropping it would report a UTXO that demonstrably exists as one
            // this node does not have, so the failure is returned instead.
            spdlog::error("[utxoz_database] {} resolved but could not be materialised; "
                "the entry exists and is unreadable", utxoz::outpoint_to_string(key));
            return std::unexpected(entry.error());
        }
        out.found.emplace(key, std::move(*entry));
    }

    out.absent.reserve(resolved->absent.size());
    for (auto const& request : resolved->absent) {
        out.absent.push_back(request.key);
    }

    return out;
}

bool utxoz_database::compact() {
    if ( ! is_open()) {
        return false;
    }
    auto const compacted = db_->compact_all();
    if ( ! compacted) {
        spdlog::error("[utxoz_database] Compaction failed: {}",
            utxoz_error_name(compacted.error()));
        return false;
    }
    return true;
}

barrier_outcome utxoz_database::sync() {
    if ( ! is_open()) {
        // Not `unsupported`: the platform's barrier may exist and simply have
        // nothing to run against here. A caller told "this platform has none"
        // would stop asking; one told the barrier failed stops instead, which
        // is the right answer when a guarantee was wanted from a database this
        // object has let go of.
        spdlog::error("[utxoz_database] sync() on a closed database; no guarantee was obtained");
        return barrier_outcome::failed;
    }

    auto const synced = db_->sync();
    if (synced) {
        return barrier_outcome::crossed;
    }

    // These are three different facts about the machine, and collapsing them
    // into "sync failed" would send an operator looking in the wrong place.
    switch (synced.error()) {
        case utxoz::error_code::sync_unsupported:
            spdlog::warn("[utxoz_database] This platform exposes no durability "
                "barrier ({}); nothing was promised",
                to_string(utxoz::platform_durability()));
            return barrier_outcome::unsupported;
        case utxoz::error_code::sync_failed:
            spdlog::error("[utxoz_database] A durability barrier was attempted "
                "and failed; the data written so far is not known to be durable");
            return barrier_outcome::failed;
        default:
            spdlog::error("[utxoz_database] sync() failed: {}",
                utxoz_error_name(synced.error()));
            return barrier_outcome::failed;
    }
}

result_code utxoz_database::clear() {
    if ( ! is_open()) {
        return result_code::other;
    }

    // UTXO-Z doesn't have a clear method, so we need to close and reopen with remove_existing
    // Get the path first (we need to store it before closing)
    spdlog::warn("[utxoz_database] Clear not directly supported - requires reopen with remove_existing");
    return result_code::other;
}

utxoz::deletion_progress
utxoz_database::apply_deletes(std::span<utxoz::deferred_deletion_entry const> requests) {
    if ( ! is_open()) {
        // A refusal is still an answer about the same keys, so the batch comes
        // back in `unresolved` rather than as an empty progress that would read
        // as "there was nothing to do".
        //
        // DEDUPLICATED, exactly as the library does it: the partition is over
        // DISTINCT keys, keeping the FIRST occurrence of each — its height
        // included, so a caller that sent one outpoint at two heights gets the
        // earlier one back. Copying the span verbatim would hand back more
        // entries than there are keys and break the invariant every caller
        // matches results against, on the one path that never reaches the
        // library to have it applied for us.
        utxoz::deletion_progress closed;
        closed.unresolved.reserve(requests.size());

        boost::unordered_flat_set<utxoz::raw_outpoint> seen;
        seen.reserve(requests.size());
        for (auto const& request : requests) {
            if (seen.insert(request.key).second) {
                closed.unresolved.push_back(request);
            }
        }

        closed.error = utxoz::error_code::closed;
        return closed;
    }

    auto progress = db_->apply_deletes(requests);

    if (progress.error) {
        // NOT flattened into a failure. A deletion writes as it walks, so this
        // call has already applied whatever is in `erased`, and that part is a
        // fact the caller needs in order not to resend it.
        spdlog::error("[utxoz_database] applying {} deletion(s) stopped: {}; {} applied, "
            "{} proven absent, {} still owed", requests.size(),
            utxoz_error_name(*progress.error), progress.erased.size(),
            progress.absent.size(), progress.unresolved.size());
    }

    return progress;
}

void utxoz_database::print_statistics() {
    if (is_open()) {
        db_->print_statistics();
    }
}

void utxoz_database::print_sizing_report() const {
    if (is_open()) {
        db_->print_sizing_report();
    }
}

void utxoz_database::print_height_range_stats() const {
    if (is_open()) {
        db_->print_height_range_stats();
    }
}

#ifdef KTH_UTXOZ_REFERENCE_MODE
std::expected<utxo_entry, result_code> utxoz_database::resolve_reference_ref(
    utxoz::reference_find_result const& ref,
    uint32_t output_index) const
{
    if ( ! block_store_ || ! header_index_) {
        spdlog::error("[utxoz_database] resolve_reference_ref: block_store or header_index not set");
        return std::unexpected(result_code::other);
    }

    // Read raw transaction from flat file using typed fields from reference_find_result
    flat_file_pos pos{static_cast<int32_t>(ref.file_number), ref.offset};
    auto raw_tx = block_store_->read_tx_raw(pos);
    if ( ! raw_tx) {
        spdlog::error("[utxoz_database] resolve_reference_ref: failed to read tx at file={} offset={}",
            ref.file_number, ref.offset);
        return std::unexpected(result_code::other);
    }

    // Parse the transaction
    auto tx = kth::from_data_chunk<domain::chain::transaction>(*raw_tx, true);  // wire=true
    if ( ! tx) {
        spdlog::error("[utxoz_database] resolve_reference_ref: failed to parse tx at file={} offset={}",
            ref.file_number, ref.offset);
        return std::unexpected(result_code::other);
    }

    if (output_index >= tx->outputs().size()) {
        spdlog::error("[utxoz_database] resolve_reference_ref: output_index {} >= tx outputs size {}",
            output_index, tx->outputs().size());
        return std::unexpected(result_code::other);
    }

    // Determine coinbase: first input prevout is null (hash=0, index=max)
    bool const is_coinbase = !tx->inputs().empty() &&
        tx->inputs()[0].previous_output().hash() == null_hash &&
        tx->inputs()[0].previous_output().index() == max_uint32;

    // Calculate MTP from header_index
    // MTP at height h = median of timestamps at heights [h-10, h]
    uint32_t mtp = 0;
    auto const block_height = ref.block_height;
    if (block_height >= 11) {
        std::array<uint32_t, 11> timestamps;
        for (uint32_t i = 0; i < 11; ++i) {
            auto const ancestor_idx = static_cast<header_index::index_t>(block_height - 10 + i);
            timestamps[i] = header_index_->get_timestamp(ancestor_idx);
        }
        std::sort(timestamps.begin(), timestamps.end());
        mtp = timestamps[5]; // median
    } else {
        auto const idx = static_cast<header_index::index_t>(block_height);
        mtp = header_index_->get_timestamp(idx);
    }

    return utxo_entry{std::move(tx->outputs()[output_index]), block_height, mtp, is_coinbase};
}
#endif

// =============================================================================
// Private helper functions
// =============================================================================

utxoz::raw_outpoint utxoz_database::point_to_key(domain::chain::point const& point) {
    utxoz::raw_outpoint key{};

    // Copy 32-byte hash
    auto const& hash = point.hash();
    std::memcpy(key.data(), hash.data(), 32);

    // Append 4-byte index (little-endian)
    uint32_t const index = point.index();
    key[32] = static_cast<uint8_t>(index & 0xFF);
    key[33] = static_cast<uint8_t>((index >> 8) & 0xFF);
    key[34] = static_cast<uint8_t>((index >> 16) & 0xFF);
    key[35] = static_cast<uint8_t>((index >> 24) & 0xFF);

    return key;
}

domain::chain::point utxoz_database::key_to_point(utxoz::raw_outpoint const& key) {
    // Extract 32-byte hash
    hash_digest hash;
    std::memcpy(hash.data(), key.data(), 32);

    // Extract 4-byte index (little-endian)
    uint32_t index =
        static_cast<uint32_t>(key[32]) |
        (static_cast<uint32_t>(key[33]) << 8) |
        (static_cast<uint32_t>(key[34]) << 16) |
        (static_cast<uint32_t>(key[35]) << 24);

    return domain::chain::point{hash, index};
}

std::vector<uint8_t> utxoz_database::entry_to_bytes(utxo_entry const& entry) {
    auto data = entry.to_data();
    return std::vector<uint8_t>(data.begin(), data.end());
}

std::expected<utxo_entry, result_code> utxoz_database::bytes_to_entry(std::span<uint8_t const> bytes) {
    data_chunk data(bytes.begin(), bytes.end());

    auto entry = kth::from_data_chunk<utxo_entry>(data);
    if ( ! entry) {
        return std::unexpected(result_code::other);
    }

    return *entry;
}

} // namespace kth::database
