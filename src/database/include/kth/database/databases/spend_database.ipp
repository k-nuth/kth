// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_SPEND_DATABASE_IPP_
#define KTH_DATABASE_SPEND_DATABASE_IPP_

#include <kth/infrastructure/log/source.hpp>

namespace kth::database {

//public
template <typename Clock>
std::expected<domain::chain::input_point, result_code> internal_database_basis<Clock>::get_spend(domain::chain::output_point const& point) const {

    auto key = kth::database::to_db_value(point, KTH_INTERNAL_DB_WIRE);
    KTH_DB_val value;

    KTH_DB_txn* db_txn;
    auto res0 = kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn);
    if (res0 != KTH_DB_SUCCESS) {
        spdlog::info("[database] Error begining LMDB Transaction [get_spend] {}", res0);
        return std::unexpected(result_code::other);
    }

    res0 = kth_db_get(db_txn, dbi_spend_db_, &key.val, &value);
    if (res0 == KTH_DB_NOTFOUND) {
        kth_db_txn_commit(db_txn);
        return std::unexpected(result_code::key_not_found);
    }
    if (res0 != KTH_DB_SUCCESS) {
        kth_db_txn_commit(db_txn);
        return std::unexpected(result_code::other);
    }

    // Deserialize while the txn is still open — `value` points into
    // store-owned memory that is only valid until commit. This also
    // removes the intermediate `data_chunk` copy the previous code
    // needed to survive the commit.
    auto res_input = kth::database::from_db_value<domain::chain::input_point>(value);

    res0 = kth_db_txn_commit(db_txn);
    if (res0 != KTH_DB_SUCCESS) {
        spdlog::debug("[database] Error commiting LMDB Transaction [get_spend] {}", res0);
        return std::unexpected(result_code::other);
    }

    if ( ! res_input) {
        return std::unexpected(result_code::other);
    }
    return *res_input;
}

#if ! defined(KTH_DB_READONLY)

//pivate
template <typename Clock>
result_code internal_database_basis<Clock>::insert_spend(domain::chain::output_point const& out_point, domain::chain::input_point const& in_point, KTH_DB_txn* db_txn) {

    auto key = kth::database::to_db_value(out_point, KTH_INTERNAL_DB_WIRE);
    auto value = kth::database::to_db_value(in_point, true);

    auto res = kth_db_put(db_txn, dbi_spend_db_, &key.val, &value.val, KTH_DB_NOOVERWRITE);
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

    auto key = kth::database::to_db_value(out_point, KTH_INTERNAL_DB_WIRE);

    auto res = kth_db_del(db_txn, dbi_spend_db_, &key.val, NULL);

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
