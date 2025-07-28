// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_LMDB_HELPER_HPP_
#define KTH_DATABASE_LMDB_HELPER_HPP_

#include <kth/database/databases/generic_db.hpp>

#include <utility>

namespace kth::database {

std::pair<int, KTH_DB_txn*> transaction_begin(KTH_DB_env* env, KTH_DB_txn* parent, unsigned int flags) {
    KTH_DB_txn* db_txn;
    auto res = kth_db_txn_begin(env, NULL, 0, &db_txn);
    return { res, db_txn };
}

std::pair<int, KTH_DB_txn*> transaction_readonly_begin(KTH_DB_env* env, KTH_DB_txn* parent, unsigned int flags) {
    return transaction_begin(env, parent, flags | KTH_DB_RDONLY);
}

#if ! defined(KTH_DB_READONLY)
#endif

}  // namespace kth::database

#endif  // KTH_DATABASE_LMDB_HELPER_HPP_
