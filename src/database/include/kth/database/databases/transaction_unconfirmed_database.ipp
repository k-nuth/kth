// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_TRANSACTION_UNCONFIRMED_DATABASE_HPP_
#define KTH_DATABASE_TRANSACTION_UNCONFIRMED_DATABASE_HPP_

#include <kth/infrastructure/log/source.hpp>

namespace kth::database {

template <typename Clock>
transaction_unconfirmed_entry internal_database_basis<Clock>::get_transaction_unconfirmed(hash_digest const& hash) const {

    KTH_DB_txn* db_txn;
    auto res = kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn);
    if (res != KTH_DB_SUCCESS) {
        return {};
    }

    auto const& ret = get_transaction_unconfirmed(hash, db_txn);

    if (kth_db_txn_commit(db_txn) != KTH_DB_SUCCESS) {
        return {};
    }

    return ret;
}

template <typename Clock>
transaction_unconfirmed_entry internal_database_basis<Clock>::get_transaction_unconfirmed(hash_digest const& hash, KTH_DB_txn* db_txn) const {
    auto key = kth_db_make_value(hash.size(), const_cast<hash_digest&>(hash).data());
    KTH_DB_val value;

    if (kth_db_get(db_txn, dbi_transaction_unconfirmed_db_, &key, &value) != KTH_DB_SUCCESS) {
        return {};
    }

    auto data = db_value_to_data_chunk(value);
    byte_reader reader(data);
    auto res_entry = transaction_unconfirmed_entry::from_data(reader);
    if ( ! res_entry) {
        return {};
    }
    return *res_entry;
}

template <typename Clock>
std::vector<transaction_unconfirmed_entry> internal_database_basis<Clock>::get_all_transaction_unconfirmed() const {

    std::vector<transaction_unconfirmed_entry> result;

    KTH_DB_txn* db_txn;
    auto res = kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn);
    if (res != KTH_DB_SUCCESS) {
        return result;
    }

    KTH_DB_cursor* cursor;

    if (kth_db_cursor_open(db_txn, dbi_transaction_unconfirmed_db_, &cursor) != KTH_DB_SUCCESS) {
        kth_db_txn_commit(db_txn);
        return result;
    }

    KTH_DB_val key;
    KTH_DB_val value;


    int rc;
    if ((rc = kth_db_cursor_get(cursor, &key, &value, KTH_DB_NEXT)) == 0) {

        auto data = db_value_to_data_chunk(value);
        byte_reader reader1(data);
        auto res1 = transaction_unconfirmed_entry::from_data(reader1);
        if (res1) {
            result.push_back(*res1);
        }

        while ((rc = kth_db_cursor_get(cursor, &key, &value, KTH_DB_NEXT)) == 0) {
            auto data = db_value_to_data_chunk(value);
            byte_reader reader2(data);
            auto res2 = transaction_unconfirmed_entry::from_data(reader2);
            if (res2) {
                result.push_back(*res2);
            }
        }
    }

    kth_db_cursor_close(cursor);

    if (kth_db_txn_commit(db_txn) != KTH_DB_SUCCESS) {
        return result;
    }

    return result;
}

#if ! defined(KTH_DB_READONLY)

template <typename Clock>
result_code internal_database_basis<Clock>::remove_transaction_unconfirmed(hash_digest const& tx_id,  KTH_DB_txn* db_txn) {

    auto key = kth_db_make_value(tx_id.size(), const_cast<hash_digest&>(tx_id).data());

    auto res = kth_db_del(db_txn, dbi_transaction_unconfirmed_db_, &key, NULL);
    if (res == KTH_DB_NOTFOUND) {
        // spdlog::debug("[database] Key not found deleting transaction unconfirmed DB in LMDB [remove_transaction_unconfirmed] - kth_db_del: {}", res);
        return result_code::key_not_found;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error deleting transaction unconfirmed DB in LMDB [remove_transaction_unconfirmed] - kth_db_del: {}", res);
        return result_code::other;
    }

    return result_code::success;
}

#endif // ! defined(KTH_DB_READONLY)

template <typename Clock>
inline
uint32_t internal_database_basis<Clock>::get_clock_now() const {
    auto const now = std::chrono::high_resolution_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
}

#if ! defined(KTH_DB_READONLY)

template <typename Clock>
result_code internal_database_basis<Clock>::insert_transaction_unconfirmed(domain::chain::transaction const& tx, uint32_t height, KTH_DB_txn* db_txn) {

    uint32_t arrival_time = get_clock_now();

    auto key_arr = tx.hash();                                    //TODO(fernando): podría estar afuera de la DBTx
    auto key = kth_db_make_value(key_arr.size(), key_arr.data());

    auto value_arr = transaction_unconfirmed_entry::factory_to_data(tx, arrival_time, height);
    auto value = kth_db_make_value(value_arr.size(), value_arr.data());

    auto res = kth_db_put(db_txn, dbi_transaction_unconfirmed_db_, &key, &value, KTH_DB_NOOVERWRITE);
    if (res == KTH_DB_KEYEXIST) {
        spdlog::info("[database] Duplicate key in Transaction Unconfirmed DB [insert_transaction_unconfirmed] {}", res);
        return result_code::duplicated_key;
    }

    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error saving in Transaction Unconfirmed DB [insert_transaction_unconfirmed] {}", res);
        return result_code::other;
    }

    return result_code::success;
}

#endif // ! defined(KTH_DB_READONLY)

} // namespace kth::database

#endif // KTH_DATABASE_TRANSACTION_UNCONFIRMED_DATABASE_HPP_
