// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/databases/utxoz_database.hpp>

#include <cstring>
#include <spdlog/spdlog.h>

#include <kth/infrastructure/utility/byte_reader.hpp>

#ifdef KTH_UTXOZ_COMPACT_MODE
#include <kth/database/block_store.hpp>
#include <kth/database/header_index.hpp>
#endif

namespace kth::database {

utxoz_database::~utxoz_database() {
    close();
}

bool utxoz_database::open(std::filesystem::path const& path, bool remove_existing) {
    if (is_open_) {
        spdlog::warn("[utxoz_database] Database already open");
        return true;
    }

#ifdef KTH_UTXOZ_COMPACT_MODE
    auto result = utxoz::compact_db::open(path.string(), remove_existing);
#else
    auto result = utxoz::full_db::open(path.string(), remove_existing);
#endif
    if ( ! result) {
        spdlog::error("[utxoz_database] Failed to open database: {}", static_cast<int>(result.error()));
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

#ifndef KTH_UTXOZ_COMPACT_MODE
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

#ifdef KTH_UTXOZ_COMPACT_MODE
    auto result = db_->find(key, height);
    if ( ! result) {
        return std::unexpected(result_code::key_not_found);
    }
    return resolve_compact_ref(*result, point.index());
#else
    auto result = db_->find(key, height);
    if ( ! result) {
        return std::unexpected(result_code::key_not_found);
    }
    return bytes_to_entry(result->data);
#endif
}

result_code utxoz_database::erase(domain::chain::point const& point, uint32_t height) {
    if ( ! is_open()) {
        return result_code::other;
    }

    auto key = point_to_key(point);
    auto erased = db_->erase(key, height);

    return erased > 0 ? result_code::success : result_code::key_not_found;
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

size_t utxoz_database::deferred_deletions_size() const {
    if ( ! is_open()) {
        return 0;
    }
    return db_->deferred_deletions_size();
}

std::pair<uint32_t, std::vector<utxoz::deferred_deletion_entry>> utxoz_database::process_pending_deletions() {
    if ( ! is_open()) {
        return {0, {}};
    }
    return db_->process_pending_deletions();
}

void utxoz_database::compact() {
    if (is_open()) {
        db_->compact_all();
    }
}

void utxoz_database::print_statistics() {
    if (is_open()) {
        db_->print_statistics();
    }
}

void utxoz_database::print_sizing_report() {
    if (is_open()) {
        db_->print_sizing_report();
    }
}

void utxoz_database::print_height_range_stats() {
    if (is_open()) {
        db_->print_height_range_stats();
    }
}

#ifdef KTH_UTXOZ_COMPACT_MODE
std::expected<utxo_entry, result_code> utxoz_database::resolve_compact_ref(
    utxoz::compact_find_result const& ref,
    uint32_t output_index) const
{
    if ( ! block_store_ || ! header_index_) {
        spdlog::error("[utxoz_database] resolve_compact_ref: block_store or header_index not set");
        return std::unexpected(result_code::other);
    }

    // Read raw transaction from flat file using typed fields from compact_find_result
    flat_file_pos pos{static_cast<int32_t>(ref.file_number), ref.offset};
    auto raw_tx = block_store_->read_tx_raw(pos);
    if ( ! raw_tx) {
        spdlog::error("[utxoz_database] resolve_compact_ref: failed to read tx at file={} offset={}",
            ref.file_number, ref.offset);
        return std::unexpected(result_code::other);
    }

    // Parse the transaction
    byte_reader reader(*raw_tx);
    auto tx = domain::chain::transaction::from_data(reader, true);  // wire=true
    if ( ! tx) {
        spdlog::error("[utxoz_database] resolve_compact_ref: failed to parse tx at file={} offset={}",
            ref.file_number, ref.offset);
        return std::unexpected(result_code::other);
    }

    if (output_index >= tx->outputs().size()) {
        spdlog::error("[utxoz_database] resolve_compact_ref: output_index {} >= tx outputs size {}",
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
    byte_reader reader(data);

    auto entry = utxo_entry::from_data(reader);
    if ( ! entry) {
        return std::unexpected(result_code::other);
    }

    return *entry;
}

} // namespace kth::database
