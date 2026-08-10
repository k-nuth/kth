// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_INTERNAL_DATABASE_IPP_
#define KTH_DATABASE_INTERNAL_DATABASE_IPP_

// #include <kth/infrastructure.hpp>
#include <kth/infrastructure/log/source.hpp>
#include <spdlog/spdlog.h>

namespace kth::database {

template <typename Clock>
internal_database_basis<Clock>::internal_database_basis(path const& db_dir, db_mode_type mode, uint32_t reorg_pool_limit, uint64_t db_max_size, bool safe_mode)
    : db_dir_(db_dir)
    , db_mode_(mode)
    , db_max_size_(db_max_size)
    , safe_mode_(safe_mode)
{
    // reorg_pool_limit is still parsed (settings.reorg_pool_limit / --db.reorg_pool_limit)
    // and threaded through by callers, but the LMDB reorg pool it bounded was removed,
    // so nothing consumes it here.
    (void)reorg_pool_limit;
}

template <typename Clock>
internal_database_basis<Clock>::~internal_database_basis() {
    close();
}

#if ! defined(KTH_DB_READONLY)

template <typename Clock>
bool internal_database_basis<Clock>::create() {
    std::error_code ec;

    if ( ! std::filesystem::create_directories(db_dir_, ec)) {
        if (ec.value() == directory_exists) {
            spdlog::error("[database] Failed because the directory {} already exists.", db_dir_.string());
            return false;
        }

        spdlog::error("[database] Failed to create directory {} with error, '{}'.", db_dir_.string(), ec.message());
        return false;
    }

    auto ret = open_internal();
    if ( ! ret ) {
        return false;
    }

    ret = create_db_mode_property();
    if ( ! ret ) {
        return false;
    }

    ret = create_height_properties();
    if ( ! ret ) {
        return false;
    }

    return true;
}

template <typename Clock>
bool internal_database_basis<Clock>::create_db_mode_property() {
    KTH_DB_txn* db_txn;
    auto res = kth_db_txn_begin(env_, NULL, 0, &db_txn);
    if (res != KTH_DB_SUCCESS) {
        return false;
    }

    property_code property_code_ = property_code::db_mode;
    auto key = kth_db_make_value(sizeof(property_code_), &property_code_);
    auto value = kth_db_make_value(sizeof(db_mode_), &db_mode_);

    res = kth_db_put(db_txn, dbi_properties_, &key, &value, KTH_DB_NOOVERWRITE);
    if (res != KTH_DB_SUCCESS) {
        spdlog::error("[database] Failed saving in DB Properties [create_db_mode_property] {}", int32_t(res));
        kth_db_txn_abort(db_txn);
        return false;
    }

    res = kth_db_txn_commit(db_txn);
    if (res != KTH_DB_SUCCESS) {
        return false;
    }

    return true;
}

template <typename Clock>
bool internal_database_basis<Clock>::create_height_properties() {
    KTH_DB_txn* db_txn;
    auto res = kth_db_txn_begin(env_, NULL, 0, &db_txn);
    if (res != KTH_DB_SUCCESS) {
        return false;
    }

    // Initialize last_header_height to 0
    uint32_t initial_height = 0;
    property_code header_prop = property_code::last_header_height;
    auto header_key = kth_db_make_value(sizeof(header_prop), &header_prop);
    auto header_value = kth_db_make_value(sizeof(initial_height), &initial_height);

    res = kth_db_put(db_txn, dbi_properties_, &header_key, &header_value, KTH_DB_NOOVERWRITE);
    if (res != KTH_DB_SUCCESS) {
        spdlog::error("[database] Failed saving last_header_height in DB Properties [create_height_properties] {}", int32_t(res));
        kth_db_txn_abort(db_txn);
        return false;
    }

    // Initialize last_block_height to 0
    property_code block_prop = property_code::last_block_height;
    auto block_key = kth_db_make_value(sizeof(block_prop), &block_prop);
    auto block_value = kth_db_make_value(sizeof(initial_height), &initial_height);

    res = kth_db_put(db_txn, dbi_properties_, &block_key, &block_value, KTH_DB_NOOVERWRITE);
    if (res != KTH_DB_SUCCESS) {
        spdlog::error("[database] Failed saving last_block_height in DB Properties [create_height_properties] {}", int32_t(res));
        kth_db_txn_abort(db_txn);
        return false;
    }

    res = kth_db_txn_commit(db_txn);
    if (res != KTH_DB_SUCCESS) {
        return false;
    }

    return true;
}

#endif // ! defined(KTH_DB_READONLY)

// =============================================================================
// Height Properties - Get/Set
// =============================================================================

template <typename Clock>
std::expected<uint32_t, result_code> internal_database_basis<Clock>::get_property_height(property_code prop, KTH_DB_txn* db_txn) const {
    auto key = kth_db_make_value(sizeof(prop), &prop);

    KTH_DB_val value;
    auto res = kth_db_get(db_txn, dbi_properties_, &key, &value);
    if (res == KTH_DB_NOTFOUND) {
        return std::unexpected(result_code::key_not_found);
    }
    if (res != KTH_DB_SUCCESS) {
        return std::unexpected(result_code::other);
    }

    return *static_cast<uint32_t*>(kth_db_get_data(value));
}

template <typename Clock>
std::expected<data_chunk, result_code>
internal_database_basis<Clock>::get_property_blob(property_code prop, KTH_DB_txn* db_txn) const {
    auto key = kth_db_make_value(sizeof(prop), &prop);

    KTH_DB_val value;
    auto res = kth_db_get(db_txn, dbi_properties_, &key, &value);
    if (res == KTH_DB_NOTFOUND) {
        // The absence of the key IS the clean answer for the one property that
        // uses this, so it must not be folded into the failures below.
        return std::unexpected(result_code::key_not_found);
    }
    if (res != KTH_DB_SUCCESS) {
        return std::unexpected(result_code::other);
    }

    auto const* bytes = static_cast<uint8_t const*>(kth_db_get_data(value));
    auto const size = kth_db_get_size(value);
    return data_chunk(bytes, bytes + size);
}

template <typename Clock>
std::expected<heights_t, result_code> internal_database_basis<Clock>::get_last_heights() const {
    KTH_DB_txn* db_txn;
    auto res = kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn);
    if (res != KTH_DB_SUCCESS) {
        return std::unexpected(result_code::other);
    }

