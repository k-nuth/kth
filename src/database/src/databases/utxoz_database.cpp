// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/databases/utxoz_database.hpp>

#include <cstring>
#include <spdlog/spdlog.h>

#include <kth/infrastructure/utility/byte_reader.hpp>

namespace kth::database {

utxoz_database::~utxoz_database() {
    close();
}

bool utxoz_database::open(std::filesystem::path const& path, bool remove_existing) {
    if (is_open_) {
        spdlog::warn("[utxoz_database] Database already open");
        return true;
    }

    try {
        db_ = std::make_unique<utxoz::db>();
        db_->configure(path.string(), remove_existing);
        is_open_ = true;
        spdlog::info("[utxoz_database] Opened database at {}", path.string());
        return true;
    } catch (std::exception const& e) {
        spdlog::error("[utxoz_database] Failed to open database: {}", e.what());
        db_.reset();
        return false;
    }
}

void utxoz_database::close() {
    if (db_ && is_open_) {
        db_->close();
        db_.reset();
        is_open_ = false;
        spdlog::info("[utxoz_database] Database closed");
    }
}

bool utxoz_database::is_open() const {
    return is_open_ && db_;
}

size_t utxoz_database::size() const {
    if ( ! is_open()) {
        return 0;
    }
    return db_->size();
}

result_code utxoz_database::insert(domain::chain::point const& point, utxo_entry const& entry) {
    if ( ! is_open()) {
        return result_code::other;
    }

    auto key = point_to_key(point);
    auto value = entry_to_bytes(entry);

    if (db_->insert(key, value, entry.height())) {
        return result_code::success;
    }
    return result_code::duplicated_key;
}

std::expected<utxo_entry, result_code> utxoz_database::find(domain::chain::point const& point, uint32_t height) const {
    if ( ! is_open()) {
        return std::unexpected(result_code::other);
    }

    auto key = point_to_key(point);
    auto result = db_->find(key, height);

    if ( ! result) {
        return std::unexpected(result_code::key_not_found);
    }

    return bytes_to_entry(*result);
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

std::pair<size_t, std::vector<utxoz::deferred_deletion_entry>> utxoz_database::process_pending_deletions() {
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
