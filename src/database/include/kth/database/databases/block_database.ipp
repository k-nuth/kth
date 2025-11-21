// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_BLOCK_DATABASE_IPP_
#define KTH_DATABASE_BLOCK_DATABASE_IPP_

#include <kth/infrastructure/log/source.hpp>

namespace kth::database {

//public
template <typename Clock>
std::pair<domain::chain::block, uint32_t> internal_database_basis<Clock>::get_block(hash_digest const& hash) const {
    auto key = kth_db_make_value(hash.size(), const_cast<hash_digest&>(hash).data());

    KTH_DB_txn* db_txn;
    auto res = kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn);
    if (res != KTH_DB_SUCCESS) {
        return {};
    }

    KTH_DB_val value;
    if (kth_db_get(db_txn, dbi_block_header_by_hash_, &key, &value) != KTH_DB_SUCCESS) {
        kth_db_txn_commit(db_txn);
        // kth_db_txn_abort(db_txn);
        return {};
    }

    // assert kth_db_get_size(value) == 4;
    auto height = *static_cast<uint32_t*>(kth_db_get_data(value));

    auto block = get_block(height, db_txn);

    if (kth_db_txn_commit(db_txn) != KTH_DB_SUCCESS) {
        return {};
    }

    return {block, height};
}

//public
template <typename Clock>
domain::chain::block internal_database_basis<Clock>::get_block(uint32_t height) const {
    KTH_DB_txn* db_txn;
    auto res = kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn);
    if (res != KTH_DB_SUCCESS) {
        return domain::chain::block{};
    }

    auto block = get_block(height, db_txn);

    if (kth_db_txn_commit(db_txn) != KTH_DB_SUCCESS) {
        return domain::chain::block{};
    }

    return block;
}

template <typename Clock>
domain::chain::block internal_database_basis<Clock>::get_block(uint32_t height, KTH_DB_txn* db_txn) const {

    auto key = kth_db_make_value(sizeof(height), &height);

    if (db_mode_ == db_mode_type::full) {
        auto header = get_header(height, db_txn);
        if ( ! header.is_valid()) {
            return {};
        }

        domain::chain::transaction::list tx_list;

        KTH_DB_cursor* cursor;
        if (kth_db_cursor_open(db_txn, dbi_block_db_, &cursor) != KTH_DB_SUCCESS) {
            return {};
        }

        KTH_DB_val value;
        int rc;
        if ((rc = kth_db_cursor_get(cursor, &key, &value, MDB_SET)) == 0) {

            auto tx_id = *static_cast<uint32_t*>(kth_db_get_data(value));;
            auto const entry = get_transaction(tx_id, db_txn);

            if ( ! entry.is_valid()) {
                return {};
            }

            tx_list.push_back(std::move(entry.transaction()));

            while ((rc = kth_db_cursor_get(cursor, &key, &value, MDB_NEXT_DUP)) == 0) {
                auto tx_id = *static_cast<uint32_t*>(kth_db_get_data(value));;
                auto const entry = get_transaction(tx_id, db_txn);
                tx_list.push_back(std::move(entry.transaction()));
            }
        }

        kth_db_cursor_close(cursor);

        return domain::chain::block{header, std::move(tx_list)};
    } else if (db_mode_ == db_mode_type::blocks) {
        KTH_DB_val value;

        if (kth_db_get(db_txn, dbi_block_db_, &key, &value) != KTH_DB_SUCCESS) {
            return domain::chain::block{};
        }

        auto data = db_value_to_data_chunk(value);
        byte_reader reader(data);
        auto res = domain::chain::block::from_data(reader);
        if ( ! res) {
            return domain::chain::block{};
        }
        return *res;
    }
    // db_mode_ == db_mode_type::pruned {

    auto block = get_block_reorg(height, db_txn);
    return block;
}


#if ! defined(KTH_DB_READONLY)

template <typename Clock>
result_code internal_database_basis<Clock>::insert_block(domain::chain::block const& block, uint32_t height, uint64_t tx_count, KTH_DB_txn* db_txn) {

    auto key = kth_db_make_value(sizeof(height), &height);

    if (db_mode_ == db_mode_type::full) {

        auto const& txs = block.transactions();

        for (uint64_t i = tx_count; i < tx_count + txs.size(); ++i) {
            auto value = kth_db_make_value(sizeof(i), &i);

            auto res = kth_db_put(db_txn, dbi_block_db_, &key, &value, MDB_APPENDDUP);
            if (res == KTH_DB_KEYEXIST) {
                spdlog::info("[database] Duplicate key in Block DB [insert_block] {}", res);
                return result_code::duplicated_key;
            }

            if (res != KTH_DB_SUCCESS) {
                spdlog::info("[database] Error saving in Block DB [insert_block] {}", res);
                return result_code::other;
            }
        }
    } else if (db_mode_ == db_mode_type::blocks) {
        //TODO: store tx hash
        auto data = block.to_data(false);
        auto value = kth_db_make_value(data.size(), data.data());

        auto res = kth_db_put(db_txn, dbi_block_db_, &key, &value, KTH_DB_APPEND);
        if (res == KTH_DB_KEYEXIST) {
            spdlog::info("[database] Duplicate key in Block DB [insert_block] {}", res);
            return result_code::duplicated_key;
        }

        if (res != KTH_DB_SUCCESS) {
            spdlog::info("[database] Error saving in Block DB [insert_block] {}", res);
            return result_code::other;
        }
    }

    return result_code::success;
}

template <typename Clock>
result_code internal_database_basis<Clock>::remove_blocks_db(uint32_t height, KTH_DB_txn* db_txn) {
    auto key = kth_db_make_value(sizeof(height), &height);

    if (db_mode_ == db_mode_type::full) {
        KTH_DB_cursor* cursor;
        if (kth_db_cursor_open(db_txn, dbi_block_db_, &cursor) != KTH_DB_SUCCESS) {
            return {};
        }

        KTH_DB_val value;
        int rc;
        if ((rc = kth_db_cursor_get(cursor, &key, &value, MDB_SET)) == 0) {

            if (kth_db_cursor_del(cursor, 0) != KTH_DB_SUCCESS) {
                kth_db_cursor_close(cursor);
                return result_code::other;
            }

            while ((rc = kth_db_cursor_get(cursor, &key, &value, MDB_NEXT_DUP)) == 0) {
                if (kth_db_cursor_del(cursor, 0) != KTH_DB_SUCCESS) {
                    kth_db_cursor_close(cursor);
                    return result_code::other;
                }
            }
        }

        kth_db_cursor_close(cursor);
    } else if (db_mode_ == db_mode_type::blocks) {
        auto res = kth_db_del(db_txn, dbi_block_db_, &key, NULL);
        if (res == KTH_DB_NOTFOUND) {
            spdlog::info("[database] Key not found deleting blocks DB in LMDB [remove_blocks_db] - kth_db_del: {}", res);
            return result_code::key_not_found;
        }
        if (res != KTH_DB_SUCCESS) {
            spdlog::info("[database] Error deleting blocks DB in LMDB [remove_blocks_db] - kth_db_del: {}", res);
            return result_code::other;
        }
    }

    return result_code::success;
}

#endif // ! defined(KTH_DB_READONLY)

} // namespace kth::database

#endif // KTH_DATABASE_BLOCK_DATABASE_IPP_
