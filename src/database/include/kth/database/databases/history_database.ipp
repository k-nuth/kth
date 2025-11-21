// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_HISTORY_DATABASE_IPP_
#define KTH_DATABASE_HISTORY_DATABASE_IPP_

#include <kth/infrastructure/log/source.hpp>

namespace kth::database {

template <typename Clock>
result_code internal_database_basis<Clock>::insert_history_db(domain::wallet::payment_address const& address, data_chunk const& entry, KTH_DB_txn* db_txn) {

    auto key_arr = address.hash20();     //TODO(fernando): should I take a reference?
    auto key = kth_db_make_value(key_arr.size(), key_arr.data());
    auto value = kth_db_make_value(entry.size(), const_cast<data_chunk&>(entry).data());

    auto res = kth_db_put(db_txn, dbi_history_db_, &key, &value, MDB_APPENDDUP);
    if (res == KTH_DB_KEYEXIST) {
        spdlog::info("[database] Duplicate key inserting history [insert_history_db] {}", res);
        return result_code::duplicated_key;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error inserting history [insert_history_db] {}", res);
        return result_code::other;
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::insert_input_history(domain::chain::input_point const& inpoint, uint32_t height, domain::chain::input const& input, KTH_DB_txn* db_txn) {

    auto const& prevout = input.previous_output();

    if (prevout.validation.cache.is_valid()) {

        uint64_t history_count = get_history_count(db_txn);

        if (history_count == max_uint64) {
            spdlog::info("[database] Error getting history items count");
            return result_code::other;
        }

        uint64_t id = history_count;

        // This results in a complete and unambiguous history for the
        // address since standard outputs contain unambiguous address data.
        for (auto const& address : prevout.validation.cache.addresses()) {
            auto valuearr = history_entry::factory_to_data(id, inpoint, domain::chain::point_kind::spend, height, inpoint.index(), prevout.checksum());
            auto res = insert_history_db(address, valuearr, db_txn);
            if (res != result_code::success) {
                return res;
            }
            ++id;
        }
    } else {
            //During an IBD with checkpoints some previous output info is missing.
            //We can recover it by accessing the database

            //TODO (Mario) check if we can query UTXO
            //TODO (Mario) requiere_confirmed = true ??

            auto entry = get_utxo(prevout, db_txn);

            //auto entry = get_transaction(prevout.hash(), max_uint32, true, db_txn);

            if (entry.is_valid()) {

                //auto const& tx = entry.transaction();

                //auto const& out_output = tx.outputs()[prevout.index()];

                uint64_t history_count = get_history_count(db_txn);
                if (history_count == max_uint64) {
                    spdlog::info("[database] Error getting history items count");
                    return result_code::other;
                }

                uint64_t id = history_count;

                auto const& out_output = entry.output();
                for (auto const& address : out_output.addresses()) {
                    auto valuearr = history_entry::factory_to_data(id, inpoint, domain::chain::point_kind::spend, height, inpoint.index(), prevout.checksum());
                    auto res = insert_history_db(address, valuearr, db_txn);
                    if (res != result_code::success) {
                        return res;
                    }
                    ++id;
                }
            }
            else {
                spdlog::info("[database] Error finding UTXO for input history [insert_input_history]");
            }
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::insert_output_history(hash_digest const& tx_hash,uint32_t height, uint32_t index, domain::chain::output const& output, KTH_DB_txn* db_txn ) {

    uint64_t history_count = get_history_count(db_txn);
    if (history_count == max_uint64) {
        spdlog::info("[database] Error getting history items count");
        return result_code::other;
    }

    uint64_t id = history_count;

    auto const outpoint = domain::chain::output_point {tx_hash, index};
    auto const value = output.value();

    // Standard outputs contain unambiguous address data.
    for (auto const& address : output.addresses()) {
        auto valuearr = history_entry::factory_to_data(id, outpoint, domain::chain::point_kind::output, height, index, value);
        auto res = insert_history_db(address, valuearr, db_txn);
        if (res != result_code::success) {
            return res;
        }
        ++id;
    }

    return result_code::success;
}

#endif // ! defined(KTH_DB_READONLY)


template <typename Clock>
// static
domain::chain::history_compact internal_database_basis<Clock>::history_entry_to_history_compact(history_entry const& entry) {
    return domain::chain::history_compact{entry.point_kind(), entry.point(), entry.height(), entry.value_or_checksum()};
}

template <typename Clock>
domain::chain::history_compact::list internal_database_basis<Clock>::get_history(short_hash const& key, size_t limit, size_t from_height) const {

    domain::chain::history_compact::list result;

    if (limit == 0) {
        return result;
    }

    KTH_DB_txn* db_txn;
    auto res = kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn);
    if (res != KTH_DB_SUCCESS) {
        return result;
    }

    KTH_DB_cursor* cursor;
    if (kth_db_cursor_open(db_txn, dbi_history_db_, &cursor) != KTH_DB_SUCCESS) {
        kth_db_txn_commit(db_txn);
        return result;
    }

    auto key_hash = kth_db_make_value(key.size(), const_cast<short_hash&>(key).data());
    KTH_DB_val value;
    int rc;
    if ((rc = kth_db_cursor_get(cursor, &key_hash, &value, MDB_SET)) == 0) {

        auto data = db_value_to_data_chunk(value);
        byte_reader reader1(data);
        auto entry_res = history_entry::from_data(reader1);

        if (entry_res && (from_height == 0 || entry_res->height() >= from_height)) {
            result.push_back(history_entry_to_history_compact(*entry_res));
        }

        while ((rc = kth_db_cursor_get(cursor, &key_hash, &value, MDB_NEXT_DUP)) == 0) {

            if (limit > 0 && result.size() >= limit) {
                break;
            }

            auto data = db_value_to_data_chunk(value);
            byte_reader reader2(data);
            auto entry_res2 = history_entry::from_data(reader2);

            if (entry_res2 && (from_height == 0 || entry_res2->height() >= from_height)) {
                result.push_back(history_entry_to_history_compact(*entry_res2));
            }

        }
    }

    kth_db_cursor_close(cursor);

    if (kth_db_txn_commit(db_txn) != KTH_DB_SUCCESS) {
        return result;
    }

    return result;
}

template <typename Clock>
std::vector<hash_digest> internal_database_basis<Clock>::get_history_txns(short_hash const& key, size_t limit, size_t from_height) const {

    std::set<hash_digest> temp;
    std::vector<hash_digest> result;

    // Stop once we reach the limit (if specified).
    if (limit == 0)
        return result;

    KTH_DB_txn* db_txn;
    auto res = kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn);
    if (res != KTH_DB_SUCCESS) {
        return result;
    }

    KTH_DB_cursor* cursor;
    if (kth_db_cursor_open(db_txn, dbi_history_db_, &cursor) != KTH_DB_SUCCESS) {
        kth_db_txn_commit(db_txn);
        return result;
    }

    auto key_hash = kth_db_make_value(key.size(), const_cast<short_hash&>(key).data());;
    KTH_DB_val value;
    int rc;
    if ((rc = kth_db_cursor_get(cursor, &key_hash, &value, MDB_SET)) == 0) {

        auto data = db_value_to_data_chunk(value);
        byte_reader reader3(data);
        auto entry_res3 = history_entry::from_data(reader3);

        if (entry_res3 && (from_height == 0 || entry_res3->height() >= from_height)) {
            // Avoid inserting the same tx
            auto const & pair = temp.insert(entry_res3->point().hash());
            if (pair.second){
                // Add valid txns to the result vector
                result.push_back(*pair.first);
            }
        }

        while ((rc = kth_db_cursor_get(cursor, &key_hash, &value, MDB_NEXT_DUP)) == 0) {

            if (limit > 0 && result.size() >= limit) {
                break;
            }

            auto data = db_value_to_data_chunk(value);
            byte_reader reader4(data);
            auto entry_res4 = history_entry::from_data(reader4);

            if (entry_res4 && (from_height == 0 || entry_res4->height() >= from_height)) {
                // Avoid inserting the same tx
                auto const & pair = temp.insert(entry_res4->point().hash());
                if (pair.second){
                    // Add valid txns to the result vector
                    result.push_back(*pair.first);
                }
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
result_code internal_database_basis<Clock>::remove_transaction_history_db(domain::chain::transaction const& tx, size_t height, KTH_DB_txn* db_txn) {

    for (auto const& output: tx.outputs()) {
        for (auto const& address : output.addresses()) {
            auto res = remove_history_db(address.hash20(), height, db_txn);
            if (res != result_code::success) {
                return res;
            }
        }
    }

    for (auto const& input: tx.inputs()) {

        auto const& prevout = input.previous_output();

        if (prevout.validation.cache.is_valid()) {
            for (auto const& address : prevout.validation.cache.addresses()) {
                auto res = remove_history_db(address.hash20(), height, db_txn);
                if (res != result_code::success) {
                    return res;
                }
            }
        }
        else {

            auto const& entry = get_transaction(prevout.hash(), max_uint32, db_txn);

            if (entry.is_valid()) {

                auto const& tx = entry.transaction();
                auto const& out_output = tx.outputs()[prevout.index()];

                for (auto const& address : out_output.addresses()) {

                    auto res = remove_history_db(address.hash20(), height, db_txn);
                    if (res != result_code::success) {
                        return res;
                    }
                }
            }

            /*
            for (auto const& address : input.addresses()) {
                auto res = remove_history_db(address.hash20(), height, db_txn);
                if (res != result_code::success) {
                    return res;
                }
            }*/
        }
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::remove_history_db(short_hash const& key, size_t height, KTH_DB_txn* db_txn) {

    KTH_DB_cursor* cursor;

    if (kth_db_cursor_open(db_txn, dbi_history_db_, &cursor) != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    auto key_hash = kth_db_make_value(key.size(), const_cast<short_hash&>(key).data());;
    KTH_DB_val value;
    int rc;
    if ((rc = kth_db_cursor_get(cursor, &key_hash, &value, MDB_SET)) == 0) {

        auto data = db_value_to_data_chunk(value);
        byte_reader reader5(data);
        auto entry_res5 = history_entry::from_data(reader5);

        if (entry_res5 && entry_res5->height() == height) {

            if (kth_db_cursor_del(cursor, 0) != KTH_DB_SUCCESS) {
                kth_db_cursor_close(cursor);
                return result_code::other;
            }
        }

        while ((rc = kth_db_cursor_get(cursor, &key_hash, &value, MDB_NEXT_DUP)) == 0) {

            auto data = db_value_to_data_chunk(value);
            byte_reader reader6(data);
            auto entry_res6 = history_entry::from_data(reader6);

            if (entry_res6 && entry_res6->height() == height) {
                if (kth_db_cursor_del(cursor, 0) != KTH_DB_SUCCESS) {
                    kth_db_cursor_close(cursor);
                    return result_code::other;
                }
            }
        }
    }

    kth_db_cursor_close(cursor);

    return result_code::success;
}

#endif // ! defined(KTH_DB_READONLY)


template <typename Clock>
uint64_t internal_database_basis<Clock>::get_history_count(KTH_DB_txn* db_txn) const {
  MDB_stat db_stats;
  auto ret = mdb_stat(db_txn, dbi_history_db_, &db_stats);
  if (ret != KTH_DB_SUCCESS) {
      return max_uint64;
  }
  return db_stats.ms_entries;
}

} // namespace kth::database
