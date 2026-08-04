// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_HEADER_DATABASE_IPP_
#define KTH_DATABASE_HEADER_DATABASE_IPP_

#include <kth/infrastructure/log/source.hpp>
#include <kth/database/databases/header_abla_entry.hpp>

namespace kth::database {

#if ! defined(KTH_DB_READONLY)

template <typename Clock>
result_code internal_database_basis<Clock>::push_block_header(domain::chain::block const& block, domain::chain::abla::state const& abla_state, uint32_t height, KTH_DB_txn* db_txn) {

    // The block's ABLA/chain state is no longer carried on the block value; the
    // caller (which has the block's chain_state during organize) supplies it.
    auto valuearr = to_data_with_abla_state(block, abla_state);             //TODO(fernando): podría estar afuera de la DBTx
    auto key = kth_db_make_value(sizeof(height), &height);
    auto value = kth_db_make_value(valuearr.size(), valuearr.data());

    auto res = kth_db_put(db_txn, dbi_block_header_, &key, &value, KTH_DB_APPEND);
    if (res == KTH_DB_KEYEXIST) {
        //TODO(fernando): El logging en general no está bueno que esté en la DbTx.
        spdlog::info("[database] Duplicate key inserting block header [push_block_header] {}", res);        //TODO(fernando): podría estar afuera de la DBTx.
        return result_code::duplicated_key;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error inserting block header  [push_block_header] {}", res);
        return result_code::other;
    }

    auto key_by_hash_arr = block.hash();                                    //TODO(fernando): podría estar afuera de la DBTx
    auto key_by_hash = kth_db_make_value(key_by_hash_arr.size(), key_by_hash_arr.data());

    res = kth_db_put(db_txn, dbi_block_header_by_hash_, &key_by_hash, &key, KTH_DB_NOOVERWRITE);
    if (res == KTH_DB_KEYEXIST) {
        spdlog::info("[database] Duplicate key inserting block header by hash [push_block_header] {}", res);
        return result_code::duplicated_key;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error inserting block header by hash [push_block_header] {}", res);
        return result_code::other;
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::push_header_only(domain::chain::header const& header, uint32_t height, KTH_DB_txn* db_txn) {

    auto valuearr = to_data_header_only(header);
    auto key = kth_db_make_value(sizeof(height), &height);
    auto value = kth_db_make_value(valuearr.size(), valuearr.data());

    // APPEND is the sequential-sync fast path: it assumes the new key is above
    // every existing one, which holds while the chain only grows.
    auto res = kth_db_put(db_txn, dbi_block_header_, &key, &value, KTH_DB_APPEND);
    if (res == KTH_DB_KEYEXIST) {
        // The height is already occupied, which means a reorganization put a
        // different block there. Rewrite it: leaving the abandoned header would
        // make every reader that addresses blocks by height — median time past,
        // staleness, the RPC surface — answer from the branch the node left.
        //
        // Drop the displaced header's hash -> height entry first. This table
        // describes the chain by height, so a hash that is no longer at any
        // height does not belong in it: left behind, a lookup for the abandoned
        // block would resolve to its old height and return whichever block now
        // occupies it — a different header than the one asked for. Side-branch
        // headers remain addressable through the header index.
        if (auto const displaced = get_header(height, db_txn); displaced) {
            auto displaced_hash = kth::domain::chain::hash(*displaced);
            auto displaced_key = kth_db_make_value(displaced_hash.size(), displaced_hash.data());
            auto const del = kth_db_del(db_txn, dbi_block_header_by_hash_, &displaced_key, NULL);
            if (del != KTH_DB_SUCCESS && del != KTH_DB_NOTFOUND) {
                spdlog::info("[database] Error dropping the displaced header by hash "
                    "[push_header_only] {}", del);
                return result_code::other;
            }
        }

        res = kth_db_put(db_txn, dbi_block_header_, &key, &value, 0);
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error inserting header [push_header_only] {}", res);
        return result_code::other;
    }

    auto key_by_hash_arr = kth::domain::chain::hash(header);
    auto key_by_hash = kth_db_make_value(key_by_hash_arr.size(), key_by_hash_arr.data());

    res = kth_db_put(db_txn, dbi_block_header_by_hash_, &key_by_hash, &key, KTH_DB_NOOVERWRITE);
    if (res == KTH_DB_KEYEXIST) {
        // Already indexed, and a block's height never changes, so re-persisting
        // the same header is a no-op rather than a conflict.
        return result_code::success;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error inserting header by hash [push_header_only] {}", res);
        return result_code::other;
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::push_header(domain::chain::header const& header, uint32_t height) {
    KTH_DB_txn* db_txn;
    auto res = kth_db_txn_begin(env_, NULL, 0, &db_txn);
    if (res != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    auto result = push_header_only(header, height, db_txn);
    if (result != result_code::success) {
        kth_db_txn_abort(db_txn);
        return result;
    }

    // Update last_header_height property
    result = set_property_height(property_code::last_header_height, height, db_txn);
    if (result != result_code::success) {
        kth_db_txn_abort(db_txn);
        return result;
    }

    res = kth_db_txn_commit(db_txn);
    if (res != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::push_header_with_abla(domain::chain::header const& header, uint32_t height, uint64_t block_size, uint64_t control_block_size, uint64_t elastic_buffer_size, KTH_DB_txn* db_txn) {

    auto valuearr = to_data_header_with_abla_state(header, block_size, control_block_size, elastic_buffer_size);
    auto key = kth_db_make_value(sizeof(height), &height);
    auto value = kth_db_make_value(valuearr.size(), valuearr.data());

    auto res = kth_db_put(db_txn, dbi_block_header_, &key, &value, KTH_DB_APPEND);
    if (res == KTH_DB_KEYEXIST) {
        spdlog::info("[database] Duplicate key inserting header [push_header_with_abla] {}", res);
        return result_code::duplicated_key;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error inserting header [push_header_with_abla] {}", res);
        return result_code::other;
    }

    auto key_by_hash_arr = kth::domain::chain::hash(header);
    auto key_by_hash = kth_db_make_value(key_by_hash_arr.size(), key_by_hash_arr.data());

    res = kth_db_put(db_txn, dbi_block_header_by_hash_, &key_by_hash, &key, KTH_DB_NOOVERWRITE);
    if (res == KTH_DB_KEYEXIST) {
        spdlog::info("[database] Duplicate key inserting header by hash [push_header_with_abla] {}", res);
        return result_code::duplicated_key;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error inserting header by hash [push_header_with_abla] {}", res);
        return result_code::other;
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::push_header(domain::chain::header const& header, uint32_t height, uint64_t block_size, uint64_t control_block_size, uint64_t elastic_buffer_size) {
    KTH_DB_txn* db_txn;
    auto res = kth_db_txn_begin(env_, NULL, 0, &db_txn);
    if (res != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    auto result = push_header_with_abla(header, height, block_size, control_block_size, elastic_buffer_size, db_txn);
    if (result != result_code::success) {
        kth_db_txn_abort(db_txn);
        return result;
    }

    // Update last_header_height property
    result = set_property_height(property_code::last_header_height, height, db_txn);
    if (result != result_code::success) {
        kth_db_txn_abort(db_txn);
        return result;
    }

    res = kth_db_txn_commit(db_txn);
    if (res != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::push_headers_batch(domain::chain::header::list const& headers, uint32_t start_height) {
    if (headers.empty()) {
        return result_code::success;
    }

    KTH_DB_txn* db_txn;
    auto res = kth_db_txn_begin(env_, NULL, 0, &db_txn);
    if (res != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    uint32_t height = start_height;
    for (auto const& header : headers) {
        auto result = push_header_only(header, height, db_txn);
        if (result != result_code::success && result != result_code::duplicated_key) {
            kth_db_txn_abort(db_txn);
            return result;
        }
        ++height;
    }

    // Update last_header_height property to the last height
    auto const final_height = start_height + uint32_t(headers.size()) - 1;
    auto result = set_property_height(property_code::last_header_height, final_height, db_txn);
    if (result != result_code::success) {
        kth_db_txn_abort(db_txn);
        return result;
    }

    res = kth_db_txn_commit(db_txn);
    if (res != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    return result_code::success;
}

#endif // ! defined(KTH_DB_READONLY)

template <typename Clock>
std::expected<domain::chain::header, result_code> internal_database_basis<Clock>::get_header(uint32_t height, KTH_DB_txn* db_txn) const {
    auto key = kth_db_make_value(sizeof(height), &height);
    KTH_DB_val value;

    auto res = kth_db_get(db_txn, dbi_block_header_, &key, &value);
    if (res == KTH_DB_NOTFOUND) {
        return std::unexpected(result_code::key_not_found);
    }
    if (res != KTH_DB_SUCCESS) {
        return std::unexpected(result_code::other);
    }

    auto reader = kth::database::db_reader(value);
    auto opt = get_header_and_abla_state_from_data(reader);
    if ( ! opt) {
        return std::unexpected(result_code::other);
    }
    return std::get<0>(*opt);
}

template <typename Clock>
std::expected<header_with_abla_state_t, result_code> internal_database_basis<Clock>::get_header_and_abla_state(uint32_t height, KTH_DB_txn* db_txn) const {
    auto key = kth_db_make_value(sizeof(height), &height);
    KTH_DB_val value;

    auto res = kth_db_get(db_txn, dbi_block_header_, &key, &value);
    if (res == KTH_DB_NOTFOUND) {
        return std::unexpected(result_code::key_not_found);
    }
    if (res != KTH_DB_SUCCESS) {
        return std::unexpected(result_code::other);
    }

    auto reader = kth::database::db_reader(value);
    auto opt = get_header_and_abla_state_from_data(reader);
    if ( ! opt) {
        return std::unexpected(result_code::other);
    }
    return *opt;
}

#if ! defined(KTH_DB_READONLY)

template <typename Clock>
result_code internal_database_basis<Clock>::remove_block_header(hash_digest const& hash, uint32_t height, KTH_DB_txn* db_txn) {
    auto key = kth_db_make_value(sizeof(height), &height);
    auto res = kth_db_del(db_txn, dbi_block_header_, &key, NULL);
    if (res == KTH_DB_NOTFOUND) {
        spdlog::info("[database] Key not found deleting block header in LMDB [remove_block_header] - kth_db_del: {}", res);
        return result_code::key_not_found;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Erro deleting block header in LMDB [remove_block_header] - kth_db_del: {}", res);
        return result_code::other;
    }

    auto key_hash = kth_db_make_value(hash.size(), const_cast<hash_digest&>(hash).data());

    res = kth_db_del(db_txn, dbi_block_header_by_hash_, &key_hash, NULL);
    if (res == KTH_DB_NOTFOUND) {
        spdlog::info("[database] Key not found deleting block header by hash in LMDB [remove_block_header] - kth_db_del: {}", res);
        return result_code::key_not_found;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Erro deleting block header by hash in LMDB [remove_block_header] - kth_db_del: {}", res);
        return result_code::other;
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::replace_headers_from(
    domain::chain::header::list const& headers, uint32_t start_height) {

    if (start_height == 0) {
        spdlog::error("[database] Refusing to replace headers from genesis [replace_headers_from]");
        return result_code::other;
    }

    if (headers.empty()) {
        spdlog::error("[database] Refusing to replace headers with none [replace_headers_from]");
        return result_code::other;
    }

    KTH_DB_txn* db_txn;
    auto res = kth_db_txn_begin(env_, NULL, 0, &db_txn);
    if (res != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    uint32_t height = start_height;
    for (auto const& header : headers) {
        auto const written = push_header_only(header, height, db_txn);
        if (written != result_code::success) {
            kth_db_txn_abort(db_txn);
            return written;
        }
        ++height;
    }

    // `height` is now one past the last header written: the new tip. Walk up
    // until a height is absent — heights are contiguous, so the first gap is the
    // end of the table — dropping what the abandoned branch left behind.
    for (uint32_t h = height; ; ++h) {
        auto const displaced = get_header(h, db_txn);
        if ( ! displaced) {
            break;
        }
        auto const hash = kth::domain::chain::hash(*displaced);
        auto const removed = remove_block_header(hash, h, db_txn);
        if (removed != result_code::success) {
            kth_db_txn_abort(db_txn);
            return removed;
        }
    }

    auto const result = set_property_height(property_code::last_header_height, height - 1, db_txn);
    if (result != result_code::success) {
        kth_db_txn_abort(db_txn);
        return result;
    }

    res = kth_db_txn_commit(db_txn);
    if (res != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    return result_code::success;
}

#endif // ! defined(KTH_DB_READONLY)

} // namespace kth::database

#endif // KTH_DATABASE_HEADER_DATABASE_IPP_
