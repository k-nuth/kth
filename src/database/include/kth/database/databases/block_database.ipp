// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_BLOCK_DATABASE_IPP_
#define KTH_DATABASE_BLOCK_DATABASE_IPP_

// =============================================================================
// DEPRECATED: Block storage moved to flat files (blk*.dat)
// =============================================================================

// #include <kth/infrastructure/log/source.hpp>
//
// namespace kth::database {
//
// //public
// template <typename Clock>
// std::expected<std::pair<domain::chain::block, uint32_t>, result_code> internal_database_basis<Clock>::get_block(hash_digest const& hash) const {
//     auto key = kth_db_make_value(hash.size(), const_cast<hash_digest&>(hash).data());
//
//     KTH_DB_txn* db_txn;
//     auto res = kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn);
//     if (res != KTH_DB_SUCCESS) {
//         return std::unexpected(result_code::other);
//     }
//
//     KTH_DB_val value;
//     auto res0 = kth_db_get(db_txn, dbi_block_header_by_hash_, &key, &value);
//     if (res0 == KTH_DB_NOTFOUND) {
//         kth_db_txn_commit(db_txn);
//         return std::unexpected(result_code::key_not_found);
//     }
//     if (res0 != KTH_DB_SUCCESS) {
//         kth_db_txn_commit(db_txn);
//         return std::unexpected(result_code::other);
//     }
//
//     // assert kth_db_get_size(value) == 4;
//     auto height = *static_cast<uint32_t*>(kth_db_get_data(value));
//
//     auto block_result = get_block(height, db_txn);
//
//     if (kth_db_txn_commit(db_txn) != KTH_DB_SUCCESS) {
//         return std::unexpected(result_code::other);
//     }
//
//     if ( ! block_result) {
//         return std::unexpected(block_result.error());
//     }
//
//     return std::make_pair(*block_result, height);
// }
//
// //public
// template <typename Clock>
// std::expected<domain::chain::block, result_code> internal_database_basis<Clock>::get_block(uint32_t height) const {
//     KTH_DB_txn* db_txn;
//     auto res = kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn);
//     if (res != KTH_DB_SUCCESS) {
//         return std::unexpected(result_code::other);
//     }
//
//     auto result = get_block(height, db_txn);
//
//     if (kth_db_txn_commit(db_txn) != KTH_DB_SUCCESS) {
//         return std::unexpected(result_code::other);
//     }
//
//     return result;
// }
//
// template <typename Clock>
// std::expected<domain::chain::block, result_code> internal_database_basis<Clock>::get_block(uint32_t height, KTH_DB_txn* db_txn) const {
//
//     auto key = kth_db_make_value(sizeof(height), &height);
//
//     if (db_mode_ == db_mode_type::full) {
//         auto header_result = get_header(height, db_txn);
//         if ( ! header_result) {
//             return std::unexpected(header_result.error());
//         }
//
//         domain::chain::transaction::list tx_list;
//
//         KTH_DB_cursor* cursor;
//         if (kth_db_cursor_open(db_txn, dbi_block_db_, &cursor) != KTH_DB_SUCCESS) {
//             return std::unexpected(result_code::other);
//         }
//
//         KTH_DB_val value;
//         int rc;
//         if ((rc = kth_db_cursor_get(cursor, &key, &value, MDB_SET)) == 0) {
//
//             auto tx_id = *static_cast<uint32_t*>(kth_db_get_data(value));;
//             auto const entry = get_transaction(tx_id, db_txn);
//
//             if ( ! entry.is_valid()) {
//                 kth_db_cursor_close(cursor);
//                 return std::unexpected(result_code::other);
//             }
//
//             tx_list.push_back(std::move(entry.transaction()));
//
//             while ((rc = kth_db_cursor_get(cursor, &key, &value, MDB_NEXT_DUP)) == 0) {
//                 auto tx_id = *static_cast<uint32_t*>(kth_db_get_data(value));;
//                 auto const entry = get_transaction(tx_id, db_txn);
//                 tx_list.push_back(std::move(entry.transaction()));
//             }
//         }
//
//         kth_db_cursor_close(cursor);
//
//         return domain::chain::block{*header_result, std::move(tx_list)};
//     } else if (db_mode_ == db_mode_type::blocks) {
//         KTH_DB_val value;
//
//         auto res0 = kth_db_get(db_txn, dbi_block_db_, &key, &value);
//         if (res0 == KTH_DB_NOTFOUND) {
//             return std::unexpected(result_code::key_not_found);
//         }
//         if (res0 != KTH_DB_SUCCESS) {
//             return std::unexpected(result_code::other);
//         }
//
//         auto data = db_value_to_data_chunk(value);
//         byte_reader reader(data);
//         auto res = domain::chain::block::from_data(reader);
//         if ( ! res) {
//             return std::unexpected(result_code::other);
//         }
//         return *res;
//     }
//     // db_mode_ == db_mode_type::pruned {
//
//     return get_block_reorg(height, db_txn);
// }
//
// //public
// template <typename Clock>
// std::expected<domain::chain::block::list, result_code> internal_database_basis<Clock>::get_blocks(uint32_t from, uint32_t to) const {
//     // precondition: from <= to
//     // Only supports db_mode_type::blocks
//     if (db_mode_ != db_mode_type::blocks) {
//         return std::unexpected(result_code::other);
//     }
//
//     domain::chain::block::list list;
//     list.reserve(to - from + 1);
//
//     KTH_DB_txn* db_txn;
//     auto res = kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn);
//     if (res != KTH_DB_SUCCESS) {
//         return std::unexpected(result_code::other);
//     }
//
//     KTH_DB_cursor* cursor;
//     if (kth_db_cursor_open(db_txn, dbi_block_db_, &cursor) != KTH_DB_SUCCESS) {
//         kth_db_txn_commit(db_txn);
//         return std::unexpected(result_code::other);
//     }
//
//     auto key = kth_db_make_value(sizeof(from), &from);
//     KTH_DB_val value;
//
//     int rc = kth_db_cursor_get(cursor, &key, &value, KTH_DB_SET);
//     if (rc == KTH_DB_NOTFOUND) {
//         kth_db_cursor_close(cursor);
//         kth_db_txn_commit(db_txn);
//         return std::unexpected(result_code::key_not_found);
//     }
//     if (rc != KTH_DB_SUCCESS) {
//         kth_db_cursor_close(cursor);
//         kth_db_txn_commit(db_txn);
//         return std::unexpected(result_code::other);
//     }
//
//     // Process first block
//     {
//         auto data = db_value_to_data_chunk(value);
//         byte_reader reader(data);
//         auto block_res = domain::chain::block::from_data(reader);
//         if ( ! block_res) {
//             kth_db_cursor_close(cursor);
//             kth_db_txn_commit(db_txn);
//             return std::unexpected(result_code::other);
//         }
//         list.push_back(std::move(*block_res));
//     }
//
//     // Process remaining blocks
//     for (uint32_t h = from + 1; h <= to; ++h) {
//         rc = kth_db_cursor_get(cursor, &key, &value, KTH_DB_NEXT);
//         if (rc == KTH_DB_NOTFOUND) {
//             break;  // No more blocks
//         }
//         if (rc != KTH_DB_SUCCESS) {
//             kth_db_cursor_close(cursor);
//             kth_db_txn_commit(db_txn);
//             return std::unexpected(result_code::other);
//         }
//
//         auto data = db_value_to_data_chunk(value);
//         byte_reader reader(data);
//         auto block_res = domain::chain::block::from_data(reader);
//         if ( ! block_res) {
//             kth_db_cursor_close(cursor);
//             kth_db_txn_commit(db_txn);
//             return std::unexpected(result_code::other);
//         }
//         list.push_back(std::move(*block_res));
//     }
//
//     kth_db_cursor_close(cursor);
//
//     if (kth_db_txn_commit(db_txn) != KTH_DB_SUCCESS) {
//         return std::unexpected(result_code::other);
//     }
//
//     return list;
// }
//
// //public
// template <typename Clock>
// std::expected<std::vector<data_chunk>, result_code> internal_database_basis<Clock>::get_blocks_raw(uint32_t from, uint32_t to) const {
//     // precondition: from <= to
//     // Only supports db_mode_type::blocks
//     if (db_mode_ != db_mode_type::blocks) {
//         return std::unexpected(result_code::other);
//     }
//
//     std::vector<data_chunk> list;
//     list.reserve(to - from + 1);
//
//     KTH_DB_txn* db_txn;
//     auto res = kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn);
//     if (res != KTH_DB_SUCCESS) {
//         return std::unexpected(result_code::other);
//     }
//
//     KTH_DB_cursor* cursor;
//     if (kth_db_cursor_open(db_txn, dbi_block_db_, &cursor) != KTH_DB_SUCCESS) {
//         kth_db_txn_commit(db_txn);
//         return std::unexpected(result_code::other);
//     }
//
//     auto key = kth_db_make_value(sizeof(from), &from);
//     KTH_DB_val value;
//
//     int rc = kth_db_cursor_get(cursor, &key, &value, KTH_DB_SET);
//     if (rc == KTH_DB_NOTFOUND) {
//         kth_db_cursor_close(cursor);
//         kth_db_txn_commit(db_txn);
//         return std::unexpected(result_code::key_not_found);
//     }
//     if (rc != KTH_DB_SUCCESS) {
//         kth_db_cursor_close(cursor);
//         kth_db_txn_commit(db_txn);
//         return std::unexpected(result_code::other);
//     }
//
//     // Process first block
//     list.push_back(db_value_to_data_chunk(value));
//
//     // Process remaining blocks
//     for (uint32_t h = from + 1; h <= to; ++h) {
//         rc = kth_db_cursor_get(cursor, &key, &value, KTH_DB_NEXT);
//         if (rc == KTH_DB_NOTFOUND) {
//             break;  // No more blocks
//         }
//         if (rc != KTH_DB_SUCCESS) {
//             kth_db_cursor_close(cursor);
//             kth_db_txn_commit(db_txn);
//             return std::unexpected(result_code::other);
//         }
//         list.push_back(db_value_to_data_chunk(value));
//     }
//
//     kth_db_cursor_close(cursor);
//
//     if (kth_db_txn_commit(db_txn) != KTH_DB_SUCCESS) {
//         return std::unexpected(result_code::other);
//     }
//
//     return list;
// }
//
// #if ! defined(KTH_DB_READONLY)
//
// template <typename Clock>
// result_code internal_database_basis<Clock>::insert_block(domain::chain::block const& block, uint32_t height, uint64_t tx_count, KTH_DB_txn* db_txn) {
//
//     auto key = kth_db_make_value(sizeof(height), &height);
//
//     if (db_mode_ == db_mode_type::full) {
//
//         auto const& txs = block.transactions();
//
//         for (uint64_t i = tx_count; i < tx_count + txs.size(); ++i) {
//             auto value = kth_db_make_value(sizeof(i), &i);
//
//             auto res = kth_db_put(db_txn, dbi_block_db_, &key, &value, MDB_APPENDDUP);
//             if (res == KTH_DB_KEYEXIST) {
//                 spdlog::info("[database] Duplicate key in Block DB [insert_block] {}", res);
//                 return result_code::duplicated_key;
//             }
//
//             if (res != KTH_DB_SUCCESS) {
//                 if (res == MDB_MAP_FULL) {
//                     MDB_envinfo mei;
//                     mdb_env_info(env_, &mei);
//                     auto const map_gib = double(mei.me_mapsize) / (1024.0 * 1024.0 * 1024.0);
//                     std::error_code ec;
//                     auto const space_info = std::filesystem::space(db_dir_, ec);
//                     if (!ec) {
//                         auto const available_gib = double(space_info.available) / (1024.0 * 1024.0 * 1024.0);
//                         spdlog::error("[database] MDB_MAP_FULL: LMDB map exhausted. Map size: {:.1f} GiB | Disk available: {:.1f} GiB. Increase db_max_size.", map_gib, available_gib);
//                     } else {
//                         spdlog::error("[database] MDB_MAP_FULL: LMDB map exhausted. Map size: {:.1f} GiB. Increase db_max_size.", map_gib);
//                     }
//                 } else {
//                     spdlog::info("[database] Error saving in Block DB [insert_block] {}", res);
//                 }
//                 return result_code::other;
//             }
//         }
//     } else if (db_mode_ == db_mode_type::blocks) {
//         //TODO: store tx hash
//         auto data = block.to_data(false);
//         auto value = kth_db_make_value(data.size(), data.data());
//
//         auto res = kth_db_put(db_txn, dbi_block_db_, &key, &value, KTH_DB_APPEND);
//         if (res == KTH_DB_KEYEXIST) {
//             spdlog::info("[database] Duplicate key in Block DB [insert_block] {}", res);
//             return result_code::duplicated_key;
//         }
//
//         if (res != KTH_DB_SUCCESS) {
//             if (res == MDB_MAP_FULL) {
//                 MDB_envinfo mei;
//                 mdb_env_info(env_, &mei);
//                 auto const map_gib = double(mei.me_mapsize) / (1024.0 * 1024.0 * 1024.0);
//                 std::error_code ec;
//                 auto const space_info = std::filesystem::space(db_dir_, ec);
//                 if (!ec) {
//                     auto const available_gib = double(space_info.available) / (1024.0 * 1024.0 * 1024.0);
//                     spdlog::error("[database] MDB_MAP_FULL: LMDB map exhausted. Map size: {:.1f} GiB | Disk available: {:.1f} GiB. Increase db_max_size.", map_gib, available_gib);
//                 } else {
//                     spdlog::error("[database] MDB_MAP_FULL: LMDB map exhausted. Map size: {:.1f} GiB. Increase db_max_size.", map_gib);
//                 }
//             } else {
//                 spdlog::info("[database] Error saving in Block DB [insert_block] {}", res);
//             }
//             return result_code::other;
//         }
//     }
//
//     return result_code::success;
// }
//
// template <typename Clock>
// result_code internal_database_basis<Clock>::remove_blocks_db(uint32_t height, KTH_DB_txn* db_txn) {
//     auto key = kth_db_make_value(sizeof(height), &height);
//
//     if (db_mode_ == db_mode_type::full) {
//         KTH_DB_cursor* cursor;
//         if (kth_db_cursor_open(db_txn, dbi_block_db_, &cursor) != KTH_DB_SUCCESS) {
//             return {};
//         }
//
//         KTH_DB_val value;
//         int rc;
//         if ((rc = kth_db_cursor_get(cursor, &key, &value, MDB_SET)) == 0) {
//
//             if (kth_db_cursor_del(cursor, 0) != KTH_DB_SUCCESS) {
//                 kth_db_cursor_close(cursor);
//                 return result_code::other;
//             }
//
//             while ((rc = kth_db_cursor_get(cursor, &key, &value, MDB_NEXT_DUP)) == 0) {
//                 if (kth_db_cursor_del(cursor, 0) != KTH_DB_SUCCESS) {
//                     kth_db_cursor_close(cursor);
//                     return result_code::other;
//                 }
//             }
//         }
//
//         kth_db_cursor_close(cursor);
//     } else if (db_mode_ == db_mode_type::blocks) {
//         auto res = kth_db_del(db_txn, dbi_block_db_, &key, NULL);
//         if (res == KTH_DB_NOTFOUND) {
//             spdlog::info("[database] Key not found deleting blocks DB in LMDB [remove_blocks_db] - kth_db_del: {}", res);
//             return result_code::key_not_found;
//         }
//         if (res != KTH_DB_SUCCESS) {
//             spdlog::info("[database] Error deleting blocks DB in LMDB [remove_blocks_db] - kth_db_del: {}", res);
//             return result_code::other;
//         }
//     }
//
//     return result_code::success;
// }
//
// #endif // ! defined(KTH_DB_READONLY)
//
// } // namespace kth::database

#endif // KTH_DATABASE_BLOCK_DATABASE_IPP_
