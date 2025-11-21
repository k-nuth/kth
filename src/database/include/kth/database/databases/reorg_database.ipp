// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_REORG_DATABASE_HPP_
#define KTH_DATABASE_REORG_DATABASE_HPP_

#include <kth/infrastructure/log/source.hpp>

namespace kth::database {

#if ! defined(KTH_DB_READONLY)

template <typename Clock>
result_code internal_database_basis<Clock>::insert_reorg_pool(uint32_t height, KTH_DB_val& key, KTH_DB_txn* db_txn) {
    KTH_DB_val value;
    //TODO: use cursors
    auto res = kth_db_get(db_txn, dbi_utxo_, &key, &value);
    if (res == KTH_DB_NOTFOUND) {
        spdlog::info("[database] Key not found getting UTXO [insert_reorg_pool] {}", res);
        return result_code::key_not_found;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error getting UTXO [insert_reorg_pool] {}", res);
        return result_code::other;
    }

    res = kth_db_put(db_txn, dbi_reorg_pool_, &key, &value, KTH_DB_NOOVERWRITE);
    if (res == KTH_DB_KEYEXIST) {
        spdlog::info("[database] Duplicate key inserting in reorg pool [insert_reorg_pool] {}", res);
        return result_code::duplicated_key;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error inserting in reorg pool [insert_reorg_pool] {}", res);
        return result_code::other;
    }

    auto key_index = kth_db_make_value(sizeof(height), &height);        //TODO(fernando): podría estar afuera de la DBTx
    auto value_index = kth_db_make_value(kth_db_get_size(key), kth_db_get_data(key));     //TODO(fernando): podría estar afuera de la DBTx
    res = kth_db_put(db_txn, dbi_reorg_index_, &key_index, &value_index, 0);

    if (res == KTH_DB_KEYEXIST) {
        spdlog::info("[database] Duplicate key inserting in reorg index [insert_reorg_pool] {}", res);
        return result_code::duplicated_key;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error inserting in reorg index [insert_reorg_pool] {}", res);
        return result_code::other;
    }

    return result_code::success;
}

//TODO : remove this database in db_new_with_blocks and db_new_full
template <typename Clock>
result_code internal_database_basis<Clock>::push_block_reorg(domain::chain::block const& block, uint32_t height, KTH_DB_txn* db_txn) {

    auto valuearr = block.to_data(false);               //TODO(fernando): podría estar afuera de la DBTx
    auto key = kth_db_make_value(sizeof(height), &height);              //TODO(fernando): podría estar afuera de la DBTx
    auto value = kth_db_make_value(valuearr.size(), valuearr.data());   //TODO(fernando): podría estar afuera de la DBTx

    auto res = kth_db_put(db_txn, dbi_reorg_block_, &key, &value, KTH_DB_NOOVERWRITE);
    if (res == KTH_DB_KEYEXIST) {
        spdlog::info("[database] Duplicate key inserting in reorg block [push_block_reorg] {}", res);
        return result_code::duplicated_key;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error inserting in reorg block [push_block_reorg] {}", res);
        return result_code::other;
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::insert_output_from_reorg_and_remove(domain::chain::output_point const& point, KTH_DB_txn* db_txn) {
    auto keyarr = point.to_data(KTH_INTERNAL_DB_WIRE);
    auto key = kth_db_make_value(keyarr.size(), keyarr.data());

    KTH_DB_val value;
    auto res = kth_db_get(db_txn, dbi_reorg_pool_, &key, &value);
    if (res == KTH_DB_NOTFOUND) {
        spdlog::info("[database] Key not found in reorg pool [insert_output_from_reorg_and_remove] {}", res);
        return result_code::key_not_found;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error in reorg pool [insert_output_from_reorg_and_remove] {}", res);
        return result_code::other;
    }

    res = kth_db_put(db_txn, dbi_utxo_, &key, &value, KTH_DB_NOOVERWRITE);
    if (res == KTH_DB_KEYEXIST) {
        spdlog::info("[database] Duplicate key inserting in UTXO [insert_output_from_reorg_and_remove] {}", res);
        return result_code::duplicated_key;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error inserting in UTXO [insert_output_from_reorg_and_remove] {}", res);
        return result_code::other;
    }

    res = kth_db_del(db_txn, dbi_reorg_pool_, &key, NULL);
    if (res == KTH_DB_NOTFOUND) {
        spdlog::info("[database] Key not found deleting in reorg pool [insert_output_from_reorg_and_remove] {}", res);
        return result_code::key_not_found;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error deleting in reorg pool [insert_output_from_reorg_and_remove] {}", res);
        return result_code::other;
    }
    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::remove_block_reorg(uint32_t height, KTH_DB_txn* db_txn) {

    auto key = kth_db_make_value(sizeof(height), &height);
    auto res = kth_db_del(db_txn, dbi_reorg_block_, &key, NULL);
    if (res == KTH_DB_NOTFOUND) {
        spdlog::info("[database] Key not found deleting reorg block in LMDB [remove_block_reorg] - kth_db_del: {}", res);
        return result_code::key_not_found;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error deleting reorg block in LMDB [remove_block_reorg] - kth_db_del: {}", res);
        return result_code::other;
    }
    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::remove_reorg_index(uint32_t height, KTH_DB_txn* db_txn) {

    auto key = kth_db_make_value(sizeof(height), &height);
    auto res = kth_db_del(db_txn, dbi_reorg_index_, &key, NULL);
    if (res == KTH_DB_NOTFOUND) {
        spdlog::debug("[database] Key not found deleting reorg index in LMDB [remove_reorg_index] - height: {} - kth_db_del: {}", height, res);
        return result_code::key_not_found;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::debug("[database] Error deleting reorg index in LMDB [remove_reorg_index] - height: {} - kth_db_del: {}", height, res);
        return result_code::other;
    }
    return result_code::success;
}

#endif // ! defined(KTH_DB_READONLY)

template <typename Clock>
domain::chain::block internal_database_basis<Clock>::get_block_reorg(uint32_t height, KTH_DB_txn* db_txn) const {
    auto key = kth_db_make_value(sizeof(height), &height);
    KTH_DB_val value;

    if (kth_db_get(db_txn, dbi_reorg_block_, &key, &value) != KTH_DB_SUCCESS) {
        return {};
    }

    auto data = db_value_to_data_chunk(value);
    byte_reader reader(data);       //TODO(fernando): mover fuera de la DbTx
    auto res = domain::chain::block::from_data(reader);
    if ( ! res) {
        return domain::chain::block{};
    }
    return *res;
}

template <typename Clock>
domain::chain::block internal_database_basis<Clock>::get_block_reorg(uint32_t height) const {
    KTH_DB_txn* db_txn;
    auto zzz = kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn);
    if (zzz != KTH_DB_SUCCESS) {
        return {};
    }

    auto res = get_block_reorg(height, db_txn);

    if (kth_db_txn_commit(db_txn) != KTH_DB_SUCCESS) {
        return {};
    }

    return res;
}

#if ! defined(KTH_DB_READONLY)

template <typename Clock>
result_code internal_database_basis<Clock>::prune_reorg_index(uint32_t remove_until, KTH_DB_txn* db_txn) {
    KTH_DB_cursor* cursor;
    if (kth_db_cursor_open(db_txn, dbi_reorg_index_, &cursor) != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    KTH_DB_val key;
    KTH_DB_val value;
    int rc;
    while ((rc = kth_db_cursor_get(cursor, &key, &value, KTH_DB_NEXT)) == KTH_DB_SUCCESS) {
        auto current_height = *static_cast<uint32_t*>(kth_db_get_data(key));
        if (current_height < remove_until) {

            auto res = kth_db_del(db_txn, dbi_reorg_pool_, &value, NULL);
            if (res == KTH_DB_NOTFOUND) {
                spdlog::info("[database] Key not found deleting reorg pool in LMDB [prune_reorg_index] - kth_db_del: {}", res);
                return result_code::key_not_found;
            }
            if (res != KTH_DB_SUCCESS) {
                spdlog::info("[database] Error deleting reorg pool in LMDB [prune_reorg_index] - kth_db_del: {}", res);
                return result_code::other;
            }

            if (kth_db_cursor_del(cursor, 0) != KTH_DB_SUCCESS) {
                kth_db_cursor_close(cursor);
                return result_code::other;
            }
        } else {
            break;
        }
    }

    kth_db_cursor_close(cursor);
    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::prune_reorg_block(uint32_t amount_to_delete, KTH_DB_txn* db_txn) {
    //precondition: amount_to_delete >= 1

    KTH_DB_cursor* cursor;
    if (kth_db_cursor_open(db_txn, dbi_reorg_block_, &cursor) != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    int rc;
    while ((rc = kth_db_cursor_get(cursor, nullptr, nullptr, KTH_DB_NEXT)) == KTH_DB_SUCCESS) {
        if (kth_db_cursor_del(cursor, 0) != KTH_DB_SUCCESS) {
            kth_db_cursor_close(cursor);
            return result_code::other;
        }
        if (--amount_to_delete == 0) break;
    }

    kth_db_cursor_close(cursor);
    return result_code::success;
}

#endif // ! defined(KTH_DB_READONLY)

template <typename Clock>
result_code internal_database_basis<Clock>::get_first_reorg_block_height(uint32_t& out_height) const {
    KTH_DB_txn* db_txn;
    auto res = kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn);
    if (res != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    KTH_DB_cursor* cursor;
    if (kth_db_cursor_open(db_txn, dbi_reorg_block_, &cursor) != KTH_DB_SUCCESS) {
        kth_db_txn_commit(db_txn);
        return result_code::other;
    }

    KTH_DB_val key;
    int rc;
    if ((rc = kth_db_cursor_get(cursor, &key, nullptr, KTH_DB_FIRST)) != KTH_DB_SUCCESS) {
        return result_code::db_empty;
    }

    // assert kth_db_get_size(key) == 4;
    out_height = *static_cast<uint32_t*>(kth_db_get_data(key));

    kth_db_cursor_close(cursor);

    // kth_db_txn_abort(db_txn);
    if (kth_db_txn_commit(db_txn) != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    return result_code::success;
}


} // namespace kth::database

#endif // KTH_DATABASE_REORG_DATABASE_HPP_
