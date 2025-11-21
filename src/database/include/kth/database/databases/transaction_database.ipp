// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_TRANSACTION_DATABASE_HPP_
#define KTH_DATABASE_TRANSACTION_DATABASE_HPP_

#include <kth/infrastructure/log/source.hpp>

namespace kth::database {

//public
template <typename Clock>
transaction_entry internal_database_basis<Clock>::get_transaction(hash_digest const& hash, size_t fork_height) const {

    KTH_DB_txn* db_txn;
    auto res = kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn);
    if (res != KTH_DB_SUCCESS) {
        return transaction_entry{};
    }

    auto entry = get_transaction(hash, fork_height, db_txn);

    if (kth_db_txn_commit(db_txn) != KTH_DB_SUCCESS) {
        return transaction_entry{};
    }

    return entry;

}

#if ! defined(KTH_DB_READONLY)

template <typename Clock>
template <typename I>
result_code internal_database_basis<Clock>::insert_transactions(I f, I l, uint32_t height, uint32_t median_time_past, uint64_t tx_count, KTH_DB_txn* db_txn) {

    auto id = tx_count;
    uint32_t pos = 0;

    while (f != l) {
        auto const& tx = *f;

        //TODO: (Mario) : Implement tx.Confirm to update existing transactions
        auto res = insert_transaction(id, tx, height, median_time_past, pos, db_txn);
        if (res != result_code::success && res != result_code::duplicated_key) {
            return res;
        }

        //remove unconfirmed transaction if exists
        //TODO (Mario): don't recalculate tx.hash
        res = remove_transaction_unconfirmed(tx.hash(), db_txn);
        if (res != result_code::success && res != result_code::key_not_found) {
            return res;
        }

        ++f;
        ++pos;
        ++id;
    }

    return result_code::success;
}
#endif // ! defined(KTH_DB_READONLY)

template <typename Clock>
transaction_entry internal_database_basis<Clock>::get_transaction(uint64_t id, KTH_DB_txn* db_txn) const {

    auto key = kth_db_make_value(sizeof(id), &id);
    KTH_DB_val value;

    auto res = kth_db_get(db_txn, dbi_transaction_db_, &key, &value);

    if (res != KTH_DB_SUCCESS) {
        return {};
    }

    auto data = db_value_to_data_chunk(value);
    byte_reader reader(data);
    auto entry_res = transaction_entry::from_data(reader);
    if ( ! entry_res) {
        return {};
    }
    return *entry_res;
}

template <typename Clock>
transaction_entry internal_database_basis<Clock>::get_transaction(hash_digest const& hash, size_t fork_height, KTH_DB_txn* db_txn) const {
    auto key  = kth_db_make_value(hash.size(), const_cast<hash_digest&>(hash).data());
    KTH_DB_val value;

    auto res = kth_db_get(db_txn, dbi_transaction_hash_db_, &key, &value);
    if (res != KTH_DB_SUCCESS) {
        return {};
    }

    auto tx_id = *static_cast<uint32_t*>(kth_db_get_data(value));;

    auto const entry = get_transaction(tx_id, db_txn);

    if (entry.height() > fork_height) {
        return {};
    }

    //Note(Knuth): Transaction stored in dbi_transaction_db_ are always confirmed
    //the parameter requiere_confirmed is never used.
    /*if ( !  require_confirmed) {
        return entry;
    }

    auto const confirmed = entry.confirmed();

    return (confirmed && entry.height() > fork_height) || (require_confirmed && ! confirmed) ? transaction_entry{} : entry;
    */

    return entry;
}

#if ! defined(KTH_DB_READONLY)

