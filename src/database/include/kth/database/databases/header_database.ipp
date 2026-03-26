// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_HEADER_DATABASE_IPP_
#define KTH_DATABASE_HEADER_DATABASE_IPP_

#include <kth/infrastructure/log/source.hpp>
#include <kth/database/databases/header_abla_entry.hpp>

namespace kth::database {

#if ! defined(KTH_DB_READONLY)

template <typename Clock>
result_code internal_database_basis<Clock>::push_block_header(domain::chain::block const& block, uint32_t height, KTH_DB_txn* db_txn) {

    auto valuearr = to_data_with_abla_state(block);             //TODO(fernando): podría estar afuera de la DBTx
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

    auto res = kth_db_put(db_txn, dbi_block_header_, &key, &value, KTH_DB_APPEND);
    if (res == KTH_DB_KEYEXIST) {
        spdlog::info("[database] Duplicate key inserting header [push_header_only] {}", res);
        return result_code::duplicated_key;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error inserting header [push_header_only] {}", res);
        return result_code::other;
    }

    auto key_by_hash_arr = header.hash();
    auto key_by_hash = kth_db_make_value(key_by_hash_arr.size(), key_by_hash_arr.data());

    res = kth_db_put(db_txn, dbi_block_header_by_hash_, &key_by_hash, &key, KTH_DB_NOOVERWRITE);
    if (res == KTH_DB_KEYEXIST) {
        spdlog::info("[database] Duplicate key inserting header by hash [push_header_only] {}", res);
        return result_code::duplicated_key;
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

    auto key_by_hash_arr = header.hash();
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

    auto data = db_value_to_data_chunk(value);
    byte_reader reader(data);
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

    auto data = db_value_to_data_chunk(value);
    byte_reader reader(data);
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

#endif // ! defined(KTH_DB_READONLY)

} // namespace kth::database

#endif // KTH_DATABASE_HEADER_DATABASE_IPP_
