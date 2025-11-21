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
#endif // ! defined(KTH_DB_READONLY)

template <typename Clock>
domain::chain::header internal_database_basis<Clock>::get_header(uint32_t height, KTH_DB_txn* db_txn) const {
    auto key = kth_db_make_value(sizeof(height), &height);
    KTH_DB_val value;

    if (kth_db_get(db_txn, dbi_block_header_, &key, &value) != KTH_DB_SUCCESS) {
        return {};
    }

    auto data = db_value_to_data_chunk(value);
    byte_reader reader(data);
    auto opt = get_header_and_abla_state_from_data(reader);
    if ( ! opt) {
        return {};
    }
    return std::get<0>(*opt);
}

template <typename Clock>
std::optional<header_with_abla_state_t> internal_database_basis<Clock>::get_header_and_abla_state(uint32_t height, KTH_DB_txn* db_txn) const {
    auto key = kth_db_make_value(sizeof(height), &height);
    KTH_DB_val value;

    if (kth_db_get(db_txn, dbi_block_header_, &key, &value) != KTH_DB_SUCCESS) {
        return {};
    }

    auto data = db_value_to_data_chunk(value);
    byte_reader reader(data);
    auto opt = get_header_and_abla_state_from_data(reader);
    if ( ! opt) {
        return {};
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