template <typename Clock>
result_code internal_database_basis<Clock>::insert_transaction(uint64_t id, domain::chain::transaction const& tx, uint32_t height, uint32_t median_time_past, uint32_t position, KTH_DB_txn* db_txn) {

    auto key = kth_db_make_value(sizeof(id), &id);

    //auto key_arr = tx.hash();                                    //TODO(fernando): podría estar afuera de la DBTx
    //auto key  = kth_db_make_value(key_arr.size(), key_arr.data());

    auto valuearr = transaction_entry::factory_to_data(tx, height, median_time_past, position);
    auto value = kth_db_make_value(valuearr.size(), valuearr.data());

    auto res = kth_db_put(db_txn, dbi_transaction_db_, &key, &value, KTH_DB_APPEND);
    if (res == KTH_DB_KEYEXIST) {
        spdlog::info("[database] Duplicate key in Transaction DB [insert_transaction] {}", res);
        return result_code::duplicated_key;
    }

    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error saving in Transaction DB [insert_transaction] {}", res);
        return result_code::other;
    }


    auto key_arr = tx.hash();                                    //TODO(fernando): podría estar afuera de la DBTx
    auto key_tx  = kth_db_make_value(key_arr.size(), key_arr.data());

    res = kth_db_put(db_txn, dbi_transaction_hash_db_, &key_tx, &key, KTH_DB_NOOVERWRITE);
    if (res == KTH_DB_KEYEXIST) {
        spdlog::info("[database] Duplicate key in Transaction DB [insert_transaction] {}", res);
        return result_code::duplicated_key;
    }

    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error saving in Transaction DB [insert_transaction] {}", res);
        return result_code::other;
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::remove_transactions(domain::chain::block const& block, uint32_t height, KTH_DB_txn* db_txn) {

    auto const& txs = block.transactions();
    uint32_t pos = 0;
    for (auto const& tx : txs) {

        auto const& hash = tx.hash();

        auto res0 = remove_transaction_history_db(tx, height, db_txn);
        if (res0 != result_code::success) {
            return res0;
        }

        if (pos > 0) {
            auto res0 = remove_transaction_spend_db(tx, db_txn);
            if (res0 != result_code::success && res0 != result_code::key_not_found) {
                return res0;
            }
        }

        auto key = kth_db_make_value(hash.size(), const_cast<hash_digest&>(hash).data());
        KTH_DB_val value;

        auto res = kth_db_get(db_txn, dbi_transaction_hash_db_, &key, &value);
        if (res != KTH_DB_SUCCESS) {
            return result_code::other;
        }

        auto tx_id = *static_cast<uint32_t*>(kth_db_get_data(value));;
        auto key_tx = kth_db_make_value(sizeof(tx_id), &tx_id);

        res = kth_db_del(db_txn, dbi_transaction_db_, &key_tx, NULL);
        if (res == KTH_DB_NOTFOUND) {
            spdlog::info("[database] Key not found deleting transaction DB in LMDB [remove_transactions] - kth_db_del: {}", res);
            return result_code::key_not_found;
        }
        if (res != KTH_DB_SUCCESS) {
            spdlog::info("[database] Error deleting transaction DB in LMDB [remove_transactions] - kth_db_del: {}", res);
            return result_code::other;
        }

        res = kth_db_del(db_txn, dbi_transaction_hash_db_, &key, NULL);
        if (res == KTH_DB_NOTFOUND) {
            spdlog::info("[database] Key not found deleting transaction DB in LMDB [remove_transactions] - kth_db_del: {}", res);
            return result_code::key_not_found;
        }
        if (res != KTH_DB_SUCCESS) {
            spdlog::info("[database] Error deleting transaction DB in LMDB [remove_transactions] - kth_db_del: {}", res);
            return result_code::other;
        }

        ++pos;
    }


    /*auto key = kth_db_make_value(sizeof(height), &height);
    KTH_DB_val value;

    KTH_DB_cursor* cursor;
    if (kth_db_cursor_open(db_txn, dbi_block_db_, &cursor) != KTH_DB_SUCCESS) {
        return {};
    }

    uint32_t pos = 0;
    int rc;
    if ((rc = kth_db_cursor_get(cursor, &key, &value, MDB_SET)) == 0) {

        auto tx_id = *static_cast<uint32_t*>(kth_db_get_data(value));;
        auto const entry = get_transaction(tx_id, db_txn);
        auto const& tx = entry.transaction();

        auto res0 = remove_transaction_history_db(tx, height, db_txn);
        if (res0 != result_code::success) {
            return res0;
        }

        if (pos > 0) {
            res0 = remove_transaction_spend_db(tx, db_txn);
            if (res0 != result_code::success && res0 != result_code::key_not_found) {
                return res0;
            }
        }

        auto key_tx = kth_db_make_value(sizeof(tx_id), &tx_id);
        auto res = kth_db_del(db_txn, dbi_transaction_db_, &key_tx, NULL);
        if (res == KTH_DB_NOTFOUND) {
            spdlog::info("[database] Key not found deleting transaction DB in LMDB [remove_transactions] - kth_db_del: {}", res);
            return result_code::key_not_found;
        }
        if (res != KTH_DB_SUCCESS) {
            spdlog::info("[database] Error deleting transaction DB in LMDB [remove_transactions] - kth_db_del: {}", res);
            return result_code::other;
        }

        auto key_hash = kth_db_make_value(tx.hash().size(), tx.hash().data());
        res = kth_db_del(db_txn, dbi_transaction_hash_db_, &key_hash, NULL);
        if (res == KTH_DB_NOTFOUND) {
            spdlog::info("[database] Key not found deleting transaction DB in LMDB [remove_transactions] - kth_db_del: {}", res);
            return result_code::key_not_found;
        }
        if (res != KTH_DB_SUCCESS) {
            spdlog::info("[database] Error deleting transaction DB in LMDB [remove_transactions] - kth_db_del: {}", res);
            return result_code::other;
        }

        while ((rc = kth_db_cursor_get(cursor, &key, &value, MDB_NEXT_DUP)) == 0) {
            ++pos;
            auto tx_id = *static_cast<uint32_t*>(kth_db_get_data(value));;
            auto const entry = get_transaction(tx_id, db_txn);
            auto const& tx = entry.transaction();

            auto res0 = remove_transaction_history_db(tx, height, db_txn);
            if (res0 != result_code::success) {
                return res0;
            }

            if (pos > 0) {
                res0 = remove_transaction_spend_db(tx, db_txn);
                if (res0 != result_code::success && res0 != result_code::key_not_found) {
                    return res0;
                }
            }

            auto key_tx = kth_db_make_value(sizeof(tx_id), &tx_id);
            auto res = kth_db_del(db_txn, dbi_transaction_db_, &key_tx, NULL);
            if (res == KTH_DB_NOTFOUND) {
                spdlog::info("[database] Key not found deleting transaction DB in LMDB [remove_transactions] - kth_db_del: {}", res);
                return result_code::key_not_found;
            }
            if (res != KTH_DB_SUCCESS) {
                spdlog::info("[database] Error deleting transaction DB in LMDB [remove_transactions] - kth_db_del: {}", res);
                return result_code::other;
            }

            auto key_hash = kth_db_make_value(tx.hash().size(), tx.hash().data());
            res = kth_db_del(db_txn, dbi_transaction_hash_db_, &key_hash, NULL);
            if (res == KTH_DB_NOTFOUND) {
                spdlog::info("[database] Key not found deleting transaction DB in LMDB [remove_transactions] - kth_db_del: {}", res);
                return result_code::key_not_found;
            }
            if (res != KTH_DB_SUCCESS) {
                spdlog::info("[database] Error deleting transaction DB in LMDB [remove_transactions] - kth_db_del: {}", res);
                return result_code::other;
            }
        }
    }

    kth_db_cursor_close(cursor);*/


    /*//To get the tx hashes to remove, we need to read the block db
    if (kth_db_get(db_txn, dbi_block_db_, &key, &value) != KTH_DB_SUCCESS) {
        return result_code::other;
    }

    auto n = kth_db_get_size(value);
    auto f = static_cast<uint8_t*>(kth_db_get_data(value));
    //precondition: mv_size es multiplo de 32

    uint32_t pos = 0;
    while (n != 0) {
        hash_digest h;
        std::copy(f, f + kth::hash_size, h.data());

        auto key_tx = kth_db_make_value(h.size(), h.data());

        auto const& entry = get_transaction(h, height, db_txn);
        if ( ! entry.is_valid() ) {
            return result_code::other;
        }

        auto const& tx = entry.transaction();

        auto res0 = remove_transaction_history_db(tx, height, db_txn);
        if (res0 != result_code::success) {
            return res0;
        }

        if (pos > 0) {
            res0 = remove_transaction_spend_db(tx, db_txn);
            if (res0 != result_code::success && res0 != result_code::key_not_found) {
                return res0;
            }
        }

        auto res = kth_db_del(db_txn, dbi_transaction_db_, &key_tx, NULL);
        if (res == KTH_DB_NOTFOUND) {
            spdlog::info("[database] Key not found deleting transaction DB in LMDB [remove_transactions] - kth_db_del: {}", res);
            return result_code::key_not_found;
        }
        if (res != KTH_DB_SUCCESS) {
            spdlog::info("[database] Error deleting transaction DB in LMDB [remove_transactions] - kth_db_del: {}", res);
            return result_code::other;
        }

        n -= kth::hash_size;
        f += kth::hash_size;
        ++pos;
    }*/

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::update_transaction(domain::chain::transaction const& tx, uint32_t height, uint32_t median_time_past, uint32_t position, KTH_DB_txn* db_txn) {
    auto key_arr = tx.hash();                                    //TODO(fernando): podría estar afuera de la DBTx
    auto key  = kth_db_make_value(key_arr.size(), key_arr.data());

    auto valuearr = transaction_entry::factory_to_data(tx, height, median_time_past, position);
    auto value = kth_db_make_value(valuearr.size(), valuearr.data());

    auto res = kth_db_put(db_txn, dbi_transaction_db_, &key, &value, 0);
    if (res == KTH_DB_KEYEXIST) {
        spdlog::info("[database] Duplicate key in Transaction DB [insert_transaction] {}", res);
        return result_code::duplicated_key;
    }

    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error saving in Transaction DB [insert_transaction] {}", res);
        return result_code::other;
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::set_spend(domain::chain::output_point const& point, uint32_t spender_height, KTH_DB_txn* db_txn) {

    // Limit search to confirmed transactions at or below the spender height,
    // since a spender cannot spend above its own height.
    // Transactions are not marked as spent unless the spender is confirmed.
    // This is consistent with support for unconfirmed double spends.

    auto entry = get_transaction(point.hash(), spender_height, db_txn);

    // The transaction is not exist as confirmed at or below the height.
    if ( ! entry.is_valid() ) {
        return result_code::other;
    }

    auto const& tx = entry.transaction();

    if (point.index() >= tx.outputs().size()) {
        return result_code::other;
    }

    //update spender_height
    auto& output = tx.outputs()[point.index()];
    output.validation.spender_height = spender_height;

    //overwrite transaction
    auto ret = update_transaction(tx, entry.height(), entry.median_time_past(), entry.position(), db_txn);
    if (ret != result_code::success & ret != result_code::duplicated_key) {
        return ret;
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::set_unspend(domain::chain::output_point const& point, KTH_DB_txn* db_txn) {
    return set_spend(point, domain::chain::output::validation::not_spent, db_txn);
}
#endif // ! defined(KTH_DB_READONLY)

template <typename Clock>
uint64_t internal_database_basis<Clock>::get_tx_count(KTH_DB_txn* db_txn) const {
  MDB_stat db_stats;
  auto ret = mdb_stat(db_txn, dbi_transaction_db_, &db_stats);
  if (ret != KTH_DB_SUCCESS) {
      return max_uint64;
  }
  return db_stats.ms_entries;
}



} // namespace kth::database

#endif // KTH_DATABASE_TRANSACTION_DATABASE_HPP_
