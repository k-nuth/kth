// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_SPEND_DATABASE_IPP_
#define KTH_DATABASE_SPEND_DATABASE_IPP_

#include <kth/infrastructure/log/source.hpp>

namespace kth::database {

//public
template <typename Clock>
domain::chain::input_point internal_database_basis<Clock>::get_spend(domain::chain::output_point const& point) const {

    auto keyarr = point.to_data(KTH_INTERNAL_DB_WIRE);
    auto key = kth_db_make_value(keyarr.size(), keyarr.data());
    KTH_DB_val value;

    KTH_DB_txn* db_txn;
    auto res0 = kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn);
    if (res0 != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error begining LMDB Transaction [get_spend] {}", res0);
        return domain::chain::input_point{};
    }

    res0 = kth_db_get(db_txn, dbi_spend_db_, &key, &value);
    if (res0 != KTH_DB_SUCCESS) {
        kth_db_txn_commit(db_txn);
        // kth_db_txn_abort(db_txn);
        return domain::chain::input_point{};
    }

    auto data = db_value_to_data_chunk(value);

    res0 = kth_db_txn_commit(db_txn);
    if (res0 != KTH_DB_SUCCESS) {
        spdlog::debug("[database] Error commiting LMDB Transaction [get_spend] {}", res0);
        return domain::chain::input_point{};
    }

    byte_reader reader(data);
    auto res_input = domain::chain::input_point::from_data(reader);
    if ( ! res_input) {
        return domain::chain::input_point{};
    }
    return *res_input;
}

#if ! defined(KTH_DB_READONLY)

//pivate
template <typename Clock>
result_code internal_database_basis<Clock>::insert_spend(domain::chain::output_point const& out_point, domain::chain::input_point const& in_point, KTH_DB_txn* db_txn) {

    auto keyarr = out_point.to_data(KTH_INTERNAL_DB_WIRE);
    auto key = kth_db_make_value(keyarr.size(), keyarr.data());

    auto value_arr = in_point.to_data();
    auto value = kth_db_make_value(value_arr.size(), value_arr.data());

    auto res = kth_db_put(db_txn, dbi_spend_db_, &key, &value, KTH_DB_NOOVERWRITE);
    if (res == KTH_DB_KEYEXIST) {
        spdlog::info("[database] Duplicate key inserting spend [insert_spend] {}", res);
        return result_code::duplicated_key;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error inserting spend [insert_spend] {}", res);
        return result_code::other;
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::remove_transaction_spend_db(domain::chain::transaction const& tx, KTH_DB_txn* db_txn) {

    for (auto const& input: tx.inputs()) {

        auto const& prevout = input.previous_output();

        auto res = remove_spend(prevout, db_txn);
        if (res != result_code::success) {
            return res;
        }

        /*res = set_unspend(prevout, db_txn);
        if (res != result_code::success) {
            return res;
        }*/
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::remove_spend(domain::chain::output_point const& out_point, KTH_DB_txn* db_txn) {

    auto keyarr = out_point.to_data(KTH_INTERNAL_DB_WIRE);      //TODO(fernando): podría estar afuera de la DBTx
    auto key = kth_db_make_value(keyarr.size(), keyarr.data());                     //TODO(fernando): podría estar afuera de la DBTx

    auto res = kth_db_del(db_txn, dbi_spend_db_, &key, NULL);

    if (res == KTH_DB_NOTFOUND) {
        spdlog::info("[database] Key not found deleting spend [remove_spend] {}", res);
        return result_code::key_not_found;
    }
    if (res != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error deleting spend [remove_spend] {}", res);
        return result_code::other;
    }
    return result_code::success;
}

#endif // ! defined(KTH_DB_READONLY)



} // namespace kth::database

#endif // KTH_DATABASE_SPEND_DATABASE_IPP_