    auto header_result = get_property_height(property_code::last_header_height, db_txn);
    if ( ! header_result) {
        kth_db_txn_commit(db_txn);
        return std::unexpected(header_result.error());
    }

    auto block_result = get_property_height(property_code::last_block_height, db_txn);
    if ( ! block_result) {
        kth_db_txn_commit(db_txn);
        return std::unexpected(block_result.error());
    }

    if (kth_db_txn_commit(db_txn) != KTH_DB_SUCCESS) {
        return std::unexpected(result_code::other);
    }

    return heights_t{*header_result, *block_result};
}

#if ! defined(KTH_DB_READONLY)

template <typename Clock>
result_code internal_database_basis<Clock>::set_property_height(property_code prop, uint32_t height) {
    KTH_DB_txn* db_txn;
    auto res = kth_db_txn_begin(env_, NULL, 0, &db_txn);
    if (res != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    auto result = set_property_height(prop, height, db_txn);
    if (result != result_code::success) {
        kth_db_txn_abort(db_txn);
        return result;
    }

    if (kth_db_txn_commit(db_txn) != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::set_property_height(property_code prop, uint32_t height, KTH_DB_txn* db_txn) {
    auto key = kth_db_make_value(sizeof(prop), &prop);
    auto value = kth_db_make_value(sizeof(height), &height);

    auto res = kth_db_put(db_txn, dbi_properties_, &key, &value, 0);  // 0 = overwrite if exists
    if (res != KTH_DB_SUCCESS) {
        spdlog::error("[database] Failed updating height property in DB Properties [set_property_height] {}", int32_t(res));
        return result_code::other;
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::set_property_blob(property_code prop, byte_span value, KTH_DB_txn* db_txn) {
    auto key = kth_db_make_value(sizeof(prop), &prop);
    auto val = kth_db_make_value(value.size(), const_cast<uint8_t*>(value.data()));

    auto res = kth_db_put(db_txn, dbi_properties_, &key, &val, 0);  // 0 = overwrite if exists
    if (res != KTH_DB_SUCCESS) {
        spdlog::error("[database] Failed writing property {} [set_property_blob] {}",
            uint32_t(prop), int32_t(res));
        return result_code::other;
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::clear_property(property_code prop, KTH_DB_txn* db_txn) {
    auto key = kth_db_make_value(sizeof(prop), &prop);

    auto res = kth_db_del(db_txn, dbi_properties_, &key, nullptr);
    if (res == KTH_DB_NOTFOUND) {
        // Already absent, which is the state this was asked to reach.
        return result_code::success;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::error("[database] Failed clearing property {} [clear_property] {}",
            uint32_t(prop), int32_t(res));
        return result_code::other;
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::set_last_header_height(uint32_t height) {
    return set_property_height(property_code::last_header_height, height);
}

template <typename Clock>
result_code internal_database_basis<Clock>::set_last_block_height(uint32_t height) {
    return set_property_height(property_code::last_block_height, height);
}

#endif // ! defined(KTH_DB_READONLY)

template <typename Clock>
bool internal_database_basis<Clock>::open() {

    auto ret = open_internal();
    if ( ! ret ) {
        return false;
    }

    ret = verify_db_mode_property();
    if ( ! ret ) {
        return false;
    }

    return true;
}

template <typename Clock>
bool internal_database_basis<Clock>::open_internal() {

    if ( ! create_and_open_environment()) {
        spdlog::error("[database] Error configuring LMDB environment.");
        return false;
    }

    return open_databases();
}

template <typename Clock>
bool internal_database_basis<Clock>::verify_db_mode_property() const {

    KTH_DB_txn* db_txn;
    auto res = kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn);
    if (res != KTH_DB_SUCCESS) {
        return false;
    }

    property_code property_code_ = property_code::db_mode;

    auto key = kth_db_make_value(sizeof(property_code_), &property_code_);
    KTH_DB_val value;

    res = kth_db_get(db_txn, dbi_properties_, &key, &value);
    if (res != KTH_DB_SUCCESS) {
        spdlog::error("[database] Failed getting DB Properties [verify_db_mode_property] {}", int32_t(res));
        kth_db_txn_abort(db_txn);
        return false;
    }

    auto const db_mode_db = *static_cast<db_mode_type*>(kth_db_get_data(value));

    res = kth_db_txn_commit(db_txn);
    if (res != KTH_DB_SUCCESS) {
        return false;
    }

    if (db_mode_ != db_mode_db) {
        spdlog::error("[database] Error validating DB Mode, the node is compiled for another DB mode. Node DB Mode: {}, Actual DB Mode: {}", uint32_t(db_mode_), uint32_t(db_mode_db));
        return false;
    }

    return true;
}

template <typename Clock>
bool internal_database_basis<Clock>::close() {
    if (db_opened_) {
        // Force synchronous flush: the environment is opened MDB_NOSYNC, so
        // nothing committed during the run has necessarily reached the disk.
        //
        // Reported rather than discarded. Closing cannot be refused — the
        // caller is on its way out and there is nothing further to protect —
        // but a barrier that failed here means transactions this run reported
        // as committed were lost, and a shutdown that says nothing about that
        // leaves the next start to discover it as corruption with no
        // explanation.
        if (kth_db_env_sync(env_, true) != KTH_DB_SUCCESS) {
            spdlog::critical("[database] The final sync failed: transactions this run "
                "reported as committed may not be on disk");
        }

        // Close all DBIs before closing the environment
        kth_db_dbi_close(env_, dbi_block_header_);
        kth_db_dbi_close(env_, dbi_block_header_by_hash_);
        kth_db_dbi_close(env_, dbi_properties_);

        db_opened_ = false;
    }

    if (env_created_) {
        kth_db_env_close(env_);
        env_created_ = false;
    }

    return true;
}

#if ! defined(KTH_DB_READONLY)

template <typename Clock>
std::expected<uint32_t, result_code> internal_database_basis<Clock>::get_utxo_built_height() const {
    // A read needs a live transaction: get_property_height forwards it straight to
    // kth_db_get, and a null txn makes every lookup fail (key_not_found), so the
    // persisted height was never actually read back and resume always fell through
    // to the block-sync marker. Open a read-only transaction like get_last_heights.
    KTH_DB_txn* db_txn;
    if (kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn) != KTH_DB_SUCCESS) {
        return std::unexpected(result_code::other);
    }

    auto result = get_property_height(property_code::utxo_built_height, db_txn);
    kth_db_txn_commit(db_txn);
    return result;
}

template <typename Clock>
result_code internal_database_basis<Clock>::set_utxo_built_height(uint32_t height) {
    return set_property_height(property_code::utxo_built_height, height);
}

template <typename Clock>
result_code internal_database_basis<Clock>::begin_transition_record(utxo_transition_record const& record) {
    auto const bytes = encode(record);

    KTH_DB_txn* db_txn;
    if (kth_db_txn_begin(env_, NULL, 0, &db_txn) != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    auto const written = set_property_blob(property_code::utxo_transition,
        byte_span{bytes.data(), bytes.size()}, db_txn);
    if (written != result_code::success) {
        kth_db_txn_abort(db_txn);
        return written;
    }

    if (kth_db_txn_commit(db_txn) != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::write_heights(transition_heights const& heights, KTH_DB_txn* db_txn) {
    if (heights.last_block_height) {
        auto const res = set_property_height(property_code::last_block_height,
            *heights.last_block_height, db_txn);
        if (res != result_code::success) {
            return res;
        }
    }

    if (heights.utxo_built_height) {
        auto const res = set_property_height(property_code::utxo_built_height,
            *heights.utxo_built_height, db_txn);
        if (res != result_code::success) {
            return res;
        }
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::set_heights(transition_heights const& heights) {
    KTH_DB_txn* db_txn;
    if (kth_db_txn_begin(env_, NULL, 0, &db_txn) != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    auto const res = write_heights(heights, db_txn);
    if (res != result_code::success) {
        kth_db_txn_abort(db_txn);
        return res;
    }

    if (kth_db_txn_commit(db_txn) != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::publish_transition(transition_heights const& heights) {
    // write_heights treats two absent members as a no-op, and the clear below
    // would then run on its own — which is a transition declared finished
    // without publishing where it finished. That is the one thing this
    // transaction exists to make impossible, so it is refused rather than
    // committed: a caller with nothing to publish has no transition to close.
    if ( ! heights.last_block_height && ! heights.utxo_built_height) {
        return result_code::other;
    }

    KTH_DB_txn* db_txn;
    if (kth_db_txn_begin(env_, NULL, 0, &db_txn) != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    auto const res = write_heights(heights, db_txn);
    if (res != result_code::success) {
        kth_db_txn_abort(db_txn);
        return res;
    }

    // Last inside the transaction, and inseparable from the heights above it.
    // Two transactions would leave the instant this exists to remove: one where
    // the height says "arrived" and the record says "clean" independently.
    auto const cleared = clear_property(property_code::utxo_transition, db_txn);
    if (cleared != result_code::success) {
        kth_db_txn_abort(db_txn);
        return cleared;
    }

    if (kth_db_txn_commit(db_txn) != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::env_sync() {
    if ( ! db_opened_) {
        // Nothing is mapped, so reporting success would suggest a guarantee
        // about a database this object has let go of.
        return result_code::other;
    }
    if (kth_db_env_sync(env_, true) != KTH_DB_SUCCESS) {
        return result_code::other;
    }
    return result_code::success;
}

#endif // ! defined(KTH_DB_READONLY)

template <typename Clock>
transition_check internal_database_basis<Clock>::read_transition_record() const {
    KTH_DB_txn* db_txn;
    if (kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn) != KTH_DB_SUCCESS) {
        return {transition_status::unreadable, std::nullopt, std::nullopt};
    }

    auto const stored = get_property_blob(property_code::utxo_transition, db_txn);
    kth_db_txn_commit(db_txn);

    if ( ! stored) {
        // The one place "there is nothing here" and "I could not look" must not
        // meet. key_not_found is the clean answer; anything else is not an
        // answer at all.
        return stored.error() == result_code::key_not_found
             ? transition_check{transition_status::clean, std::nullopt, std::nullopt}
             : transition_check{transition_status::unreadable, std::nullopt, std::nullopt};
    }

    auto const decoded = decode_transition_record(*stored);
    if ( ! decoded) {
        return {transition_status::corrupt, std::nullopt, decoded.error()};
    }

    // A record that decodes says a transition started and was never recorded as
    // finished. `in_progress` is the only state the format defines, so there is
    // no second branch to take here yet — the field exists so adding one later
    // does not need a format bump.
    return {transition_status::recovery_required, *decoded, std::nullopt};
}

template <typename Clock>
std::expected<std::pair<domain::chain::header, uint32_t>, result_code> internal_database_basis<Clock>::get_header(hash_digest const& hash) const {
    auto key  = kth_db_make_value(hash.size(), const_cast<hash_digest&>(hash).data());

    KTH_DB_txn* db_txn;
    auto res = kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn);
    if (res != KTH_DB_SUCCESS) {
        return std::unexpected(result_code::other);
    }

    KTH_DB_val value;
    auto res0 = kth_db_get(db_txn, dbi_block_header_by_hash_, &key, &value);
    if (res0 == KTH_DB_NOTFOUND) {
        kth_db_txn_commit(db_txn);
        return std::unexpected(result_code::key_not_found);
    }
    if (res0 != KTH_DB_SUCCESS) {
        kth_db_txn_commit(db_txn);
        return std::unexpected(result_code::other);
    }

    // assert kth_db_get_size(value) == 4;
    auto height = *static_cast<uint32_t*>(kth_db_get_data(value));

    auto header_result = get_header(height, db_txn);

    if (kth_db_txn_commit(db_txn) != KTH_DB_SUCCESS) {
        return std::unexpected(result_code::other);
    }

    if ( ! header_result) {
        return std::unexpected(header_result.error());
    }

    return std::make_pair(*header_result, height);
}

template <typename Clock>
std::expected<domain::chain::header, result_code> internal_database_basis<Clock>::get_header(uint32_t height) const {
    KTH_DB_txn* db_txn;
    auto ret1 = kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn);
    if (ret1 != KTH_DB_SUCCESS) {
        return std::unexpected(result_code::other);
    }

    auto result = get_header(height, db_txn);

    if (kth_db_txn_commit(db_txn) != KTH_DB_SUCCESS) {
        return std::unexpected(result_code::other);
    }

    return result;
}

template <typename Clock>
std::expected<header_with_abla_state_t, result_code> internal_database_basis<Clock>::get_header_and_abla_state(uint32_t height) const {
    KTH_DB_txn* db_txn;
    auto zzz = kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn);
    if (zzz != KTH_DB_SUCCESS) {
        return std::unexpected(result_code::other);
    }

    auto result = get_header_and_abla_state(height, db_txn);

    if (kth_db_txn_commit(db_txn) != KTH_DB_SUCCESS) {
        return std::unexpected(result_code::other);
    }

    return result;
}

template <typename Clock>
std::expected<domain::chain::header::list, result_code> internal_database_basis<Clock>::get_headers(uint32_t from, uint32_t to) const {
    // precondition: from <= to
    domain::chain::header::list list;

    KTH_DB_txn* db_txn;
    auto zzz = kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn);
    if (zzz != KTH_DB_SUCCESS) {
        return std::unexpected(result_code::other);
    }

    KTH_DB_cursor* cursor;
    if (kth_db_cursor_open(db_txn, dbi_block_header_, &cursor) != KTH_DB_SUCCESS) {
        kth_db_txn_commit(db_txn);
        return std::unexpected(result_code::other);
    }

    auto key = kth_db_make_value(sizeof(from), &from);

    KTH_DB_val value;
    int rc = kth_db_cursor_get(cursor, &key, &value, KTH_DB_SET);
    if (rc == KTH_DB_NOTFOUND) {
        kth_db_cursor_close(cursor);
        kth_db_txn_commit(db_txn);
        return std::unexpected(result_code::key_not_found);
    }
    if (rc != KTH_DB_SUCCESS) {
        kth_db_cursor_close(cursor);
        kth_db_txn_commit(db_txn);
        return std::unexpected(result_code::other);
    }

    auto res1 = kth::database::from_db_value<domain::chain::header>(value);
    if (res1) {
        list.push_back(*res1);
    }

    while ((rc = kth_db_cursor_get(cursor, &key, &value, KTH_DB_NEXT)) == KTH_DB_SUCCESS) {
        auto height = *static_cast<uint32_t*>(kth_db_get_data(key));
        if (height > to) break;
        auto res2 = kth::database::from_db_value<domain::chain::header>(value);
        if (res2) {
            list.push_back(*res2);
        }
    }

    kth_db_cursor_close(cursor);
    kth_db_txn_commit(db_txn);
    return list;
}

// Private functions
// ------------------------------------------------------------------------------------------------------

template <typename Clock>
size_t internal_database_basis<Clock>::get_db_page_size() const {
    return boost::interprocess::mapped_region::get_page_size();
}

template <typename Clock>
size_t internal_database_basis<Clock>::adjust_db_size(size_t size) const {
    // precondition: env_ have to be created (kth_db_env_create)
    // The kth_db_env_set_mapsize should be a multiple of the OS page size.
    size_t const page_size = get_db_page_size();
    auto res = size_t(std::ceil(double(size) / page_size)) * page_size;
    return res;

    // size_t const mod = size % page_size;
    // return size + (mod != 0) ? (page_size - mod) : 0;
}

template <typename Clock>
bool internal_database_basis<Clock>::create_and_open_environment() {
    spdlog::debug("[internal_database] create_and_open_environment() - starting");
    spdlog::default_logger()->flush();

    if (kth_db_env_create(&env_) != KTH_DB_SUCCESS) {
        return false;
    }
    env_created_ = true;
    spdlog::debug("[internal_database] create_and_open_environment() - env created, env_={}", (void*)env_);
    spdlog::default_logger()->flush();

    // TODO(fernando): see what to do with mdb_env_set_maxreaders ----------------------------------------------
    // int threads = tools::get_max_concurrency();
    // if (threads > 110 &&	/* maxreaders default is 126, leave some slots for other read processes */
    //     (result = mdb_env_set_maxreaders(m_env, threads+16)))
    //     throw0(DB_ERROR(lmdb_error("Failed to set max number of readers: ", result).c_str()));
    // ----------------------------------------------------------------------------------------------------------------

    auto const adjusted_size = adjust_db_size(db_max_size_);
    auto res = kth_db_env_set_mapsize(env_, adjusted_size);
    if (res != KTH_DB_SUCCESS) {
        spdlog::error("[database] Error setting max memory map size. Verify do you have enough free space. [create_and_open_environment] {}", int32_t(res));
        return false;
    }

    size_t max_dbs;
    if (db_mode_ == db_mode_type::full) {
        max_dbs = max_dbs_full_;
    } else if (db_mode_ == db_mode_type::blocks) {
        max_dbs = max_dbs_blocks_;
    } else if (db_mode_ == db_mode_type::pruned) {
        max_dbs = max_dbs_pruned_;
    }

    res = kth_db_env_set_maxdbs(env_, max_dbs);
    if (res != KTH_DB_SUCCESS) {
        return false;
    }

    //Note(knuth): fastest flags
    //KTH_DB_NORDAHEAD | KTH_DB_NOSYNC  | KTH_DB_NOTLS | KTH_DB_WRITEMAP | KTH_DB_MAPASYNC
    //for more secure flags use: KTH_DB_NORDAHEAD | KTH_DB_NOSYNC  | KTH_DB_NOTLS

    int mdb_flags = KTH_DB_NORDAHEAD | KTH_DB_NOSYNC | KTH_DB_NOTLS;

#if defined(KTH_DB_READONLY)
    mdb_flags |= KTH_DB_RDONLY;
#endif

    if ( ! safe_mode_) {
        mdb_flags |= KTH_DB_WRITEMAP | KTH_DB_MAPASYNC;
    }

    res = kth_db_env_open(env_, db_dir_.string().c_str(), mdb_flags, env_open_mode_);
    if (res != KTH_DB_SUCCESS) {
        spdlog::error("[database] Error opening LMDB environment: {}", res);
        return false;
    }

    // Log LMDB storage status
    {
        MDB_envinfo mei;
        MDB_stat mst;
        mdb_env_info(env_, &mei);
        mdb_env_stat(env_, &mst);

        auto const map_gib = double(mei.me_mapsize) / (1024.0 * 1024.0 * 1024.0);
        auto const used_bytes = mst.ms_psize * mei.me_last_pgno;
        auto const used_gib = double(used_bytes) / (1024.0 * 1024.0 * 1024.0);
        auto const pct_used = (double(used_bytes) / double(mei.me_mapsize)) * 100.0;

        std::error_code ec;
        auto const space_info = std::filesystem::space(db_dir_, ec);
        auto const disk_gib = !ec ? double(space_info.available) / (1024.0 * 1024.0 * 1024.0) : 0.0;

        if (pct_used >= 90.0) {
            spdlog::warn("[database] Storage: {:.1f}/{:.1f} GiB ({:.0f}%) | Disk: {:.1f} GiB free - NEAR CAPACITY",
                used_gib, map_gib, pct_used, disk_gib);
        } else {
            spdlog::info("[database] Storage: {:.1f}/{:.1f} GiB ({:.0f}%) | Disk: {:.1f} GiB free",
                used_gib, map_gib, pct_used, disk_gib);
        }
    }

    return true;
}

/*
template <typename Clock>
bool internal_database_basis<Clock>::set_fast_flags_environment(bool enabled) {

    if (fast_mode && enabled) {
        return true;
    }

    if ( ! fast_mode && !enabled) {
        return true;
    }

    spdlog::info("[database] Setting LMDB Environment Flags. Fast mode: {}", (enabled ? "yes" : "no" ));

    //KTH_DB_WRITEMAP |
    auto res = mdb_env_set_flags(env_, KTH_DB_MAPASYNC, enabled ? 1 : 0);
    if ( res != KTH_DB_SUCCESS ) {
        spdlog::error("[database] Error setting LMDB Environmet flags. [set_fast_flags_environment] {}", int32_t(res));
        return false;
    }

    fast_mode = enabled;
    return true;
}
*/

inline
int compare_uint64(KTH_DB_val const* a, KTH_DB_val const* b) {

    //TODO(fernando): check this casts... something smells bad
    uint64_t const va = *(uint64_t const *)kth_db_get_data(*a);
    uint64_t const vb = *(uint64_t const *)kth_db_get_data(*b);

    //std::println("va: {}", va);
    //std::println("vb: {}", va);

    return (va < vb) ? -1 : va > vb;
}

template <typename Clock>
bool internal_database_basis<Clock>::open_databases() {
    KTH_DB_txn* db_txn;

    spdlog::debug("[internal_database] open_databases() - starting, thread_id={}", std::hash<std::thread::id>{}(std::this_thread::get_id()));
    spdlog::default_logger()->flush();

    auto res = kth_db_txn_begin(env_, NULL, KTH_DB_CONDITIONAL_READONLY, &db_txn);
    if (res != KTH_DB_SUCCESS) {
        spdlog::error("[internal_database] open_databases() - failed to begin transaction: {}", res);
        return false;
    }

    auto open_db = [&](auto const& db_name, uint32_t flags, KTH_DB_dbi* dbi){
        auto result = kth_db_dbi_open(db_txn, db_name, flags, dbi);
        spdlog::debug("[internal_database] open_databases() - opened {}: result={}, dbi={}", db_name, result, *dbi);
        spdlog::default_logger()->flush();
        if (result != KTH_DB_SUCCESS) {
            kth_db_txn_abort(db_txn);
        }
        return result == KTH_DB_SUCCESS;
    };

    if ( ! open_db(block_header_db_name, KTH_DB_CONDITIONAL_CREATE | KTH_DB_INTEGERKEY, &dbi_block_header_)) return false;
    if ( ! open_db(block_header_by_hash_db_name, KTH_DB_CONDITIONAL_CREATE, &dbi_block_header_by_hash_)) return false;
    if ( ! open_db(db_properties_name, KTH_DB_CONDITIONAL_CREATE | KTH_DB_INTEGERKEY, &dbi_properties_)) return false;

    auto commit_res = kth_db_txn_commit(db_txn);
    db_opened_ = commit_res == KTH_DB_SUCCESS;
    spdlog::debug("[internal_database] open_databases() - commit result={}, db_opened_={}", commit_res, db_opened_);
    spdlog::default_logger()->flush();
    return db_opened_;
}

} // namespace kth::database

#endif // KTH_DATABASE_INTERNAL_DATABASE_IPP_

