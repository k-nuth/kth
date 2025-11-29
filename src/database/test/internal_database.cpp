// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <filesystem>
#include <print>
#include <tuple>

#include <test_helpers.hpp>

#include <kth/database.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>

using namespace boost::system;
using namespace std::filesystem;

using namespace kth;
using namespace kth::domain::chain;
using namespace kth::database;

#define DIRECTORY "internal_database"
fs::path const db_path = fs::path(DIRECTORY) / "internal_db";

constexpr uint64_t db_size = uint64_t(10485760) * 10; //100 * (uint64_t(1) << 30);

struct internal_database_directory_setup_fixture {
    internal_database_directory_setup_fixture() {
        std::error_code ec;
        remove_all(DIRECTORY, ec);
        REQUIRE(create_directories(DIRECTORY, ec));

        internal_database db(db_path, db_mode_type::full, 10000000, db_size, true);
        REQUIRE(db.create());
        REQUIRE(db.close());
        REQUIRE(db.open());
    }

    ////~internal_database_directory_setup_fixture() {
    ////    error_code ec;
    ////    remove_all(DIRECTORY, ec);
    ////}
};

domain::chain::block get_block(std::string const& enc) {
    auto const data = decode_base16(enc);
    byte_reader reader(*data);
    auto res = domain::chain::block::from_data(reader);
    if ( ! res) {
        return domain::chain::block{};
    }
    return *res;
}

domain::chain::block get_genesis() {
    std::string genesis_enc =
        "01000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"
        "29ab5f49"
        "ffff001d"
        "1dac2b7c"
        "01"
        "01000000"
        "01"
        "0000000000000000000000000000000000000000000000000000000000000000ffffffff"
        "4d"
        "04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73"
        "ffffffff"
        "01"
        "00f2052a01000000"
        "43"
        "4104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac"
        "00000000";
    return get_block(genesis_enc);
}

domain::chain::block get_fake_genesis() {
    std::string genesis_enc =
        "02000000"                                                              // 4     version
        "0000000000000000000000000000000000000000000000000000000000000000"      //32     Previous Block Hash
        "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"      //32     Merkle root
        "29ab5f49"                                                              // 4     Timestamp
        "ffff001d"                                                              // 4
        "1dac2b7c"                                                              // 4
        "01"                                                                    // 1
        "01000000"                                                              // 4
        "01"                                                                    // 1
        "0000000000000000000000000000000000000000000000000000000000000000ffffffff" //36
        "4d"
        "04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73" //77
        "ffffffff"
        "01"
        "00f2052a01000000"
        "43"
        "4104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac"
        "00000000";
    return get_block(genesis_enc);
}

void close_everything(KTH_DB_env* e, KTH_DB_dbi& db0, KTH_DB_dbi& db1, KTH_DB_dbi& db2, KTH_DB_dbi& db3, KTH_DB_dbi& db4, KTH_DB_dbi& db5
, KTH_DB_dbi& db6
, KTH_DB_dbi& db7
, KTH_DB_dbi& db8
, KTH_DB_dbi& db9
, KTH_DB_dbi& db10
, KTH_DB_dbi& db11
) {
    kth_db_dbi_close(e, db0);
    kth_db_dbi_close(e, db1);
    kth_db_dbi_close(e, db2);
    kth_db_dbi_close(e, db3);
    kth_db_dbi_close(e, db4);
    kth_db_dbi_close(e, db5);
    kth_db_dbi_close(e, db6);
    kth_db_dbi_close(e, db7);
    kth_db_dbi_close(e, db8);
    kth_db_dbi_close(e, db9);
    kth_db_dbi_close(e, db10);
    kth_db_dbi_close(e, db11);

    kth_db_env_close(e);
}

std::tuple<KTH_DB_env*, KTH_DB_dbi, KTH_DB_dbi, KTH_DB_dbi, KTH_DB_dbi, KTH_DB_dbi, KTH_DB_dbi
, KTH_DB_dbi
, KTH_DB_dbi
, KTH_DB_dbi
, KTH_DB_dbi
, KTH_DB_dbi
, KTH_DB_dbi
> open_dbs() {

    KTH_DB_env* env_;
    KTH_DB_dbi dbi_utxo_;
    KTH_DB_dbi dbi_reorg_pool_;
    KTH_DB_dbi dbi_reorg_index_;
    KTH_DB_dbi dbi_block_header_;
    KTH_DB_dbi dbi_block_header_by_hash_;
    KTH_DB_dbi dbi_reorg_block_;


    KTH_DB_dbi dbi_block_db_;
    KTH_DB_dbi dbi_transaction_db_;
    KTH_DB_dbi dbi_history_db_;
    KTH_DB_dbi dbi_spend_db_;
    KTH_DB_dbi dbi_transaction_hash_db_;
    KTH_DB_dbi dbi_transaction_unconfirmed_db_;

    KTH_DB_txn* db_txn;

    char block_header_db_name[] = "block_header";
    char block_header_by_hash_db_name[] = "block_header_by_hash";
    char utxo_db_name[] = "utxo_db";
    char reorg_pool_name[] = "reorg_pool";
    char reorg_index_name[] = "reorg_index";
    char reorg_block_name[] = "reorg_block";


    //Blocks DB
    char block_db_name[] = "blocks";

    char transaction_db_name[] = "transactions";
    char transaction_hash_db_name[] = "transactions_hash";
    char transaction_unconfirmed_db_name[] = "transactions_unconfirmed";
    char history_db_name[] = "history";
    char spend_db_name[] = "spend";

    REQUIRE(kth_db_env_create(&env_) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_env_set_mapsize(env_, db_size) == KTH_DB_SUCCESS);


    REQUIRE(kth_db_env_set_maxdbs(env_, 12) == KTH_DB_SUCCESS);
    // REQUIRE(kth_db_env_set_maxdbs(env_, 7) == KTH_DB_SUCCESS);
    // REQUIRE(kth_db_env_set_maxdbs(env_, 6) == KTH_DB_SUCCESS);

    auto qqq = kth_db_env_open(env_, db_path.c_str(), KTH_DB_NORDAHEAD | KTH_DB_NOSYNC | KTH_DB_NOTLS, 0664);

    REQUIRE(qqq == KTH_DB_SUCCESS);
    REQUIRE(kth_db_txn_begin(env_, NULL, 0, &db_txn) == KTH_DB_SUCCESS);


    REQUIRE(kth_db_dbi_open(db_txn, block_header_db_name, KTH_DB_CONDITIONAL_CREATE | KTH_DB_INTEGERKEY, &dbi_block_header_) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_dbi_open(db_txn, block_header_by_hash_db_name, KTH_DB_CONDITIONAL_CREATE, &dbi_block_header_by_hash_) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_dbi_open(db_txn, utxo_db_name, KTH_DB_CONDITIONAL_CREATE, &dbi_utxo_) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_dbi_open(db_txn, reorg_pool_name, KTH_DB_CONDITIONAL_CREATE, &dbi_reorg_pool_) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_dbi_open(db_txn, reorg_index_name, KTH_DB_CONDITIONAL_CREATE | KTH_DB_DUPSORT | KTH_DB_INTEGERKEY | KTH_DB_DUPFIXED, &dbi_reorg_index_) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_dbi_open(db_txn, reorg_block_name, KTH_DB_CONDITIONAL_CREATE | KTH_DB_INTEGERKEY, &dbi_reorg_block_) == KTH_DB_SUCCESS);

    REQUIRE(kth_db_dbi_open(db_txn, block_db_name, KTH_DB_CONDITIONAL_CREATE | KTH_DB_INTEGERKEY, &dbi_block_db_) == KTH_DB_SUCCESS);

    REQUIRE(kth_db_dbi_open(db_txn, block_db_name, KTH_DB_CONDITIONAL_CREATE | KTH_DB_DUPSORT | KTH_DB_INTEGERKEY | KTH_DB_DUPFIXED  | MDB_INTEGERDUP, &dbi_block_db_)== KTH_DB_SUCCESS);
    REQUIRE(kth_db_dbi_open(db_txn, transaction_db_name, KTH_DB_CONDITIONAL_CREATE | KTH_DB_INTEGERKEY, &dbi_transaction_db_)== KTH_DB_SUCCESS);
    REQUIRE(kth_db_dbi_open(db_txn, transaction_hash_db_name, KTH_DB_CONDITIONAL_CREATE, &dbi_transaction_hash_db_)== KTH_DB_SUCCESS);
    REQUIRE(kth_db_dbi_open(db_txn, history_db_name, KTH_DB_CONDITIONAL_CREATE | KTH_DB_DUPSORT | KTH_DB_DUPFIXED, &dbi_history_db_)== KTH_DB_SUCCESS);
    REQUIRE(kth_db_dbi_open(db_txn, spend_db_name, KTH_DB_CONDITIONAL_CREATE, &dbi_spend_db_)== KTH_DB_SUCCESS);
    REQUIRE(kth_db_dbi_open(db_txn, transaction_unconfirmed_db_name, KTH_DB_CONDITIONAL_CREATE, &dbi_transaction_unconfirmed_db_) == KTH_DB_SUCCESS);

    REQUIRE(kth_db_txn_commit(db_txn) == KTH_DB_SUCCESS);


    return {env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_, dbi_block_db_, dbi_transaction_db_, dbi_history_db_,dbi_spend_db_, dbi_transaction_hash_db_, dbi_transaction_unconfirmed_db_ };
}

void print_db_entries_count(KTH_DB_env* env_, KTH_DB_dbi& dbi ) {
    KTH_DB_txn *txn;
    kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &txn);

    MDB_stat db_stats;
    auto ret = mdb_stat(txn, dbi, &db_stats);
    if (ret != KTH_DB_SUCCESS) {
        std::println("Error getting entries {}", static_cast<int32_t>(ret));
        kth_db_txn_commit(txn);
        return;
    }

    std::println("Entries: {}", db_stats.ms_entries);
    kth_db_txn_commit(txn);
}

//check_reorg_output(env_, dbi_reorg_pool_, "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6", 0, "00f2052a01000000434104283338ffd784c198147f99aed2cc16709c90b1522e3b3637b312a6f9130e0eda7081e373a96d36be319710cd5c134aaffba81ff08650d7de8af332fe4d8cde20ac");
void check_reorg_output(KTH_DB_env* env_, KTH_DB_dbi& dbi_reorg_pool_, std::string txid_enc, uint32_t pos, std::string output_enc) {
    KTH_DB_txn* db_txn;

    hash_digest txid;
    REQUIRE(decode_hash(txid, txid_enc));
    output_point point{txid, pos};
    auto keyarr = point.to_data(false);
    auto key = kth_db_make_value(keyarr.size(), keyarr.data());
    KTH_DB_val value;

    REQUIRE(kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_get(db_txn, dbi_reorg_pool_, &key, &value) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_txn_commit(db_txn) == KTH_DB_SUCCESS);

    data_chunk data {static_cast<uint8_t*>(kth_db_get_data(value)), static_cast<uint8_t*>(kth_db_get_data(value)) + kth_db_get_size(value)};
    byte_reader reader(data);
    auto output_res = domain::chain::output::from_data(reader, false);
    REQUIRE(output_res);
    auto output = *output_res;

    REQUIRE(encode_base16(output.to_data(true)) == output_enc);
}

void check_reorg_output_just_existence(KTH_DB_env* env_, KTH_DB_dbi& dbi_reorg_pool_, std::string txid_enc, uint32_t pos) {
    KTH_DB_txn* db_txn;

    hash_digest txid;
    REQUIRE(decode_hash(txid, txid_enc));
    output_point point{txid, pos};
    auto keyarr = point.to_data(false);
    auto key = kth_db_make_value(keyarr.size(), keyarr.data());
    KTH_DB_val value;

    REQUIRE(kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_get(db_txn, dbi_reorg_pool_, &key, &value) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_txn_commit(db_txn) == KTH_DB_SUCCESS);
}

void check_reorg_output_doesnt_exists(KTH_DB_env* env_, KTH_DB_dbi& dbi_reorg_pool_, std::string txid_enc, uint32_t pos) {
    KTH_DB_txn* db_txn;

    hash_digest txid;
    REQUIRE(decode_hash(txid, txid_enc));
    output_point point{txid, pos};
    auto keyarr = point.to_data(false);
    auto key = kth_db_make_value(keyarr.size(), keyarr.data());
    KTH_DB_val value;

    REQUIRE(kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_get(db_txn, dbi_reorg_pool_, &key, &value) == KTH_DB_NOTFOUND);
    REQUIRE(kth_db_txn_commit(db_txn) == KTH_DB_SUCCESS);
}

void check_blocks_db_just_existence(KTH_DB_env* env_, KTH_DB_dbi& dbi_blocks_db_, uint32_t height) {
    KTH_DB_txn* db_txn;

    auto key = kth_db_make_value(sizeof(height), &height);
    KTH_DB_val value;

    REQUIRE(kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_get(db_txn, dbi_blocks_db_, &key, &value) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_txn_commit(db_txn) == KTH_DB_SUCCESS);
}

void check_blocks_db_doesnt_exists(KTH_DB_env* env_, KTH_DB_dbi& dbi_blocks_db_, uint32_t height) {
    KTH_DB_txn* db_txn;

    auto key = kth_db_make_value(sizeof(height), &height);
    KTH_DB_val value;

    REQUIRE(kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_get(db_txn, dbi_blocks_db_, &key, &value) == KTH_DB_NOTFOUND);
    REQUIRE(kth_db_txn_commit(db_txn) == KTH_DB_SUCCESS);
}

void check_blocks_db(KTH_DB_env* env_, KTH_DB_dbi& dbi_blocks_db_, uint32_t height) {
    KTH_DB_txn* db_txn;

    auto key = kth_db_make_value(sizeof(height), &height);
    KTH_DB_val value;

    REQUIRE(kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_get(db_txn, dbi_blocks_db_, &key, &value) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_txn_commit(db_txn) == KTH_DB_SUCCESS);

    data_chunk data {static_cast<uint8_t*>(kth_db_get_data(value)), static_cast<uint8_t*>(kth_db_get_data(value)) + kth_db_get_size(value)};
    byte_reader reader(data);
    auto block_res = domain::chain::block::from_data(reader, false);
    REQUIRE(block_res);
    auto block = *block_res;

    REQUIRE(block.is_valid());
}

void check_blocks_db(KTH_DB_env* env_, KTH_DB_dbi& dbi_blocks_db_, KTH_DB_dbi& dbi_block_header_, KTH_DB_dbi& dbi_transaction_db_, uint32_t height) {

    KTH_DB_txn* db_txn;
    REQUIRE(kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn) == KTH_DB_SUCCESS);


    KTH_DB_cursor* cursor;
    REQUIRE(kth_db_cursor_open(db_txn, dbi_blocks_db_, &cursor) == KTH_DB_SUCCESS);


    auto key = kth_db_make_value(sizeof(height), &height);
    domain::chain::transaction::list tx_list;

    KTH_DB_val value;
    int rc;
    REQUIRE(kth_db_cursor_get(cursor, &key, &value, MDB_SET) == KTH_DB_SUCCESS);

    auto tx_id = *static_cast<uint32_t*>(kth_db_get_data(value));

    auto key_tx = kth_db_make_value(sizeof(tx_id), &tx_id);
    KTH_DB_val value_tx;

    REQUIRE(kth_db_get(db_txn, dbi_transaction_db_, &key_tx, &value_tx) == KTH_DB_SUCCESS);

    data_chunk data_tx {static_cast<uint8_t*>(kth_db_get_data(value_tx)), static_cast<uint8_t*>(kth_db_get_data(value_tx)) + kth_db_get_size(value_tx)};
    byte_reader reader_tx(data_tx);
    auto entry_res = transaction_entry::from_data(reader_tx);
    REQUIRE(entry_res);
    auto entry = *entry_res;
    tx_list.push_back(std::move(entry.transaction()));

    while ((rc = kth_db_cursor_get(cursor, &key, &value, MDB_NEXT_DUP)) == 0) {
        auto tx_id = *static_cast<uint32_t*>(kth_db_get_data(value));;
        auto key_tx = kth_db_make_value(sizeof(tx_id), &tx_id);
        KTH_DB_val value_tx;

        REQUIRE(kth_db_get(db_txn, dbi_transaction_db_, &key_tx, &value_tx) == KTH_DB_SUCCESS);

        data_chunk data_tx {static_cast<uint8_t*>(kth_db_get_data(value_tx)), static_cast<uint8_t*>(kth_db_get_data(value_tx)) + kth_db_get_size(value_tx)};
        byte_reader reader_tx_loop(data_tx);
    auto entry_res = transaction_entry::from_data(reader_tx_loop);
    REQUIRE(entry_res);
    auto entry = *entry_res;
        tx_list.push_back(std::move(entry.transaction()));
    }


    kth_db_cursor_close(cursor);

    REQUIRE(kth_db_get(db_txn, dbi_block_header_, &key, &value) == KTH_DB_SUCCESS);

    data_chunk data_header {static_cast<uint8_t*>(kth_db_get_data(value)), static_cast<uint8_t*>(kth_db_get_data(value)) + kth_db_get_size(value)};
    byte_reader reader_header(data_header);
    auto header_res = domain::chain::header::from_data(reader_header);
    REQUIRE(header_res);
    auto header = *header_res;
    REQUIRE(header.is_valid());

    domain::chain::block block{header, std::move(tx_list)};

    REQUIRE(kth_db_txn_commit(db_txn) == KTH_DB_SUCCESS);

    REQUIRE(block.is_valid());
}

void check_transactions_db_just_existence(KTH_DB_env* env_, KTH_DB_dbi& dbi_transaction_db_, uint64_t id) {
    KTH_DB_txn* db_txn;

    auto key = kth_db_make_value(sizeof(id), &id);
    KTH_DB_val value;

    REQUIRE(kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_get(db_txn, dbi_transaction_db_, &key, &value) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_txn_commit(db_txn) == KTH_DB_SUCCESS);
}

void check_transactions_db_doesnt_exists(KTH_DB_env* env_, KTH_DB_dbi& dbi_transaction_db_, uint64_t id) {
    KTH_DB_txn* db_txn;

    auto key = kth_db_make_value(sizeof(id), &id);
    KTH_DB_val value;

    REQUIRE(kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_get(db_txn, dbi_transaction_db_, &key, &value) == KTH_DB_NOTFOUND);
    REQUIRE(kth_db_txn_commit(db_txn) == KTH_DB_SUCCESS);
}

void check_history_db_just_existence(KTH_DB_env* env_, KTH_DB_dbi& dbi_history_db_, short_hash& hash) {
    KTH_DB_txn* db_txn;

    auto key = kth_db_make_value(hash.size(), hash.data());
    KTH_DB_val value;

    REQUIRE(kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_get(db_txn, dbi_history_db_, &key, &value) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_txn_commit(db_txn) == KTH_DB_SUCCESS);
}

void check_history_db_doesnt_exists(KTH_DB_env* env_, KTH_DB_dbi& dbi_history_db_, short_hash& hash) {
    KTH_DB_txn* db_txn;

    auto key = kth_db_make_value(hash.size(), hash.data());
    KTH_DB_val value;

    REQUIRE(kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_get(db_txn, dbi_history_db_, &key, &value) == KTH_DB_NOTFOUND);
    REQUIRE(kth_db_txn_commit(db_txn) == KTH_DB_SUCCESS);
}

void check_reorg_index(KTH_DB_env* env_, KTH_DB_dbi& dbi_reorg_index_, std::string txid_enc, uint32_t pos, uint32_t height) {
    KTH_DB_txn* db_txn;
    KTH_DB_val value;

    auto height_key = kth_db_make_value(sizeof(uint32_t), &height);
    REQUIRE(kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_get(db_txn, dbi_reorg_index_, &height_key, &value) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_txn_commit(db_txn) == KTH_DB_SUCCESS);
    data_chunk data2 {static_cast<uint8_t*>(kth_db_get_data(value)), static_cast<uint8_t*>(kth_db_get_data(value)) + kth_db_get_size(value)};
    byte_reader reader(data2);
    auto point_indexed_res = domain::chain::point::from_data(reader, false);
    REQUIRE(point_indexed_res);
    auto point_indexed = *point_indexed_res;

    hash_digest txid;
    REQUIRE(decode_hash(txid, txid_enc));
    output_point point{txid, pos};

    REQUIRE(point == point_indexed);
}

void check_reorg_index_doesnt_exists(KTH_DB_env* env_, KTH_DB_dbi& dbi_reorg_index_, uint32_t height) {
    KTH_DB_txn* db_txn;
    KTH_DB_val value;

    auto height_key = kth_db_make_value(sizeof(uint32_t), &height);
    REQUIRE(kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_get(db_txn, dbi_reorg_index_, &height_key, &value) == KTH_DB_NOTFOUND);
    REQUIRE(kth_db_txn_commit(db_txn) == KTH_DB_SUCCESS);
}


void check_reorg_block(KTH_DB_env* env_, KTH_DB_dbi& dbi_reorg_block_, uint32_t height, std::string block_enc) {
    KTH_DB_txn* db_txn;

    auto key = kth_db_make_value(sizeof(uint32_t), &height);
    KTH_DB_val value;

    REQUIRE(kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_get(db_txn, dbi_reorg_block_, &key, &value) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_txn_commit(db_txn) == KTH_DB_SUCCESS);

    data_chunk data {static_cast<uint8_t*>(kth_db_get_data(value)), static_cast<uint8_t*>(kth_db_get_data(value)) + kth_db_get_size(value)};
    byte_reader reader(data);
    auto block_res = domain::chain::block::from_data(reader, false);
    REQUIRE(block_res);
    auto block = *block_res;

    REQUIRE(encode_base16(block.to_data(false)) == block_enc);
}

void check_reorg_block_doesnt_exists(KTH_DB_env* env_, KTH_DB_dbi& dbi_reorg_block_, uint32_t height) {
    KTH_DB_txn* db_txn;

    auto key = kth_db_make_value(sizeof(uint32_t), &height);
    KTH_DB_val value;

    REQUIRE(kth_db_txn_begin(env_, NULL, KTH_DB_RDONLY, &db_txn) == KTH_DB_SUCCESS);
    REQUIRE(kth_db_get(db_txn, dbi_reorg_block_, &key, &value) == KTH_DB_NOTFOUND);
    REQUIRE(kth_db_txn_commit(db_txn) == KTH_DB_SUCCESS);
}


size_t db_count_items(KTH_DB_env *env, KTH_DB_dbi dbi) {
    KTH_DB_val key, data;
	KTH_DB_txn *txn;
    KTH_DB_cursor *cursor;
    int rc;

    kth_db_txn_begin(env, NULL, KTH_DB_RDONLY, &txn);
    kth_db_cursor_open(txn, dbi, &cursor);

    size_t count = 0;
    while ((rc = kth_db_cursor_get(cursor, &key, &data, KTH_DB_NEXT)) == 0) {

        data_chunk key_bytes {static_cast<uint8_t*>(kth_db_get_data(key)), static_cast<uint8_t*>(kth_db_get_data(key)) + kth_db_get_size(key)};
        std::reverse(begin(key_bytes), end(key_bytes));
        std::println("{}", encode_base16(key_bytes));

        data_chunk value_bytes {static_cast<uint8_t*>(kth_db_get_data(data)), static_cast<uint8_t*>(kth_db_get_data(data)) + kth_db_get_size(data)};
        std::reverse(begin(value_bytes), end(value_bytes));
        // std::println("src/database/test/internal_database.cpp", encode_base16(value_bytes));

        ++count;
    }
    // std::println("src/database/test/internal_database.cpp", "---------------------------------\n");

    kth_db_cursor_close(cursor);
    kth_db_txn_commit(txn);

    return count;
}

size_t db_count_index_by_height(KTH_DB_env *env, KTH_DB_dbi dbi, size_t height) {

    auto height_key = kth_db_make_value(sizeof(uint32_t), &height);

	KTH_DB_txn *txn;
    kth_db_txn_begin(env, NULL, KTH_DB_RDONLY, &txn);

    KTH_DB_cursor *cursor;
    kth_db_cursor_open(txn, dbi, &cursor);

    size_t count = 0;
    KTH_DB_val data;

    int rc;
    if ((rc = kth_db_cursor_get(cursor, &height_key, &data, MDB_SET)) == 0) {
        ++count;
        while ((rc = kth_db_cursor_get(cursor, &height_key, &data, MDB_NEXT_DUP)) == 0) {
            ++count;
        }
    } else {
        // std::println("src/database/test/internal_database.cpp", "no encontre el primero\n");
    }
    // std::println("src/database/test/internal_database.cpp", "---------------------------------\n");

    kth_db_cursor_close(cursor);
    kth_db_txn_abort(txn);

    return count;
}

size_t db_count_db_by_address(KTH_DB_env *env, KTH_DB_dbi dbi, domain::wallet::payment_address const& address) {
    auto hash = address.hash20();
    auto key = kth_db_make_value(hash.size(), hash.data());

	KTH_DB_txn *txn;
    kth_db_txn_begin(env, NULL, KTH_DB_RDONLY, &txn);

    KTH_DB_cursor *cursor;
    kth_db_cursor_open(txn, dbi, &cursor);

    size_t count = 0;
    KTH_DB_val data;
    int rc;
    if ((rc = kth_db_cursor_get(cursor, &key, &data, MDB_SET)) == 0) {
        ++count;
        while ((rc = kth_db_cursor_get(cursor, &key, &data, MDB_NEXT_DUP)) == 0) {
            ++count;
        }
    } else {
        // std::println("src/database/test/internal_database.cpp", "no encontre el primero\n");
    }
    // std::println("src/database/test/internal_database.cpp", "---------------------------------\n");

    kth_db_cursor_close(cursor);
    kth_db_txn_abort(txn);

    return count;
}


bool db_exists_height(KTH_DB_env *env, KTH_DB_dbi dbi, size_t height) {
	KTH_DB_txn *txn;

    auto height_key = kth_db_make_value(sizeof(uint32_t), &height);
    KTH_DB_val value;

    auto xxx = kth_db_txn_begin(env, NULL, KTH_DB_RDONLY, &txn);
    xxx = kth_db_get(txn, dbi, &height_key, &value);
    bool res = xxx == KTH_DB_SUCCESS;
    kth_db_txn_abort(txn);

    return res;
}


void check_index_and_pool(KTH_DB_env *env, KTH_DB_dbi& dbi_index, KTH_DB_dbi& dbi_pool) {
    KTH_DB_val key, data;
    KTH_DB_val value;
	KTH_DB_txn *txn;
    KTH_DB_cursor *cursor;
    int rc;

    kth_db_txn_begin(env, NULL, KTH_DB_RDONLY, &txn);
    kth_db_cursor_open(txn, dbi_index, &cursor);

    while ((rc = kth_db_cursor_get(cursor, &key, &data, KTH_DB_NEXT)) == 0) {
        REQUIRE(kth_db_get(txn, dbi_pool, &data, &value) == KTH_DB_SUCCESS);
    }

    kth_db_cursor_close(cursor);
    kth_db_txn_abort(txn);
}

template <size_t Secs>
struct dummy_clock {
    using duration = std::chrono::system_clock::duration;
    static
    std::chrono::time_point<dummy_clock> now() noexcept {
        return std::chrono::time_point<dummy_clock>(duration(std::chrono::seconds(Secs)));
    }
};

// ---------------------------------------------------------------------------------

// BOOST_FIXTURE_TEST_SUITE(internal_db_tests, internal_database_directory_setup_fixture)

TEST_CASE("internal database  dummy clock", "[None]") {
    auto start = dummy_clock<200>::now();
    auto end = dummy_clock<200>::now();
    // std::println("{}us.", std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    REQUIRE(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() == 0);
}

TEST_CASE("internal database  adjust db size", "[None]") {
    internal_database db(db_path, db_mode_type::full, 10000000, 1, true);
    REQUIRE(db.open());
}

TEST_CASE("internal database  open", "[None]") {
    internal_database db(db_path, db_mode_type::full, 10000000, db_size, true);
    REQUIRE(db.open());
}


TEST_CASE("internal database  test get all transaction unconfirmed", "[None]") {
    internal_database db(db_path, db_mode_type::full, 10000000, db_size, true);
    db.open();
    auto ret = db.get_all_transaction_unconfirmed();
}

TEST_CASE("internal database  insert genesis", "[None]") {
    auto const genesis = get_genesis();

    internal_database db(db_path, db_mode_type::full, 10000000, db_size, true);
    REQUIRE(db.open());
    REQUIRE(db.push_block(genesis, 0, 1) == result_code::success);

    REQUIRE(db.get_header(genesis.hash()).first.is_valid());
    REQUIRE(db.get_header(genesis.hash()).first.hash() == genesis.hash());
    REQUIRE(db.get_header(genesis.hash()).second == 0);
    REQUIRE(db.get_header(0).is_valid());
    REQUIRE(db.get_header(0).hash() == genesis.hash());


    REQUIRE(db.get_block(0).header().hash() == genesis.hash());

    hash_digest txid;
    std::string txid_enc = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b";
    REQUIRE(decode_hash(txid, txid_enc));

    auto entry = db.get_utxo(output_point{txid, 0});
    REQUIRE(entry.is_valid());

    REQUIRE(entry.height() == 0);
    REQUIRE(entry.median_time_past() == 1);
    REQUIRE(entry.coinbase());

    auto output = entry.output();
    REQUIRE(output.is_valid());



    auto const& tx = db.get_transaction(txid, max_uint32);
    REQUIRE(tx.is_valid());


    auto const& address = domain::wallet::payment_address("1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa");
    REQUIRE(address);

    auto history_list = db.get_history(address.hash20(),max_uint32,0);
    REQUIRE(history_list.size() == 1);

    auto history_item = history_list[0];

    REQUIRE(history_item.kind == point_kind::output);
    REQUIRE(history_item.point.hash() == txid);
    REQUIRE(history_item.point.index() == 0);
    REQUIRE(history_item.height == 0);
    REQUIRE(history_item.value == 5000000000);

    std::string output_enc = "00f2052a01000000434104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac";
    REQUIRE(encode_base16(output.to_data(true)) == output_enc);
}

TEST_CASE("internal database  insert duplicate block", "[None]") {
    auto const genesis = get_genesis();

    internal_database db(db_path, db_mode_type::full, 10000000, db_size, true);
    REQUIRE(db.open());
    auto res = db.push_block(genesis, 0, 1);

    REQUIRE(res == result_code::success);
    REQUIRE(succeed(res));

    res = db.push_block(genesis, 0, 1);
    REQUIRE(res == result_code::duplicated_key);
    REQUIRE( ! succeed(res));
}

TEST_CASE("internal database  insert block genesis duplicate", "[None]") {
    auto const genesis = get_genesis();

    internal_database db(db_path, db_mode_type::full, 10000000, db_size, true);
    REQUIRE(db.open());
    auto res = db.push_genesis(genesis);

    REQUIRE(res == result_code::success);
    REQUIRE(succeed(res));

    res = db.push_genesis(genesis);
    REQUIRE(res == result_code::duplicated_key);
    REQUIRE( ! succeed(res));
}



TEST_CASE("internal database  insert block genesis and get", "[None]") {
    auto const genesis = get_genesis();

    internal_database db(db_path, db_mode_type::full, 10000000, db_size, true);
    REQUIRE(db.open());
    auto res = db.push_genesis(genesis);

    REQUIRE(res == result_code::success);
    REQUIRE(succeed(res));

    auto const block = db.get_block(0);


    auto const& m = block.generate_merkle_root();
    auto const& m2 = block.header().merkle();

    REQUIRE(block.is_valid_merkle_root() == true);
}

TEST_CASE("internal database  insert block genesis and get transaction", "[None]") {
    auto const genesis = get_genesis();

    internal_database db(db_path, db_mode_type::full, 10000000, db_size, true);
    REQUIRE(db.open());
    auto res = db.push_genesis(genesis);

    REQUIRE(res == result_code::success);
    REQUIRE(succeed(res));



    hash_digest txid;
    auto txid_enc = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b";
    REQUIRE(decode_hash(txid, txid_enc));

    auto const tx2 = db.get_transaction(txid,max_size_t);
    REQUIRE(tx2.is_valid() == true);
}

TEST_CASE("internal database  insert duplicate block by hash", "[None]") {
    auto const genesis = get_genesis();

    internal_database db(db_path, db_mode_type::full, 10000000, db_size, true);
    REQUIRE(db.open());
    auto res = db.push_block(genesis, 0, 1);

    REQUIRE(res == result_code::success);
    REQUIRE(succeed(res));

    res = db.push_block(genesis, 1, 1);
    REQUIRE(res == result_code::duplicated_key);
    REQUIRE( ! succeed(res));
}


TEST_CASE("internal database  insert success duplicate coinbase", "[None]") {
    auto const genesis = get_genesis();
    auto const fake = get_fake_genesis();

    internal_database db(db_path, db_mode_type::full, 10000000, db_size, true);
    REQUIRE(db.open());
    auto res = db.push_block(genesis, 0, 1);

    REQUIRE(res == result_code::success);
    REQUIRE(succeed(res));

    res = db.push_block(fake, 1, 1);
    REQUIRE(res == result_code::success_duplicate_coinbase);
    REQUIRE(succeed(res));
}

TEST_CASE("internal database  key not found", "[None]") {
    auto const spender = get_block("01000000ba8b9cda965dd8e536670f9ddec10e53aab14b20bacad27b9137190000000000190760b278fe7b8565fda3b968b918d5fd997f993b23674c0af3b6fde300b38f33a5914ce6ed5b1b01e32f570201000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b014effffffff0100f2052a01000000434104b68a50eaa0287eff855189f949c1c6e5f58b37c88231373d8a59809cbae83059cc6469d65c665ccfd1cfeb75c6e8e19413bba7fbff9bc762419a76d87b16086eac000000000100000001a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f5000000004948304502206e21798a42fae0e854281abd38bacd1aeed3ee3738d9e1446618c4571d1090db022100e2ac980643b0b82c0e88ffdfec6b64e3e6ba35e7ba5fdd7d5d6cc8d25c6b241501ffffffff0100f2052a010000001976a914404371705fa9bd789a2fcd52d2c580b65d35549d88ac00000000");
    internal_database db(db_path, db_mode_type::full, 10000000, db_size, true);
    REQUIRE(db.open());
    REQUIRE(db.push_block(spender, 1, 1) == result_code::key_not_found);
}

TEST_CASE("internal database  insert duplicate", "[None]") {
    auto const orig = get_block("01000000a594fda9d85f69e762e498650d6fdb54d838657cea7841915203170000000000a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f505da904ce6ed5b1b017fe8070101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b015cffffffff0100f2052a01000000434104283338ffd784c198147f99aed2cc16709c90b1522e3b3637b312a6f9130e0eda7081e373a96d36be319710cd5c134aaffba81ff08650d7de8af332fe4d8cde20ac00000000");
    auto const spender = get_block("01000000ba8b9cda965dd8e536670f9ddec10e53aab14b20bacad27b9137190000000000190760b278fe7b8565fda3b968b918d5fd997f993b23674c0af3b6fde300b38f33a5914ce6ed5b1b01e32f570201000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b014effffffff0100f2052a01000000434104b68a50eaa0287eff855189f949c1c6e5f58b37c88231373d8a59809cbae83059cc6469d65c665ccfd1cfeb75c6e8e19413bba7fbff9bc762419a76d87b16086eac000000000100000001a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f5000000004948304502206e21798a42fae0e854281abd38bacd1aeed3ee3738d9e1446618c4571d1090db022100e2ac980643b0b82c0e88ffdfec6b64e3e6ba35e7ba5fdd7d5d6cc8d25c6b241501ffffffff0100f2052a010000001976a914404371705fa9bd789a2fcd52d2c580b65d35549d88ac00000000");
    // std::println("src/database/test/internal_database.cpp", encode_hash(orig.hash()));
    // std::println("src/database/test/internal_database.cpp", encode_hash(spender.hash()));

    internal_database db(db_path, db_mode_type::full, 10000000, db_size, true);
    REQUIRE(db.open());
    REQUIRE(db.push_block(orig, 0, 1) == result_code::success);
    REQUIRE(db.push_block(spender, 1, 1) == result_code::success);
    REQUIRE(db.push_block(spender, 1, 1) == result_code::duplicated_key);
}

TEST_CASE("internal database  insert double spend block", "[None]") {
    auto const orig = get_block("01000000a594fda9d85f69e762e498650d6fdb54d838657cea7841915203170000000000a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f505da904ce6ed5b1b017fe8070101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b015cffffffff0100f2052a01000000434104283338ffd784c198147f99aed2cc16709c90b1522e3b3637b312a6f9130e0eda7081e373a96d36be319710cd5c134aaffba81ff08650d7de8af332fe4d8cde20ac00000000");
    auto const spender0 = get_block("01000000ba8b9cda965dd8e536670f9ddec10e53aab14b20bacad27b9137190000000000190760b278fe7b8565fda3b968b918d5fd997f993b23674c0af3b6fde300b38f33a5914ce6ed5b1b01e32f570201000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b014effffffff0100f2052a01000000434104b68a50eaa0287eff855189f949c1c6e5f58b37c88231373d8a59809cbae83059cc6469d65c665ccfd1cfeb75c6e8e19413bba7fbff9bc762419a76d87b16086eac000000000100000001a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f5000000004948304502206e21798a42fae0e854281abd38bacd1aeed3ee3738d9e1446618c4571d1090db022100e2ac980643b0b82c0e88ffdfec6b64e3e6ba35e7ba5fdd7d5d6cc8d25c6b241501ffffffff0100f2052a010000001976a914404371705fa9bd789a2fcd52d2c580b65d35549d88ac00000000");
    auto const spender1 = get_block("02000000ba8b9cda965dd8e536670f9ddec10e53aab14b20bacad27b9137190000000000190760b278fe7b8565fda3b968b918d5fd997f993b23674c0af3b6fde300b38f33a5914ce6ed5b1b01e32f570201000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b014effffffff0100f2052a01000000434104b68a50eaa0287eff855189f949c1c6e5f58b37c88231373d8a59809cbae83059cc6469d65c665ccfd1cfeb75c6e8e19413bba7fbff9bc762419a76d87b16086eac000000000200000001a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f5000000004948304502206e21798a42fae0e854281abd38bacd1aeed3ee3738d9e1446618c4571d1090db022100e2ac980643b0b82c0e88ffdfec6b64e3e6ba35e7ba5fdd7d5d6cc8d25c6b241501ffffffff0100f2052a010000001976a914404371705fa9bd789a2fcd52d2c580b65d35549d88ac00000000");

    // std::println("src/database/test/internal_database.cpp", encode_hash(orig.hash()));
    // std::println("src/database/test/internal_database.cpp", encode_hash(spender.hash()));

    internal_database db(db_path, db_mode_type::full, 10000000, db_size, true);
    REQUIRE(db.open());
    REQUIRE(db.push_block(orig, 0, 1) == result_code::success);
    REQUIRE(db.push_block(spender0, 1, 1) == result_code::success);
    REQUIRE(db.push_block(spender1, 2, 1) == result_code::key_not_found);
}

TEST_CASE("internal database  spend", "[None]") {
    //79880
    auto const orig = get_block("01000000a594fda9d85f69e762e498650d6fdb54d838657cea7841915203170000000000a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f505da904ce6ed5b1b017fe8070101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b015cffffffff0100f2052a01000000434104283338ffd784c198147f99aed2cc16709c90b1522e3b3637b312a6f9130e0eda7081e373a96d36be319710cd5c134aaffba81ff08650d7de8af332fe4d8cde20ac00000000");
    //80000
    auto const spender = get_block("01000000ba8b9cda965dd8e536670f9ddec10e53aab14b20bacad27b9137190000000000190760b278fe7b8565fda3b968b918d5fd997f993b23674c0af3b6fde300b38f33a5914ce6ed5b1b01e32f570201000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b014effffffff0100f2052a01000000434104b68a50eaa0287eff855189f949c1c6e5f58b37c88231373d8a59809cbae83059cc6469d65c665ccfd1cfeb75c6e8e19413bba7fbff9bc762419a76d87b16086eac000000000100000001a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f5000000004948304502206e21798a42fae0e854281abd38bacd1aeed3ee3738d9e1446618c4571d1090db022100e2ac980643b0b82c0e88ffdfec6b64e3e6ba35e7ba5fdd7d5d6cc8d25c6b241501ffffffff0100f2052a010000001976a914404371705fa9bd789a2fcd52d2c580b65d35549d88ac00000000");

    internal_database db(db_path, db_mode_type::full, 10000000, db_size, true);
    REQUIRE(db.open());
    REQUIRE(db.push_block(orig, 0, 1) == result_code::success);

    hash_digest txid;
    std::string txid_enc = "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6";
    REQUIRE(decode_hash(txid, txid_enc));
    auto entry = db.get_utxo(output_point{txid, 0});
    REQUIRE(entry.is_valid());

    REQUIRE(entry.height() == 0);
    REQUIRE(entry.median_time_past() == 1);
    REQUIRE(entry.coinbase());


    auto output = entry.output();
    REQUIRE(output.is_valid());

    std::string output_enc = "00f2052a01000000434104283338ffd784c198147f99aed2cc16709c90b1522e3b3637b312a6f9130e0eda7081e373a96d36be319710cd5c134aaffba81ff08650d7de8af332fe4d8cde20ac";
    REQUIRE(encode_base16(output.to_data(true)) == output_enc);


    // --------------------------------------------------------------------------------------------------------------------------
    REQUIRE(db.push_block(spender, 1, 1) == result_code::success);

    entry = db.get_utxo(output_point{txid, 0});
    output = entry.output();
    REQUIRE( !  output.is_valid());

    txid_enc = "c06fbab289f723c6261d3030ddb6be121f7d2508d77862bb1e484f5cd7f92b25";
    REQUIRE(decode_hash(txid, txid_enc));
    entry = db.get_utxo(output_point{txid, 0});
    REQUIRE(entry.is_valid());
    REQUIRE(entry.height() == 1);
    REQUIRE(entry.median_time_past() == 1);
    REQUIRE(entry.coinbase());

    output = entry.output();
    REQUIRE(output.is_valid());
    output_enc = "00f2052a01000000434104b68a50eaa0287eff855189f949c1c6e5f58b37c88231373d8a59809cbae83059cc6469d65c665ccfd1cfeb75c6e8e19413bba7fbff9bc762419a76d87b16086eac";
    REQUIRE(encode_base16(output.to_data(true)) == output_enc);

    txid_enc = "5a4ebf66822b0b2d56bd9dc64ece0bc38ee7844a23ff1d7320a88c5fdb2ad3e2";
    REQUIRE(decode_hash(txid, txid_enc));
    entry = db.get_utxo(output_point{txid, 0});
    REQUIRE(entry.is_valid());
    REQUIRE(entry.height() == 1);
    REQUIRE(entry.median_time_past() == 1);
    REQUIRE( ! entry.coinbase());


    output = entry.output();
    REQUIRE(output.is_valid());
    output_enc = "00f2052a010000001976a914404371705fa9bd789a2fcd52d2c580b65d35549d88ac";
    REQUIRE(encode_base16(output.to_data(true)) == output_enc);
}


TEST_CASE("internal database  reorg", "[None]") {
    //79880 - 00000000002e872c6fbbcf39c93ef0d89e33484ebf457f6829cbf4b561f3af5a
    std::string orig_enc = "01000000a594fda9d85f69e762e498650d6fdb54d838657cea7841915203170000000000a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f505da904ce6ed5b1b017fe8070101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b015cffffffff0100f2052a01000000434104283338ffd784c198147f99aed2cc16709c90b1522e3b3637b312a6f9130e0eda7081e373a96d36be319710cd5c134aaffba81ff08650d7de8af332fe4d8cde20ac00000000";
    auto const orig = get_block(orig_enc);
    //80000 - 000000000043a8c0fd1d6f726790caa2a406010d19efd2780db27bdbbd93baf6
    std::string spender_enc = "01000000ba8b9cda965dd8e536670f9ddec10e53aab14b20bacad27b9137190000000000190760b278fe7b8565fda3b968b918d5fd997f993b23674c0af3b6fde300b38f33a5914ce6ed5b1b01e32f570201000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b014effffffff0100f2052a01000000434104b68a50eaa0287eff855189f949c1c6e5f58b37c88231373d8a59809cbae83059cc6469d65c665ccfd1cfeb75c6e8e19413bba7fbff9bc762419a76d87b16086eac000000000100000001a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f5000000004948304502206e21798a42fae0e854281abd38bacd1aeed3ee3738d9e1446618c4571d1090db022100e2ac980643b0b82c0e88ffdfec6b64e3e6ba35e7ba5fdd7d5d6cc8d25c6b241501ffffffff0100f2052a010000001976a914404371705fa9bd789a2fcd52d2c580b65d35549d88ac00000000";
    auto const spender = get_block(spender_enc);

    {
        internal_database db(db_path, db_mode_type::full, 10000000, db_size, true);
        REQUIRE(db.open());
        REQUIRE(db.push_block(orig, 0, 1) == result_code::success);
        REQUIRE(db.push_block(spender, 1, 1) == result_code::success);
    }   //close() implicit

    KTH_DB_env* env_;
    KTH_DB_dbi dbi_utxo_;
    KTH_DB_dbi dbi_reorg_pool_;
    KTH_DB_dbi dbi_reorg_index_;
    KTH_DB_dbi dbi_block_header_;
    KTH_DB_dbi dbi_block_header_by_hash_;
    KTH_DB_dbi dbi_reorg_block_;
    KTH_DB_dbi dbi_block_db_;
    KTH_DB_dbi dbi_transaction_db_;
    KTH_DB_dbi dbi_transaction_hash_db_;
    KTH_DB_dbi dbi_transaction_unconfirmed_db_;
    KTH_DB_dbi dbi_history_db_;
    KTH_DB_dbi dbi_spend_db_;

    //KTH_DB_txn* db_txn;
    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_
    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    check_reorg_output(env_, dbi_reorg_pool_, "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6", 0, "00f2052a01000000434104283338ffd784c198147f99aed2cc16709c90b1522e3b3637b312a6f9130e0eda7081e373a96d36be319710cd5c134aaffba81ff08650d7de8af332fe4d8cde20ac");
    check_reorg_index(env_, dbi_reorg_index_, "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6", 0, 1);

    check_reorg_block(env_, dbi_reorg_block_, 0, orig_enc);
    check_reorg_block(env_, dbi_reorg_block_, 1, spender_enc);


    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_ ,  0);
    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_, 1);
    auto const& address = domain::wallet::payment_address("1JBSCVF6VM6QjFZyTnbpLjoCJTQEqVbepG");
    REQUIRE(address);
    REQUIRE(db_count_db_by_address(env_, dbi_history_db_, address) == 2);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_
        , dbi_block_db_
        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
        , dbi_transaction_unconfirmed_db_
    );
}


TEST_CASE("internal database  old blocks 0", "[None]") {
    //79880 - 00000000002e872c6fbbcf39c93ef0d89e33484ebf457f6829cbf4b561f3af5a
    // timestamp = 1284561413
    //             Sep 15, 2010 11:36:53 AM
    std::string orig_enc = "01000000a594fda9d85f69e762e498650d6fdb54d838657cea7841915203170000000000a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f505da904ce6ed5b1b017fe8070101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b015cffffffff0100f2052a01000000434104283338ffd784c198147f99aed2cc16709c90b1522e3b3637b312a6f9130e0eda7081e373a96d36be319710cd5c134aaffba81ff08650d7de8af332fe4d8cde20ac00000000";
    auto const orig = get_block(orig_enc);

    //80000 - 000000000043a8c0fd1d6f726790caa2a406010d19efd2780db27bdbbd93baf6
    // timestamp = 1284613427
    //             Sep 16, 2010 2:03:47 AM
    std::string spender_enc = "01000000ba8b9cda965dd8e536670f9ddec10e53aab14b20bacad27b9137190000000000190760b278fe7b8565fda3b968b918d5fd997f993b23674c0af3b6fde300b38f33a5914ce6ed5b1b01e32f570201000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b014effffffff0100f2052a01000000434104b68a50eaa0287eff855189f949c1c6e5f58b37c88231373d8a59809cbae83059cc6469d65c665ccfd1cfeb75c6e8e19413bba7fbff9bc762419a76d87b16086eac000000000100000001a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f5000000004948304502206e21798a42fae0e854281abd38bacd1aeed3ee3738d9e1446618c4571d1090db022100e2ac980643b0b82c0e88ffdfec6b64e3e6ba35e7ba5fdd7d5d6cc8d25c6b241501ffffffff0100f2052a010000001976a914404371705fa9bd789a2fcd52d2c580b65d35549d88ac00000000";
    auto const spender = get_block(spender_enc);

    using my_clock = dummy_clock<1284613427>;

    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 86, db_size, true); // 1 to 86 no entra el primero
        REQUIRE(db.open());
        REQUIRE(db.push_block(orig, 0, 1) == result_code::success);
        REQUIRE(db.push_block(spender, 1, 1) == result_code::success);
    }   //close() implicit

    KTH_DB_env* env_;
    KTH_DB_dbi dbi_utxo_;
    KTH_DB_dbi dbi_reorg_pool_;
    KTH_DB_dbi dbi_reorg_index_;
    KTH_DB_dbi dbi_block_header_;
    KTH_DB_dbi dbi_block_header_by_hash_;
    KTH_DB_dbi dbi_reorg_block_;
    KTH_DB_dbi dbi_block_db_;
    KTH_DB_dbi dbi_transaction_db_;
    KTH_DB_dbi dbi_transaction_hash_db_;
    KTH_DB_dbi dbi_transaction_unconfirmed_db_;
    KTH_DB_dbi dbi_history_db_;
    KTH_DB_dbi dbi_spend_db_;

    //KTH_DB_txn* db_txn;
    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_
    , dbi_block_db_
    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    check_reorg_output(env_, dbi_reorg_pool_, "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6", 0, "00f2052a01000000434104283338ffd784c198147f99aed2cc16709c90b1522e3b3637b312a6f9130e0eda7081e373a96d36be319710cd5c134aaffba81ff08650d7de8af332fe4d8cde20ac");
    check_reorg_index(env_, dbi_reorg_index_, "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6", 0, 1);

    check_reorg_block_doesnt_exists(env_, dbi_reorg_block_, 0);
    check_reorg_block(env_, dbi_reorg_block_, 1, spender_enc);


    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_ ,  0);
    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_, 1);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_
        , dbi_block_db_
        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
        , dbi_transaction_unconfirmed_db_
    );
}

TEST_CASE("internal database  old blocks 1", "[None]") {
    //79880 - 00000000002e872c6fbbcf39c93ef0d89e33484ebf457f6829cbf4b561f3af5a
    // timestamp = 1284561413
    //             Sep 15, 2010 11:36:53 AM
    std::string orig_enc = "01000000a594fda9d85f69e762e498650d6fdb54d838657cea7841915203170000000000a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f505da904ce6ed5b1b017fe8070101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b015cffffffff0100f2052a01000000434104283338ffd784c198147f99aed2cc16709c90b1522e3b3637b312a6f9130e0eda7081e373a96d36be319710cd5c134aaffba81ff08650d7de8af332fe4d8cde20ac00000000";
    auto const orig = get_block(orig_enc);

    //80000 - 000000000043a8c0fd1d6f726790caa2a406010d19efd2780db27bdbbd93baf6
    // timestamp = 1284613427
    //             Sep 16, 2010 2:03:47 AM
    std::string spender_enc = "01000000ba8b9cda965dd8e536670f9ddec10e53aab14b20bacad27b9137190000000000190760b278fe7b8565fda3b968b918d5fd997f993b23674c0af3b6fde300b38f33a5914ce6ed5b1b01e32f570201000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b014effffffff0100f2052a01000000434104b68a50eaa0287eff855189f949c1c6e5f58b37c88231373d8a59809cbae83059cc6469d65c665ccfd1cfeb75c6e8e19413bba7fbff9bc762419a76d87b16086eac000000000100000001a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f5000000004948304502206e21798a42fae0e854281abd38bacd1aeed3ee3738d9e1446618c4571d1090db022100e2ac980643b0b82c0e88ffdfec6b64e3e6ba35e7ba5fdd7d5d6cc8d25c6b241501ffffffff0100f2052a010000001976a914404371705fa9bd789a2fcd52d2c580b65d35549d88ac00000000";
    auto const spender = get_block(spender_enc);

    using my_clock = dummy_clock<1284613427>;

    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 87, db_size, true);
        REQUIRE(db.open());
        REQUIRE(db.push_block(orig, 0, 1) == result_code::success);
        REQUIRE(db.push_block(spender, 1, 1) == result_code::success);
    }   //close() implicit

    KTH_DB_env* env_;
    KTH_DB_dbi dbi_utxo_;
    KTH_DB_dbi dbi_reorg_pool_;
    KTH_DB_dbi dbi_reorg_index_;
    KTH_DB_dbi dbi_block_header_;
    KTH_DB_dbi dbi_block_header_by_hash_;
    KTH_DB_dbi dbi_reorg_block_;
    KTH_DB_dbi dbi_block_db_;
    KTH_DB_dbi dbi_transaction_db_;
    KTH_DB_dbi dbi_transaction_hash_db_;
    KTH_DB_dbi dbi_transaction_unconfirmed_db_;
    KTH_DB_dbi dbi_history_db_;
    KTH_DB_dbi dbi_spend_db_;

    //KTH_DB_txn* db_txn;
    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_
    , dbi_block_db_
    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    check_reorg_output(env_, dbi_reorg_pool_, "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6", 0, "00f2052a01000000434104283338ffd784c198147f99aed2cc16709c90b1522e3b3637b312a6f9130e0eda7081e373a96d36be319710cd5c134aaffba81ff08650d7de8af332fe4d8cde20ac");
    check_reorg_index(env_, dbi_reorg_index_, "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6", 0, 1);

    check_reorg_block(env_, dbi_reorg_block_, 0, orig_enc);
    check_reorg_block(env_, dbi_reorg_block_, 1, spender_enc);


    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_ ,  0);
    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_, 1);



    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_
        , dbi_block_db_
        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
        , dbi_transaction_unconfirmed_db_
    );
}


TEST_CASE("internal database  old blocks 2", "[None]") {
    //79880 - 00000000002e872c6fbbcf39c93ef0d89e33484ebf457f6829cbf4b561f3af5a
    // timestamp = 1284561413
    //             Sep 15, 2010 11:36:53 AM
    std::string orig_enc = "01000000a594fda9d85f69e762e498650d6fdb54d838657cea7841915203170000000000a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f505da904ce6ed5b1b017fe8070101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b015cffffffff0100f2052a01000000434104283338ffd784c198147f99aed2cc16709c90b1522e3b3637b312a6f9130e0eda7081e373a96d36be319710cd5c134aaffba81ff08650d7de8af332fe4d8cde20ac00000000";
    auto const orig = get_block(orig_enc);

    //80000 - 000000000043a8c0fd1d6f726790caa2a406010d19efd2780db27bdbbd93baf6
    // timestamp = 1284613427
    //             Sep 16, 2010 2:03:47 AM
    std::string spender_enc = "01000000ba8b9cda965dd8e536670f9ddec10e53aab14b20bacad27b9137190000000000190760b278fe7b8565fda3b968b918d5fd997f993b23674c0af3b6fde300b38f33a5914ce6ed5b1b01e32f570201000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b014effffffff0100f2052a01000000434104b68a50eaa0287eff855189f949c1c6e5f58b37c88231373d8a59809cbae83059cc6469d65c665ccfd1cfeb75c6e8e19413bba7fbff9bc762419a76d87b16086eac000000000100000001a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f5000000004948304502206e21798a42fae0e854281abd38bacd1aeed3ee3738d9e1446618c4571d1090db022100e2ac980643b0b82c0e88ffdfec6b64e3e6ba35e7ba5fdd7d5d6cc8d25c6b241501ffffffff0100f2052a010000001976a914404371705fa9bd789a2fcd52d2c580b65d35549d88ac00000000";
    auto const spender = get_block(spender_enc);

    using my_clock = dummy_clock<1284613427 + 600>;

    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 1, db_size, true);
        REQUIRE(db.open());
        REQUIRE(db.push_block(orig, 0, 1) == result_code::success);
        REQUIRE(db.push_block(spender, 1, 1) == result_code::success);
    }   //close() implicit

    KTH_DB_env* env_;
    KTH_DB_dbi dbi_utxo_;
    KTH_DB_dbi dbi_reorg_pool_;
    KTH_DB_dbi dbi_reorg_index_;
    KTH_DB_dbi dbi_block_header_;
    KTH_DB_dbi dbi_block_header_by_hash_;
    KTH_DB_dbi dbi_reorg_block_;
    KTH_DB_dbi dbi_block_db_;
    KTH_DB_dbi dbi_transaction_db_;
    KTH_DB_dbi dbi_transaction_hash_db_;
    KTH_DB_dbi dbi_transaction_unconfirmed_db_;
    KTH_DB_dbi dbi_history_db_;
    KTH_DB_dbi dbi_spend_db_;

    //KTH_DB_txn* db_txn;
    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_
    , dbi_block_db_
    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    check_reorg_output_doesnt_exists(env_, dbi_reorg_pool_, "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6", 0);
    check_reorg_index_doesnt_exists(env_, dbi_reorg_index_, 1);

    check_reorg_block_doesnt_exists(env_, dbi_reorg_block_, 0);
    check_reorg_block_doesnt_exists(env_, dbi_reorg_block_, 1);
    // check_reorg_block(env_, dbi_reorg_block_, 1, spender_enc);


    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_ ,  0);
    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_, 1);


    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_
        , dbi_block_db_
        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );
}


TEST_CASE("internal database  reorg index", "[None]") {

    // Block #4334
    // BlockHash 000000009cdad3c55df9c9bc88265329254a6c8ca810fa7f0e953c947df86dc7
    auto const b0 = get_block("010000005a0aeca67f7e43582c2b0138b2daa0c8dc1bedbb2477cfba2d3f96bf0000000065dabdbdb83e9820e4f666f3634d88308909789f7ae29e730812784a96485e3cd5899749ffff001dc2544c010101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0804ffff001d028e00ffffffff0100f2052a01000000434104b48f20398caaf3ff5d40710e0af87a4b86fd19d125ddacf15a2a023831d1731350e5fd40d0e28bb6481ad1843847213764feb98a2dd041069a8c39c842e1da93ac00000000");
    // Block #4966
    // BlockHash 000000004f6a440a95a5d2d6c89f3e6b46587cd43f76efbaa96ef5d37ea90961
    auto const b1 = get_block("010000002cba11cca7170e1da742fcfb83d15c091bac17a79c089e944dda904a00000000b7404d6a9c451a9f527a7fbeb54839c2bca2eac7b138cdd700be19d733efa0fc82609e49ffff001df66339030101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d0121ffffffff0100f2052a01000000434104c20502b45fe276d418a32b55435cb4361dea4e173c36a8e0ad52518b17f2d48cde4336c8ac8d2270e6040469f11c56036db1feef803fb529e36d0f599261cb19ac00000000");
    // Block #4561
    // BlockHash 0000000089abf237d732a1515f7066e7ba29e1833664da8b704c8575c0465223
    auto const b2 = get_block("0100000052df1ff74876f2de37db341e12f932768f2a18dcc09dd024a1e676aa00000000cc07817ab589551d698ba7eb2a6efd6670d6951792ad52e2bd5832bf2f4930ecb5f19949ffff001d4045c6010101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0804ffff001d029f00ffffffff0100f2052a010000004341044bdc62d08cc074664cef09b24b01125a5e92fc2d61f821d6fddac367eb80b06a0a6148feabb0d25717f9eb84950ef0d3e7fe49ce7fb5a6e14da881a5b2bc64c0ac00000000");
    // Block #4991
    // BlockHash 00000000fa413253e1d30ff687f239b528330c810e3de86e42a538175682599d
    auto const b3 = get_block("0100000040eb019191a99f1f3ef5e04606314d80a635e214ca3347388259ad4000000000f61fefef8ee758b273ee64e1bf5c07485dd74cd065a5ce0d59827e0700cad0d98c9f9e49ffff001d20a92f010101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d013affffffff0100f2052a01000000434104debec4c8f781d5223a90aa90416ee51abf15d37086cc2d9be67873534fb06ae68c8a4e0ed0a7eedff9c97fe23f03e652d7f44501286dc1f75148bfaa9e386300ac00000000");
    // Block #4556
    // BlockHash 0000000054d4f171b0eab3cd4e31da4ce5a1a06f27b39bf36c5902c9bb8ef5c4
    auto const b4 = get_block("0100000021a06106f13b4f0112a63e77fae3a48ffe10716ff3cdfb35371032990000000015327dc99375fc1fdc02e15394369daa6e23ad4dc27e7c1c4af21606add5b068dadf9949ffff001dda8444000101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0804ffff001d029600ffffffff0100f2052a0100000043410475804bd8be2560ab2ebd170228429814d60e58d7415a93dc51f23e4cb2f8b5188dd56fa8d519e9ef9c16d6ab22c1c8304e10e74a28444eb26821948b2d1482a4ac00000000");

    // Block #5217
    // BlockHash 0000000025075f093c42a0393c844bc59f90024b18a9f588f6fa3fc37487c3c2
    auto const spender0 = get_block("01000000944bb801c604dda3d51758f292afdca8d973288434c8fe4bf0b5982d000000008a7d204ffe05282b05f280459401b59be41b089cefc911f4fb5641f90309d942b929a149ffff001d1b8d847f0301000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0804ffff001d02c500ffffffff0100f2052a01000000434104f4af426e464d972012256f4cbe5df528aa99b1ceb489968a56cf6b295e6fad1473be89f66fbd3d16adf3dfba7c5253517d11d1d188fe858720497c4fc0a1ef9dac00000000010000000465dabdbdb83e9820e4f666f3634d88308909789f7ae29e730812784a96485e3c000000004948304502204c52c2301dcc3f6af7a3ef2ad118185ca2d52a7ae90013332ad53732a085b8d4022100f074ab99e77d5d4c54eb6bfc82c42a094d7d7eaf632d52897ef058c617a2bb2301ffffffffb7404d6a9c451a9f527a7fbeb54839c2bca2eac7b138cdd700be19d733efa0fc000000004847304402206c55518aa596824d1e760afcfeb7b0103a1a82ea8dcd4c3474becc8246ba823702205009cbc40affa3414f9a139f38a86f81a401193f759fb514b9b1d4e2e49f82a401ffffffffcc07817ab589551d698ba7eb2a6efd6670d6951792ad52e2bd5832bf2f4930ec0000000049483045022100b485b4daa4af75b7b34b4f2338e7b96809c75fab5577905ade0789c7f821a69e022010d73d2a3c7fcfc6db911dead795b0aa7d5448447ad5efc7e516699955a18ac801fffffffff61fefef8ee758b273ee64e1bf5c07485dd74cd065a5ce0d59827e0700cad0d9000000004a493046022100bc6e89ee580d1c721b15c36d0a1218c9e78f6f7537616553341bbd1199fe615a02210093062f2c1a1c87f55b710011976a03dff57428e38dd640f6fbdef0fa52ad462d01ffffffff0100c817a80400000043410408998c08bbe6bba756e9b864722fe76ca403929382db2b120f9f621966b00af48f4b014b458bccd4f2acf63b1487ecb9547bc87bdecb08e9c4d08c138c76439aac00000000010000000115327dc99375fc1fdc02e15394369daa6e23ad4dc27e7c1c4af21606add5b068000000004a49304602210086b55b7f2fa5395d1e90a85115ada930afa01b86116d6bbeeecd8e2b97eefbac022100d653846d378845df2ced4b4923dcae4b6ddd5e8434b25e1602928235054c8d5301ffffffff0100f2052a01000000434104b68b035858a00051ca70dd4ba297168d9a3720b642c2e0cd08846bfbb144233b11b24c4b8565353b579bd7109800e42a1fc1e20dbdfbba6a12d0089aab313181ac00000000");

    {
        internal_database db(db_path, db_mode_type::full, 10000000, db_size, true);
        REQUIRE(db.open());
        REQUIRE(db.push_block(b0, 0, 1) == result_code::success);
        REQUIRE(db.push_block(b1, 1, 1) == result_code::success);
        REQUIRE(db.push_block(b2, 2, 1) == result_code::success);
        REQUIRE(db.push_block(b3, 3, 1) == result_code::success);
        REQUIRE(db.push_block(b4, 4, 1)      == result_code::success);
        REQUIRE(db.push_block(spender0, 5, 1) == result_code::success);
    }   //close() implicit


    KTH_DB_env* env_;
    //KTH_DB_txn* db_txn;
    KTH_DB_dbi dbi_utxo_;
    KTH_DB_dbi dbi_reorg_pool_;
    KTH_DB_dbi dbi_reorg_index_;
    KTH_DB_dbi dbi_block_header_;
    KTH_DB_dbi dbi_block_header_by_hash_;
    KTH_DB_dbi dbi_reorg_block_;
    KTH_DB_dbi dbi_block_db_;
    KTH_DB_dbi dbi_transaction_db_;
    KTH_DB_dbi dbi_transaction_hash_db_;
    KTH_DB_dbi dbi_transaction_unconfirmed_db_;
    KTH_DB_dbi dbi_history_db_;
    KTH_DB_dbi dbi_spend_db_;

    //KTH_DB_txn* db_txn;
    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_
    , dbi_block_db_
    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    check_reorg_output_just_existence(env_, dbi_reorg_pool_, "3c5e48964a781208739ee27a9f78098930884d63f366f6e420983eb8bdbdda65", 0);
    check_reorg_output_just_existence(env_, dbi_reorg_pool_, "fca0ef33d719be00d7cd38b1c7eaa2bcc23948b5be7f7a529f1a459c6a4d40b7", 0);
    check_reorg_output_just_existence(env_, dbi_reorg_pool_, "ec30492fbf3258bde252ad921795d67066fd6e2aeba78b691d5589b57a8107cc", 0);
    check_reorg_output_just_existence(env_, dbi_reorg_pool_, "d9d0ca00077e82590dcea565d04cd75d48075cbfe164ee73b258e78eefef1ff6", 0);
    check_reorg_output_just_existence(env_, dbi_reorg_pool_, "68b0d5ad0616f24a1c7c7ec24dad236eaa9d369453e102dc1ffc7593c97d3215", 0);


    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_ ,  0);
    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_, 5);

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 5);

    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 5) == 5);
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 8) == 0);

    check_index_and_pool(env_, dbi_reorg_index_, dbi_reorg_pool_);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_
        , dbi_block_db_
        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
        , dbi_transaction_unconfirmed_db_
    );
}


TEST_CASE("internal database  reorg index2", "[None]") {

    // Block #4334
    // BlockHash 000000009cdad3c55df9c9bc88265329254a6c8ca810fa7f0e953c947df86dc7
    auto const b0 = get_block("010000005a0aeca67f7e43582c2b0138b2daa0c8dc1bedbb2477cfba2d3f96bf0000000065dabdbdb83e9820e4f666f3634d88308909789f7ae29e730812784a96485e3cd5899749ffff001dc2544c010101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0804ffff001d028e00ffffffff0100f2052a01000000434104b48f20398caaf3ff5d40710e0af87a4b86fd19d125ddacf15a2a023831d1731350e5fd40d0e28bb6481ad1843847213764feb98a2dd041069a8c39c842e1da93ac00000000");
    // Block #4966
    // BlockHash 000000004f6a440a95a5d2d6c89f3e6b46587cd43f76efbaa96ef5d37ea90961
    auto const b1 = get_block("010000002cba11cca7170e1da742fcfb83d15c091bac17a79c089e944dda904a00000000b7404d6a9c451a9f527a7fbeb54839c2bca2eac7b138cdd700be19d733efa0fc82609e49ffff001df66339030101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d0121ffffffff0100f2052a01000000434104c20502b45fe276d418a32b55435cb4361dea4e173c36a8e0ad52518b17f2d48cde4336c8ac8d2270e6040469f11c56036db1feef803fb529e36d0f599261cb19ac00000000");
    // Block #4561
    // BlockHash 0000000089abf237d732a1515f7066e7ba29e1833664da8b704c8575c0465223
    auto const b2 = get_block("0100000052df1ff74876f2de37db341e12f932768f2a18dcc09dd024a1e676aa00000000cc07817ab589551d698ba7eb2a6efd6670d6951792ad52e2bd5832bf2f4930ecb5f19949ffff001d4045c6010101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0804ffff001d029f00ffffffff0100f2052a010000004341044bdc62d08cc074664cef09b24b01125a5e92fc2d61f821d6fddac367eb80b06a0a6148feabb0d25717f9eb84950ef0d3e7fe49ce7fb5a6e14da881a5b2bc64c0ac00000000");
    // Block #4991
    // BlockHash 00000000fa413253e1d30ff687f239b528330c810e3de86e42a538175682599d
    auto const b3 = get_block("0100000040eb019191a99f1f3ef5e04606314d80a635e214ca3347388259ad4000000000f61fefef8ee758b273ee64e1bf5c07485dd74cd065a5ce0d59827e0700cad0d98c9f9e49ffff001d20a92f010101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d013affffffff0100f2052a01000000434104debec4c8f781d5223a90aa90416ee51abf15d37086cc2d9be67873534fb06ae68c8a4e0ed0a7eedff9c97fe23f03e652d7f44501286dc1f75148bfaa9e386300ac00000000");
    // Block #4556
    // BlockHash 0000000054d4f171b0eab3cd4e31da4ce5a1a06f27b39bf36c5902c9bb8ef5c4
    auto const b4 = get_block("0100000021a06106f13b4f0112a63e77fae3a48ffe10716ff3cdfb35371032990000000015327dc99375fc1fdc02e15394369daa6e23ad4dc27e7c1c4af21606add5b068dadf9949ffff001dda8444000101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0804ffff001d029600ffffffff0100f2052a0100000043410475804bd8be2560ab2ebd170228429814d60e58d7415a93dc51f23e4cb2f8b5188dd56fa8d519e9ef9c16d6ab22c1c8304e10e74a28444eb26821948b2d1482a4ac00000000");
    // Block #9
    // BlockHash 000000008d9dc510f23c2657fc4f67bea30078cc05a90eb89e84cc475c080805
    auto const b5 = get_block("01000000c60ddef1b7618ca2348a46e868afc26e3efc68226c78aa47f8488c4000000000c997a5e56e104102fa209c6a852dd90660a20b2d9c352423edce25857fcd37047fca6649ffff001d28404f530101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d0134ffffffff0100f2052a0100000043410411db93e1dcdb8a016b49840f8c53bc1eb68a382e97b1482ecad7b148a6909a5cb2e0eaddfb84ccf9744464f82e160bfa9b8b64f9d4c03f999b8643f656b412a3ac00000000");


    // Block #5217
    // BlockHash 0000000025075f093c42a0393c844bc59f90024b18a9f588f6fa3fc37487c3c2
    auto const spender0 = get_block("01000000944bb801c604dda3d51758f292afdca8d973288434c8fe4bf0b5982d000000008a7d204ffe05282b05f280459401b59be41b089cefc911f4fb5641f90309d942b929a149ffff001d1b8d847f0301000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0804ffff001d02c500ffffffff0100f2052a01000000434104f4af426e464d972012256f4cbe5df528aa99b1ceb489968a56cf6b295e6fad1473be89f66fbd3d16adf3dfba7c5253517d11d1d188fe858720497c4fc0a1ef9dac00000000010000000465dabdbdb83e9820e4f666f3634d88308909789f7ae29e730812784a96485e3c000000004948304502204c52c2301dcc3f6af7a3ef2ad118185ca2d52a7ae90013332ad53732a085b8d4022100f074ab99e77d5d4c54eb6bfc82c42a094d7d7eaf632d52897ef058c617a2bb2301ffffffffb7404d6a9c451a9f527a7fbeb54839c2bca2eac7b138cdd700be19d733efa0fc000000004847304402206c55518aa596824d1e760afcfeb7b0103a1a82ea8dcd4c3474becc8246ba823702205009cbc40affa3414f9a139f38a86f81a401193f759fb514b9b1d4e2e49f82a401ffffffffcc07817ab589551d698ba7eb2a6efd6670d6951792ad52e2bd5832bf2f4930ec0000000049483045022100b485b4daa4af75b7b34b4f2338e7b96809c75fab5577905ade0789c7f821a69e022010d73d2a3c7fcfc6db911dead795b0aa7d5448447ad5efc7e516699955a18ac801fffffffff61fefef8ee758b273ee64e1bf5c07485dd74cd065a5ce0d59827e0700cad0d9000000004a493046022100bc6e89ee580d1c721b15c36d0a1218c9e78f6f7537616553341bbd1199fe615a02210093062f2c1a1c87f55b710011976a03dff57428e38dd640f6fbdef0fa52ad462d01ffffffff0100c817a80400000043410408998c08bbe6bba756e9b864722fe76ca403929382db2b120f9f621966b00af48f4b014b458bccd4f2acf63b1487ecb9547bc87bdecb08e9c4d08c138c76439aac00000000010000000115327dc99375fc1fdc02e15394369daa6e23ad4dc27e7c1c4af21606add5b068000000004a49304602210086b55b7f2fa5395d1e90a85115ada930afa01b86116d6bbeeecd8e2b97eefbac022100d653846d378845df2ced4b4923dcae4b6ddd5e8434b25e1602928235054c8d5301ffffffff0100f2052a01000000434104b68b035858a00051ca70dd4ba297168d9a3720b642c2e0cd08846bfbb144233b11b24c4b8565353b579bd7109800e42a1fc1e20dbdfbba6a12d0089aab313181ac00000000");

    // Block #170
    // BlockHash 00000000d1145790a8694403d4063f323d499e655c83426834d4ce2f8dd4a2ee
    auto const spender1 = get_block("0100000055bd840a78798ad0da853f68974f3d183e2bd1db6a842c1feecf222a00000000ff104ccb05421ab93e63f8c3ce5c2c2e9dbb37de2764b3a3175c8166562cac7d51b96a49ffff001d283e9e700201000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d0102ffffffff0100f2052a01000000434104d46c4968bde02899d2aa0963367c7a6ce34eec332b32e42e5f3407e052d64ac625da6f0718e7b302140434bd725706957c092db53805b821a85b23a7ac61725bac000000000100000001c997a5e56e104102fa209c6a852dd90660a20b2d9c352423edce25857fcd3704000000004847304402204e45e16932b8af514961a1d3a1a25fdf3f4f7732e9d624c6c61548ab5fb8cd410220181522ec8eca07de4860a4acdd12909d831cc56cbbac4622082221a8768d1d0901ffffffff0200ca9a3b00000000434104ae1a62fe09c5f51b13905f07f06b99a2f7159b2225f374cd378d71302fa28414e7aab37397f554a7df5f142c21c1b7303b8a0626f1baded5c72a704f7e6cd84cac00286bee0000000043410411db93e1dcdb8a016b49840f8c53bc1eb68a382e97b1482ecad7b148a6909a5cb2e0eaddfb84ccf9744464f82e160bfa9b8b64f9d4c03f999b8643f656b412a3ac00000000");


    {
        internal_database db(db_path, db_mode_type::full, 10000000, db_size, true);
        REQUIRE(db.open());
        REQUIRE(db.push_block(b0, 0, 1) == result_code::success);
        REQUIRE(db.push_block(b1, 1, 1) == result_code::success);
        REQUIRE(db.push_block(b2, 2, 1) == result_code::success);
        REQUIRE(db.push_block(b3, 3, 1) == result_code::success);
        REQUIRE(db.push_block(b4, 4, 1) == result_code::success);
        REQUIRE(db.push_block(b5, 5, 1) == result_code::success);

        auto p = db.get_utxo_pool_from(0, 5);
        REQUIRE(p.first == result_code::key_not_found);
        REQUIRE(p.second.size() == 0);

        REQUIRE(db.push_block(spender0, 6, 1) == result_code::success);

        p = db.get_utxo_pool_from(0, 6);
        REQUIRE(p.first == result_code::success);
        REQUIRE(p.second.size() == 5);

        p = db.get_utxo_pool_from(6, 6);
        REQUIRE(p.first == result_code::success);
        REQUIRE(p.second.size() == 5);

        REQUIRE(db.push_block(spender1, 7, 1) == result_code::success);

        p = db.get_utxo_pool_from(0, 7);
        REQUIRE(p.first == result_code::success);
        REQUIRE(p.second.size() == 6);

        p = db.get_utxo_pool_from(6, 7);
        REQUIRE(p.first == result_code::success);
        REQUIRE(p.second.size() == 6);

    }   //close() implicit


    KTH_DB_env* env_;
    //KTH_DB_txn* db_txn;
    KTH_DB_dbi dbi_utxo_;
    KTH_DB_dbi dbi_reorg_pool_;
    KTH_DB_dbi dbi_reorg_index_;
    KTH_DB_dbi dbi_block_header_;
    KTH_DB_dbi dbi_block_header_by_hash_;
    KTH_DB_dbi dbi_reorg_block_;


    KTH_DB_dbi dbi_block_db_;


    KTH_DB_dbi dbi_transaction_db_;
    KTH_DB_dbi dbi_transaction_hash_db_;
    KTH_DB_dbi dbi_transaction_unconfirmed_db_;
    KTH_DB_dbi dbi_history_db_;
    KTH_DB_dbi dbi_spend_db_;

    //KTH_DB_txn* db_txn;
    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    check_reorg_output_just_existence(env_, dbi_reorg_pool_, "3c5e48964a781208739ee27a9f78098930884d63f366f6e420983eb8bdbdda65", 0);
    check_reorg_output_just_existence(env_, dbi_reorg_pool_, "fca0ef33d719be00d7cd38b1c7eaa2bcc23948b5be7f7a529f1a459c6a4d40b7", 0);
    check_reorg_output_just_existence(env_, dbi_reorg_pool_, "ec30492fbf3258bde252ad921795d67066fd6e2aeba78b691d5589b57a8107cc", 0);
    check_reorg_output_just_existence(env_, dbi_reorg_pool_, "d9d0ca00077e82590dcea565d04cd75d48075cbfe164ee73b258e78eefef1ff6", 0);
    check_reorg_output_just_existence(env_, dbi_reorg_pool_, "68b0d5ad0616f24a1c7c7ec24dad236eaa9d369453e102dc1ffc7593c97d3215", 0);

    check_reorg_output_just_existence(env_, dbi_reorg_pool_, "0437cd7f8525ceed2324359c2d0ba26006d92d856a9c20fa0241106ee5a597c9", 0);




    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_ ,  0);
    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_, 7);

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 6);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 6);

    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 6) == 5);
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 7) == 1);
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 8) == 0);

    check_index_and_pool(env_, dbi_reorg_index_, dbi_reorg_pool_);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );
}

/*
TEST_CASE("internal database  test tx address", "[None]") {

std::println("*************************************************************");

auto wire_tx1 = decode_base16("0100000001a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f5000000004948304502206e21798a42fae0e854281abd38bacd1aeed3ee3738d9e1446618c4571d1090db022100e2ac980643b0b82c0e88ffdfec6b64e3e6ba35e7ba5fdd7d5d6cc8d25c6b241501ffffffff0100f2052a010000001976a914404371705fa9bd789a2fcd52d2c580b65d35549d88ac00000000");
REQUIRE(wire_tx1);
transaction tx1;
REQUIRE(tx1.from_data(*wire_tx1, true));

REQUIRE(tx1.is_valid());

for (auto const& i : tx1.inputs()) {

    std::println("address HHHHHHHHHHHHHHHHH:{}", i.address());

    auto const& script = i.script();

    std::println("address SSSSS:{}", script.to_string(0));


    for (auto const& a : i.addresses()) {
            std::println("address iiiiiii:{}", a);
    }
}


for (auto const& o : tx1.outputs()) {
    for (auto const& a : o.addresses()) {
            std::println("address oooooo:{}", a);
    }
}

std::println("*************************************************************");

}

*/

TEST_CASE("internal database  reorg 0", "[None]") {
    //79880 - 00000000002e872c6fbbcf39c93ef0d89e33484ebf457f6829cbf4b561f3af5a
    // timestamp = 1284561413
    //             Sep 15, 2010 11:36:53 AM
    std::string orig_enc = "01000000a594fda9d85f69e762e498650d6fdb54d838657cea7841915203170000000000a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f505da904ce6ed5b1b017fe8070101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b015cffffffff0100f2052a01000000434104283338ffd784c198147f99aed2cc16709c90b1522e3b3637b312a6f9130e0eda7081e373a96d36be319710cd5c134aaffba81ff08650d7de8af332fe4d8cde20ac00000000";
    auto const orig = get_block(orig_enc);

    //80000 - 000000000043a8c0fd1d6f726790caa2a406010d19efd2780db27bdbbd93baf6
    // timestamp = 1284613427
    //             Sep 16, 2010 2:03:47 AM
    std::string spender_enc = "01000000ba8b9cda965dd8e536670f9ddec10e53aab14b20bacad27b9137190000000000190760b278fe7b8565fda3b968b918d5fd997f993b23674c0af3b6fde300b38f33a5914ce6ed5b1b01e32f570201000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b014effffffff0100f2052a01000000434104b68a50eaa0287eff855189f949c1c6e5f58b37c88231373d8a59809cbae83059cc6469d65c665ccfd1cfeb75c6e8e19413bba7fbff9bc762419a76d87b16086eac000000000100000001a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f5000000004948304502206e21798a42fae0e854281abd38bacd1aeed3ee3738d9e1446618c4571d1090db022100e2ac980643b0b82c0e88ffdfec6b64e3e6ba35e7ba5fdd7d5d6cc8d25c6b241501ffffffff0100f2052a010000001976a914404371705fa9bd789a2fcd52d2c580b65d35549d88ac00000000";

    auto const spender = get_block(spender_enc);

    using my_clock = dummy_clock<1284613427>;

    hash_digest txid;
    hash_digest txid2;
    hash_digest txid3;
    REQUIRE(decode_hash(txid, "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6"));
    REQUIRE(decode_hash(txid2, "c06fbab289f723c6261d3030ddb6be121f7d2508d77862bb1e484f5cd7f92b25"));
    REQUIRE(decode_hash(txid3, "5a4ebf66822b0b2d56bd9dc64ece0bc38ee7844a23ff1d7320a88c5fdb2ad3e2"));

    KTH_DB_env* env_;
    KTH_DB_dbi dbi_utxo_;
    KTH_DB_dbi dbi_reorg_pool_;
    KTH_DB_dbi dbi_reorg_index_;
    KTH_DB_dbi dbi_block_header_;
    KTH_DB_dbi dbi_block_header_by_hash_;
    KTH_DB_dbi dbi_reorg_block_;
    KTH_DB_dbi dbi_block_db_;
    KTH_DB_dbi dbi_transaction_db_;
    KTH_DB_dbi dbi_transaction_hash_db_;
    KTH_DB_dbi dbi_transaction_unconfirmed_db_;
    KTH_DB_dbi dbi_history_db_;
    KTH_DB_dbi dbi_spend_db_;


    // Insert the First Block
    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 86, db_size, true); // 1 to 86 no entra el primero
        REQUIRE(db.open());

        //State A ------------------------------------------------------------
        REQUIRE(db.push_block(orig, 0, 1) == result_code::success);

        auto entry = db.get_utxo(output_point{txid, 0});
        REQUIRE(entry.is_valid());

        auto const& address = domain::wallet::payment_address("1JBSCVF6VM6QjFZyTnbpLjoCJTQEqVbepG");
        REQUIRE(address);


        auto history_list = db.get_history(address.hash20(),max_uint32,0);
        REQUIRE(history_list.size() == 1);

        auto history_item = history_list[0];

        hash_digest txid;
        std::string txid_enc = "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6";
        REQUIRE(decode_hash(txid, txid_enc));

        REQUIRE(history_item.kind == point_kind::output);
        REQUIRE(history_item.point.hash() == txid);
        REQUIRE(history_item.point.index() == 0);
        REQUIRE(history_item.height == 0);
        REQUIRE(history_item.value == 5000000000);
    }   //close() implicit

    //KTH_DB_txn* db_txn;
    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();



    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_, 0);

    auto const& address = domain::wallet::payment_address("1JBSCVF6VM6QjFZyTnbpLjoCJTQEqVbepG");
    REQUIRE(address);

    REQUIRE(db_count_db_by_address(env_, dbi_history_db_, address) == 1);

    print_db_entries_count(env_, dbi_transaction_db_);
    check_transactions_db_just_existence(env_,dbi_transaction_db_,0);
    REQUIRE(db_count_items(env_, dbi_transaction_db_) == 1);

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 0);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 0);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 0);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );

    // Insert the Spender Block
    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 86, db_size, true); // 1 to 86 no entra el primero
        REQUIRE(db.open());

        //State B ------------------------------------------------------------
        REQUIRE(db.push_block(spender, 1, 1) == result_code::success);

        auto entry = db.get_utxo(output_point{txid, 0});
        REQUIRE( ! entry.is_valid());

        entry = db.get_utxo(output_point{txid2, 0});
        REQUIRE(entry.is_valid());

        entry = db.get_utxo(output_point{txid3, 0});
        REQUIRE(entry.is_valid());

        REQUIRE(db.get_header(orig.hash()).first.is_valid());
        REQUIRE(db.get_header(orig.hash()).first.hash() == orig.hash());
        REQUIRE(db.get_header(orig.hash()).second == 0);
        REQUIRE(db.get_header(0).is_valid());
        REQUIRE(db.get_header(0).hash() == orig.hash());

        REQUIRE(db.get_header(spender.hash()).first.is_valid());
        REQUIRE(db.get_header(spender.hash()).first.hash() == spender.hash());
        REQUIRE(db.get_header(spender.hash()).second == 1);
        REQUIRE(db.get_header(1).is_valid());
        REQUIRE(db.get_header(1).hash() == spender.hash());



        auto const& address = domain::wallet::payment_address("1JBSCVF6VM6QjFZyTnbpLjoCJTQEqVbepG");
        REQUIRE(address);

        auto history_list = db.get_history(address.hash20(),max_uint32,0);
        REQUIRE(history_list.size() == 2);

        auto history_item = history_list[0];

        hash_digest txid;
        std::string txid_enc = "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6";
        REQUIRE(decode_hash(txid, txid_enc));

        REQUIRE(history_item.kind == point_kind::output);
        REQUIRE(history_item.point.hash() == txid);
        REQUIRE(history_item.point.index() == 0);
        REQUIRE(history_item.height == 0);
        REQUIRE(history_item.value == 5000000000);

        history_item = history_list[1];

        hash_digest txid2;
        std::string txid_enc2 = "5a4ebf66822b0b2d56bd9dc64ece0bc38ee7844a23ff1d7320a88c5fdb2ad3e2";
        REQUIRE(decode_hash(txid2, txid_enc2));

        auto const& tx_entry = db.get_transaction(txid,max_uint32);
        REQUIRE(tx_entry.is_valid());
        auto const& tx = tx_entry.transaction();
        output_point point = {tx.hash(), 0};

        REQUIRE(history_item.kind == point_kind::spend);
        REQUIRE(history_item.point.hash() == txid2);
        REQUIRE(history_item.point.index() == 0);
        REQUIRE(history_item.height == 1);
        REQUIRE(history_item.previous_checksum ==  point.checksum());


        auto const& address2 = domain::wallet::payment_address("16ro3Jptwo4asSevZnsRX6vfRS24TGE6uK");
        REQUIRE(address2);

        history_list = db.get_history(address2.hash20(), max_uint32, 0);
        REQUIRE(history_list.size() == 1);

        history_item = history_list[0];

        REQUIRE(decode_hash(txid, txid_enc));

        REQUIRE(history_item.kind == point_kind::output);
        REQUIRE(history_item.point.hash() == txid2);
        REQUIRE(history_item.point.index() == 0);
        REQUIRE(history_item.height == 1);
        REQUIRE(history_item.value == 5000000000);

        auto const& in_point = db.get_spend(output_point{txid, 0});
        REQUIRE(in_point.is_valid());

    }   //close() implicit


    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 1);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 1);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 1);
    check_reorg_output(env_, dbi_reorg_pool_, "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6", 0, "00f2052a01000000434104283338ffd784c198147f99aed2cc16709c90b1522e3b3637b312a6f9130e0eda7081e373a96d36be319710cd5c134aaffba81ff08650d7de8af332fe4d8cde20ac");
    check_reorg_index(env_, dbi_reorg_index_, "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6", 0, 1);
    check_reorg_block_doesnt_exists(env_, dbi_reorg_block_, 0);
    check_reorg_block(env_, dbi_reorg_block_, 1, spender_enc);


    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_, 1);

    auto const& address4 = domain::wallet::payment_address("18REpJroZy5eYCtqK1jwwgQUvVkPojy2rR");
    REQUIRE(address4);
    REQUIRE(db_count_db_by_address(env_, dbi_history_db_, address4) == 1);

    auto const& address1 = domain::wallet::payment_address("16ro3Jptwo4asSevZnsRX6vfRS24TGE6uK");
    REQUIRE(address1);
    REQUIRE(db_count_db_by_address(env_, dbi_history_db_, address1) == 1);

    print_db_entries_count(env_, dbi_transaction_db_);
    check_transactions_db_just_existence(env_,dbi_transaction_db_,0);
    check_transactions_db_just_existence(env_,dbi_transaction_db_,1);
    check_transactions_db_just_existence(env_,dbi_transaction_db_,2);
    REQUIRE(db_count_items(env_, dbi_transaction_db_) == 3);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );

    // Remove the Spender Block
    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 86, db_size, true); // 1 to 86 no entra el primero
        REQUIRE(db.open());

        //State C ------------------------------------------------------------
        domain::chain::block out_block;
        REQUIRE(db.pop_block(out_block) == result_code::success);
        REQUIRE(out_block.is_valid());
        REQUIRE(out_block.hash() == spender.hash());
        REQUIRE(out_block == spender);

        auto entry = db.get_utxo(output_point{txid, 0});
        REQUIRE(entry.is_valid());

        entry = db.get_utxo(output_point{txid2, 0});
        REQUIRE( ! entry.is_valid());
        entry = db.get_utxo(output_point{txid3, 0});
        REQUIRE( ! entry.is_valid());

        REQUIRE(db.get_header(orig.hash()).first.is_valid());
        REQUIRE(db.get_header(orig.hash()).first.hash() == orig.hash());
        REQUIRE(db.get_header(orig.hash()).second == 0);
        REQUIRE(db.get_header(0).is_valid());
        REQUIRE(db.get_header(0).hash() == orig.hash());

        REQUIRE( !  db.get_header(spender.hash()).first.is_valid());
        REQUIRE( !  db.get_header(1).is_valid());



        /*auto const& address = domain::wallet::payment_address("1JBSCVF6VM6QjFZyTnbpLjoCJTQEqVbepG");
        REQUIRE(address);

        auto history_list = db.get_history(address.hash20(),max_uint32,0);
        REQUIRE(history_list.size() == 1);

        auto history_item = history_list[0];

        hash_digest txid;
        std::string txid_enc = "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6";
        REQUIRE(decode_hash(txid, txid_enc));

        REQUIRE(history_item.kind == point_kind::output);
        REQUIRE(history_item.point.hash() == txid);
        REQUIRE(history_item.point.index() == 0);
        REQUIRE(history_item.height == 0);
        REQUIRE(history_item.value == 5000000000);


        auto const& address2 = domain::wallet::payment_address("bitcoincash:qpqyxutst75m67y69lx495k9szm96d25n53frahv6a");
        REQUIRE(address2);

        history_list = db.get_history(address2.hash(),max_uint32,0);
        REQUIRE(history_list.size() == 0);


        auto const& in_point = db.get_spend(output_point{txid, 0});
        REQUIRE( ! in_point.is_valid());*/
    }   //close() implicit


    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 0);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 0);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 0);



    check_blocks_db_doesnt_exists(env_, dbi_block_db_,1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);

    print_db_entries_count(env_, dbi_transaction_db_);
    check_transactions_db_just_existence(env_,dbi_transaction_db_,0);
    check_transactions_db_doesnt_exists(env_,dbi_transaction_db_,1);
    check_transactions_db_doesnt_exists(env_,dbi_transaction_db_,2);
    REQUIRE(db_count_items(env_, dbi_transaction_db_) == 1);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_


        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
        , dbi_transaction_unconfirmed_db_
    );

    // Insert the Spender Block, again
    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 86, db_size, true); // 1 to 86 no entra el primero
        REQUIRE(db.open());

        //State B ------------------------------------------------------------
        REQUIRE(db.push_block(spender, 1, 1) == result_code::success);

        auto entry = db.get_utxo(output_point{txid, 0});
        REQUIRE( ! entry.is_valid());

        entry = db.get_utxo(output_point{txid2, 0});
        REQUIRE(entry.is_valid());

        entry = db.get_utxo(output_point{txid3, 0});
        REQUIRE(entry.is_valid());

        REQUIRE(db.get_header(orig.hash()).first.is_valid());
        REQUIRE(db.get_header(orig.hash()).first.hash() == orig.hash());
        REQUIRE(db.get_header(orig.hash()).second == 0);
        REQUIRE(db.get_header(0).is_valid());
        REQUIRE(db.get_header(0).hash() == orig.hash());

        REQUIRE(db.get_header(spender.hash()).first.is_valid());
        REQUIRE(db.get_header(spender.hash()).first.hash() == spender.hash());
        REQUIRE(db.get_header(spender.hash()).second == 1);
        REQUIRE(db.get_header(1).is_valid());
        REQUIRE(db.get_header(1).hash() == spender.hash());



        auto const& address = domain::wallet::payment_address("1JBSCVF6VM6QjFZyTnbpLjoCJTQEqVbepG");
        REQUIRE(address);

        auto history_list = db.get_history(address.hash20(),max_uint32,0);
        REQUIRE(history_list.size() == 2);

        auto history_item = history_list[0];

        hash_digest txid;
        std::string txid_enc = "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6";
        REQUIRE(decode_hash(txid, txid_enc));

        REQUIRE(history_item.kind == point_kind::output);
        REQUIRE(history_item.point.hash() == txid);
        REQUIRE(history_item.point.index() == 0);
        REQUIRE(history_item.height == 0);
        REQUIRE(history_item.value == 5000000000);

        history_item = history_list[1];

        hash_digest txid2;
        std::string txid_enc2 = "5a4ebf66822b0b2d56bd9dc64ece0bc38ee7844a23ff1d7320a88c5fdb2ad3e2";
        REQUIRE(decode_hash(txid2, txid_enc2));

        auto const& tx_entry = db.get_transaction(txid, max_uint32);
        REQUIRE(tx_entry.is_valid());
        auto const& tx = tx_entry.transaction();
        output_point point = {tx.hash(), 0};

        REQUIRE(history_item.kind == point_kind::spend);
        REQUIRE(history_item.point.hash() == txid2);
        REQUIRE(history_item.point.index() == 0);
        REQUIRE(history_item.height == 1);
        REQUIRE(history_item.previous_checksum ==  point.checksum());




        auto const& address2 = domain::wallet::payment_address("16ro3Jptwo4asSevZnsRX6vfRS24TGE6uK");
        REQUIRE(address2);

        history_list = db.get_history(address2.hash20(), max_uint32, 0);
        REQUIRE(history_list.size() == 1);

        history_item = history_list[0];

        REQUIRE(decode_hash(txid, txid_enc));

        REQUIRE(history_item.kind == point_kind::output);
        REQUIRE(history_item.point.hash() == txid2);
        REQUIRE(history_item.point.index() == 0);
        REQUIRE(history_item.height == 1);
        REQUIRE(history_item.value == 5000000000);



        auto const& in_point = db.get_spend(output_point{txid, 0});
        REQUIRE(in_point.is_valid());
    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();


    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 1);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 1);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 1);
    check_reorg_output(env_, dbi_reorg_pool_, "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6", 0, "00f2052a01000000434104283338ffd784c198147f99aed2cc16709c90b1522e3b3637b312a6f9130e0eda7081e373a96d36be319710cd5c134aaffba81ff08650d7de8af332fe4d8cde20ac");
    check_reorg_index(env_, dbi_reorg_index_, "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6", 0, 1);
    check_reorg_block_doesnt_exists(env_, dbi_reorg_block_, 0);
    check_reorg_block(env_, dbi_reorg_block_, 1, spender_enc);




    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);

    print_db_entries_count(env_, dbi_transaction_db_);
    check_transactions_db_just_existence(env_,dbi_transaction_db_,0);
    check_transactions_db_just_existence(env_,dbi_transaction_db_,1);
    check_transactions_db_just_existence(env_,dbi_transaction_db_,2);
    REQUIRE(db_count_items(env_, dbi_transaction_db_) == 3);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_


        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
        , dbi_transaction_unconfirmed_db_
    );
}

TEST_CASE("internal database  reorg 1", "[None]") {
    //79880 - 00000000002e872c6fbbcf39c93ef0d89e33484ebf457f6829cbf4b561f3af5a
    // timestamp = 1284561413
    //             Sep 15, 2010 11:36:53 AM
    std::string orig_enc = "01000000a594fda9d85f69e762e498650d6fdb54d838657cea7841915203170000000000a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f505da904ce6ed5b1b017fe8070101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b015cffffffff0100f2052a01000000434104283338ffd784c198147f99aed2cc16709c90b1522e3b3637b312a6f9130e0eda7081e373a96d36be319710cd5c134aaffba81ff08650d7de8af332fe4d8cde20ac00000000";
    auto const orig = get_block(orig_enc);

    //80000 - 000000000043a8c0fd1d6f726790caa2a406010d19efd2780db27bdbbd93baf6
    // timestamp = 1284613427
    //             Sep 16, 2010 2:03:47 AM
    std::string spender_enc = "01000000ba8b9cda965dd8e536670f9ddec10e53aab14b20bacad27b9137190000000000190760b278fe7b8565fda3b968b918d5fd997f993b23674c0af3b6fde300b38f33a5914ce6ed5b1b01e32f570201000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b014effffffff0100f2052a01000000434104b68a50eaa0287eff855189f949c1c6e5f58b37c88231373d8a59809cbae83059cc6469d65c665ccfd1cfeb75c6e8e19413bba7fbff9bc762419a76d87b16086eac000000000100000001a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f5000000004948304502206e21798a42fae0e854281abd38bacd1aeed3ee3738d9e1446618c4571d1090db022100e2ac980643b0b82c0e88ffdfec6b64e3e6ba35e7ba5fdd7d5d6cc8d25c6b241501ffffffff0100f2052a010000001976a914404371705fa9bd789a2fcd52d2c580b65d35549d88ac00000000";
    auto const spender = get_block(spender_enc);

    using my_clock = dummy_clock<1284613427 + 600>;

    hash_digest txid;
    hash_digest txid2;
    hash_digest txid3;
    REQUIRE(decode_hash(txid, "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6"));
    REQUIRE(decode_hash(txid2, "c06fbab289f723c6261d3030ddb6be121f7d2508d77862bb1e484f5cd7f92b25"));
    REQUIRE(decode_hash(txid3, "5a4ebf66822b0b2d56bd9dc64ece0bc38ee7844a23ff1d7320a88c5fdb2ad3e2"));


    KTH_DB_env* env_;
    KTH_DB_dbi dbi_utxo_;
    KTH_DB_dbi dbi_reorg_pool_;
    KTH_DB_dbi dbi_reorg_index_;
    KTH_DB_dbi dbi_block_header_;
    KTH_DB_dbi dbi_block_header_by_hash_;
    KTH_DB_dbi dbi_reorg_block_;
    //KTH_DB_txn* db_txn;


    KTH_DB_dbi dbi_block_db_;


    KTH_DB_dbi dbi_transaction_db_;
    KTH_DB_dbi dbi_transaction_hash_db_;
    KTH_DB_dbi dbi_transaction_unconfirmed_db_;
    KTH_DB_dbi dbi_history_db_;
    KTH_DB_dbi dbi_spend_db_;

    // Insert the First Block
    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 1, db_size, true);
        REQUIRE(db.open());

        //State A ------------------------------------------------------------
        REQUIRE(db.push_block(orig, 0, 1) == result_code::success);

        auto entry = db.get_utxo(output_point{txid, 0});
        REQUIRE(entry.is_valid());
    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();


    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 0);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 0);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 0);


    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_


        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
        , dbi_transaction_unconfirmed_db_
    );

    // Insert the Spender Block
    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 1, db_size, true);
        REQUIRE(db.open());

        //State B ------------------------------------------------------------
        REQUIRE(db.push_block(spender, 1, 1) == result_code::success);

        auto entry = db.get_utxo(output_point{txid, 0});
        REQUIRE( ! entry.is_valid());

        entry = db.get_utxo(output_point{txid2, 0});
        REQUIRE(entry.is_valid());

        entry = db.get_utxo(output_point{txid3, 0});
        REQUIRE(entry.is_valid());

        REQUIRE(db.get_header(orig.hash()).first.is_valid());
        REQUIRE(db.get_header(orig.hash()).first.hash() == orig.hash());
        REQUIRE(db.get_header(orig.hash()).second == 0);
        REQUIRE(db.get_header(0).is_valid());
        REQUIRE(db.get_header(0).hash() == orig.hash());

        REQUIRE(db.get_header(spender.hash()).first.is_valid());
        REQUIRE(db.get_header(spender.hash()).first.hash() == spender.hash());
        REQUIRE(db.get_header(spender.hash()).second == 1);
        REQUIRE(db.get_header(1).is_valid());
        REQUIRE(db.get_header(1).hash() == spender.hash());

    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 0);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 0);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 0);
    check_reorg_output_doesnt_exists(env_, dbi_reorg_pool_, "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6", 0);
    check_reorg_index_doesnt_exists(env_, dbi_reorg_index_, 1);
    check_reorg_block_doesnt_exists(env_, dbi_reorg_block_, 0);
    check_reorg_block_doesnt_exists(env_, dbi_reorg_block_, 1);


    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_,dbi_block_header_, dbi_transaction_db_,1);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_


        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );

    // Remove the Spender Block
    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 1, db_size, true);
        REQUIRE(db.open());

        //State C ------------------------------------------------------------
        domain::chain::block out_block;
        REQUIRE(db.pop_block(out_block) == result_code::key_not_found);
    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();



    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_,0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_,1);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_


        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
        , dbi_transaction_unconfirmed_db_
    );


}



TEST_CASE("internal database  prune", "[None]") {

    // Block 0
    auto const genesis = get_genesis();

    // Block 1 - 00000000839a8e6886ab5951d76f411475428afc90947ee320161bbf18eb6048
    auto const b1 = get_block("010000006fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000982051fd1e4ba744bbbe680e1fee14677ba1a3c3540bf7b1cdb606e857233e0e61bc6649ffff001d01e362990101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d0104ffffffff0100f2052a0100000043410496b538e853519c726a2c91e61ec11600ae1390813a627c66fb8be7947be63c52da7589379515d4e0a604f8141781e62294721166bf621e73a82cbf2342c858eeac00000000");

    // Block 2 - 000000006a625f06636b8bb6ac7b960a8d03705d1ace08b1a19da3fdcc99ddbd
    auto const b2 = get_block("010000004860eb18bf1b1620e37e9490fc8a427514416fd75159ab86688e9a8300000000d5fdcc541e25de1c7a5addedf24858b8bb665c9f36ef744ee42c316022c90f9bb0bc6649ffff001d08d2bd610101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d010bffffffff0100f2052a010000004341047211a824f55b505228e4c3d5194c1fcfaa15a456abdf37f9b9d97a4040afc073dee6c89064984f03385237d92167c13e236446b417ab79a0fcae412ae3316b77ac00000000");

    // Block 3 - 0000000082b5015589a3fdf2d4baff403e6f0be035a5d9742c1cae6295464449
    auto const b3 = get_block("01000000bddd99ccfda39da1b108ce1a5d70038d0a967bacb68b6b63065f626a0000000044f672226090d85db9a9f2fbfe5f0f9609b387af7be5b7fbb7a1767c831c9e995dbe6649ffff001d05e0ed6d0101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d010effffffff0100f2052a0100000043410494b9d3e76c5b1629ecf97fff95d7a4bbdac87cc26099ada28066c6ff1eb9191223cd897194a08d0c2726c5747f1db49e8cf90e75dc3e3550ae9b30086f3cd5aaac00000000");

    // Block 4 - 000000004ebadb55ee9096c9a2f8880e09da59c0d68b1c228da88e48844a1485
    auto const b4 = get_block("010000004944469562ae1c2c74d9a535e00b6f3e40ffbad4f2fda3895501b582000000007a06ea98cd40ba2e3288262b28638cec5337c1456aaf5eedc8e9e5a20f062bdf8cc16649ffff001d2bfee0a90101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d011affffffff0100f2052a01000000434104184f32b212815c6e522e66686324030ff7e5bf08efb21f8b00614fb7690e19131dd31304c54f37baa40db231c918106bb9fd43373e37ae31a0befc6ecaefb867ac00000000");

    // Block 79880 - 00000000002e872c6fbbcf39c93ef0d89e33484ebf457f6829cbf4b561f3af5a - timestamp = 1284561413 - Sep 15, 2010 11:36:53 AM
    auto const b79880 = get_block("01000000a594fda9d85f69e762e498650d6fdb54d838657cea7841915203170000000000a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f505da904ce6ed5b1b017fe8070101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b015cffffffff0100f2052a01000000434104283338ffd784c198147f99aed2cc16709c90b1522e3b3637b312a6f9130e0eda7081e373a96d36be319710cd5c134aaffba81ff08650d7de8af332fe4d8cde20ac00000000");

    // Spender blocks ....

    //Block 80000 - 000000000043a8c0fd1d6f726790caa2a406010d19efd2780db27bdbbd93baf6
    // timestamp = 1284613427
    //             Sep 16, 2010 2:03:47 AM

    std::string spender_enc = "01000000ba8b9cda965dd8e536670f9ddec10e53aab14b20bacad27b9137190000000000190760b278fe7b8565fda3b968b918d5fd997f993b23674c0af3b6fde300b38f"
                              "33a5914c"
                              "e6ed5b1b01e32f57"
                              "02"
                              "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b014effffffff0100f2052a01000000434104b68a50eaa0287eff855189f949c1c6e5f58b37c88231373d8a59809cbae83059cc6469d65c665ccfd1cfeb75c6e8e19413bba7fbff9bc762419a76d87b16086eac00000000"
                              "0100000001a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f5000000004948304502206e21798a42fae0e854281abd38bacd1aeed3ee3738d9e1446618c4571d1090db022100e2ac980643b0b82c0e88ffdfec6b64e3e6ba35e7ba5fdd7d5d6cc8d25c6b241501ffffffff0100f2052a010000001976a914404371705fa9bd789a2fcd52d2c580b65d35549d88ac00000000";
    auto const spender80000 = get_block(spender_enc);

    // Dummy blocks to spend
    auto spender80000b = spender80000;
    spender80000b.header().set_version(2);  //To change the block hash
    spender80000b.header().set_timestamp(spender80000b.header().timestamp() + 600 * 1);
    spender80000b.transactions()[0].set_version(2); //To change the coinbase tx hash
    {
        hash_digest txid;
        REQUIRE(decode_hash(txid, "0e3e2357e806b6cdb1f70b54c3a3a17b6714ee1f0e68bebb44a74b1efd512098"));
        spender80000b.transactions()[1].inputs()[0].previous_output().set_hash(txid);
    }
    // std::println("src/database/test/internal_database.cpp", encode_hash(spender80000b.transactions()[1].inputs()[0].previous_output().hash()));

    auto spender80000c = spender80000;
    spender80000c.header().set_version(3);
    spender80000c.header().set_timestamp(spender80000b.header().timestamp() + 600 * 2);
    spender80000c.transactions()[0].set_version(3);
    {
        hash_digest txid;
        REQUIRE(decode_hash(txid, "9b0fc92260312ce44e74ef369f5c66bbb85848f2eddd5a7a1cde251e54ccfdd5"));
        spender80000c.transactions()[1].inputs()[0].previous_output().set_hash(txid);
    }
    // std::println("src/database/test/internal_database.cpp", encode_hash(spender80000c.transactions()[1].inputs()[0].previous_output().hash()));

    auto spender80000d = spender80000;
    spender80000d.header().set_version(4);
    spender80000d.header().set_timestamp(spender80000b.header().timestamp() + 600 * 3);
    spender80000d.transactions()[0].set_version(4);
    {
        hash_digest txid;
        REQUIRE(decode_hash(txid, "999e1c837c76a1b7fbb7e57baf87b309960f5ffefbf2a9b95dd890602272f644"));
        spender80000d.transactions()[1].inputs()[0].previous_output().set_hash(txid);
    }
    // std::println("src/database/test/internal_database.cpp", encode_hash(spender80000d.transactions()[1].inputs()[0].previous_output().hash()));

    auto spender80000e = spender80000;
    spender80000e.header().set_version(5);
    spender80000e.header().set_timestamp(spender80000b.header().timestamp() + 600 * 4);
    spender80000e.transactions()[0].set_version(5);
    {
        hash_digest txid;
        REQUIRE(decode_hash(txid, "df2b060fa2e5e9c8ed5eaf6a45c13753ec8c63282b2688322eba40cd98ea067a"));
        spender80000e.transactions()[1].inputs()[0].previous_output().set_hash(txid);
    }
    // std::println("src/database/test/internal_database.cpp", encode_hash(spender80000e.transactions()[1].inputs()[0].previous_output().hash()));



    using my_clock = dummy_clock<1284613427>;

    KTH_DB_env* env_;
    KTH_DB_dbi dbi_utxo_;
    KTH_DB_dbi dbi_reorg_pool_;
    KTH_DB_dbi dbi_reorg_index_;
    KTH_DB_dbi dbi_reorg_block_;
    KTH_DB_dbi dbi_block_header_;
    KTH_DB_dbi dbi_block_header_by_hash_;


    KTH_DB_dbi dbi_block_db_;


    KTH_DB_dbi dbi_transaction_db_;
    KTH_DB_dbi dbi_transaction_hash_db_;
    KTH_DB_dbi dbi_transaction_unconfirmed_db_;
    KTH_DB_dbi dbi_history_db_;
    KTH_DB_dbi dbi_spend_db_;

    //KTH_DB_txn* db_txn;

    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 86, db_size, true); // 1 to 86 no entra el primero
        REQUIRE(db.open());

        REQUIRE(db.push_block(genesis, 0, 1) == result_code::success);
        REQUIRE(db.push_block(b1, 1, 1)      == result_code::success);
        REQUIRE(db.push_block(b2, 2, 1)      == result_code::success);
        REQUIRE(db.push_block(b3, 3, 1)      == result_code::success);
        REQUIRE(db.push_block(b4, 4, 1)      == result_code::success);
        REQUIRE(db.push_block(b79880, 5, 1)  == result_code::success);

    }   //close() implicit


    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 0);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 0);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 0);

    REQUIRE(db_count_items(env_, dbi_utxo_) == 6);
    REQUIRE(db_count_items(env_, dbi_block_header_) == 6);
    REQUIRE(db_count_items(env_, dbi_block_header_by_hash_) == 6);


    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);

    // check_reorg_output_doesnt_exists(env_, dbi_reorg_pool_, "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6", 0);
    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_


        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );

    // ------------------------------------------------------------------------------------
    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 86, db_size, true); // 1 to 86 no entra el primero
        REQUIRE(db.open());

        REQUIRE(db.push_block(spender80000, 6, 1) == result_code::success);

    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 1);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 1);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 1);

    REQUIRE(db_count_items(env_, dbi_utxo_) == 7);
    REQUIRE(db_count_items(env_, dbi_block_header_) == 7);
    REQUIRE(db_count_items(env_, dbi_block_header_by_hash_) == 7);


    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);

    // check_reorg_output_doesnt_exists(env_, dbi_reorg_pool_, "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6", 0);
    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );

    // ------------------------------------------------------------------------------------
    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 86, db_size, true); // 1 to 86 no entra el primero
        REQUIRE(db.open());

        REQUIRE(db.push_block(spender80000b, 7, 1) == result_code::success);

    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 2);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 2);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 2);

    REQUIRE(db_count_items(env_, dbi_utxo_) == 8);
    REQUIRE(db_count_items(env_, dbi_block_header_) == 8);
    REQUIRE(db_count_items(env_, dbi_block_header_by_hash_) == 8);



    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 7);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
        , dbi_transaction_unconfirmed_db_
    );


    // ------------------------------------------------------------------------------------
    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 86, db_size, true); // 1 to 86 no entra el primero
        REQUIRE(db.open());

        REQUIRE(db.push_block(spender80000c, 8, 1) == result_code::success);

    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 3);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 3);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 3);

    REQUIRE(db_count_items(env_, dbi_utxo_) == 9);
    REQUIRE(db_count_items(env_, dbi_block_header_) == 9);
    REQUIRE(db_count_items(env_, dbi_block_header_by_hash_) == 9);



    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 7);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 8);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
        , dbi_transaction_unconfirmed_db_
    );


    // ------------------------------------------------------------------------------------
    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 86, db_size, true); // 1 to 86 no entra el primero
        REQUIRE(db.open());

        REQUIRE(db.push_block(spender80000d, 9, 1) == result_code::success);

    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 4);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 4);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 4);

    REQUIRE(db_count_items(env_, dbi_utxo_) == 10);
    REQUIRE(db_count_items(env_, dbi_block_header_) == 10);
    REQUIRE(db_count_items(env_, dbi_block_header_by_hash_) == 10);


    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 7);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 8);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 9);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );


    // ------------------------------------------------------------------------------------
    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 86, db_size, true); // 1 to 86 no entra el primero
        REQUIRE(db.open());

        REQUIRE(db.push_block(spender80000e, 10, 1) == result_code::success);

    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 5);

    REQUIRE(db_count_items(env_, dbi_utxo_) == 11);
    REQUIRE(db_count_items(env_, dbi_block_header_) == 11);
    REQUIRE(db_count_items(env_, dbi_block_header_by_hash_) == 11);


    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 7);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 8);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 9);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 10);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );

    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 86, db_size, true);
        REQUIRE(db.open());
        REQUIRE(db.prune() == result_code::no_data_to_prune);
    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 5);




    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 7);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 8);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 9);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 10);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );

    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 11, db_size, true);
        REQUIRE(db.open());
        REQUIRE(db.prune() == result_code::no_data_to_prune);
    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 5);


    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 7);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 8);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 9);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 10);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );

    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 10, db_size, true);
        REQUIRE(db.open());
        REQUIRE(db.prune() == result_code::no_data_to_prune);
    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 5);


    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 7);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 8);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 9);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 10);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
        , dbi_transaction_unconfirmed_db_
    );

    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 5, db_size,true);
        REQUIRE(db.open());
        REQUIRE(db.prune() == result_code::no_data_to_prune);
    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 5);


    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 7);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 8);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 9);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 10);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );

    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 4, db_size, true);
        REQUIRE(db.open());
        REQUIRE(db.prune() == result_code::success);
    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 4);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 4);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 4);


    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 7);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 8);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 9);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 10);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_


        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );

    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 0, db_size, true);
        REQUIRE(db.open());
        REQUIRE(db.prune() == result_code::success);
    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 0);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 0);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 0);


    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 7);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 8);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 9);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 10);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );

}




TEST_CASE("internal database  prune 2", "[None]") {

    // Block 0
    auto const genesis = get_genesis();

    // Block 1 - 00000000839a8e6886ab5951d76f411475428afc90947ee320161bbf18eb6048
    auto const b1 = get_block("010000006fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000982051fd1e4ba744bbbe680e1fee14677ba1a3c3540bf7b1cdb606e857233e0e61bc6649ffff001d01e362990101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d0104ffffffff0100f2052a0100000043410496b538e853519c726a2c91e61ec11600ae1390813a627c66fb8be7947be63c52da7589379515d4e0a604f8141781e62294721166bf621e73a82cbf2342c858eeac00000000");

    // Block 2 - 000000006a625f06636b8bb6ac7b960a8d03705d1ace08b1a19da3fdcc99ddbd
    auto const b2 = get_block("010000004860eb18bf1b1620e37e9490fc8a427514416fd75159ab86688e9a8300000000d5fdcc541e25de1c7a5addedf24858b8bb665c9f36ef744ee42c316022c90f9bb0bc6649ffff001d08d2bd610101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d010bffffffff0100f2052a010000004341047211a824f55b505228e4c3d5194c1fcfaa15a456abdf37f9b9d97a4040afc073dee6c89064984f03385237d92167c13e236446b417ab79a0fcae412ae3316b77ac00000000");

    // Block 3 - 0000000082b5015589a3fdf2d4baff403e6f0be035a5d9742c1cae6295464449
    auto const b3 = get_block("01000000bddd99ccfda39da1b108ce1a5d70038d0a967bacb68b6b63065f626a0000000044f672226090d85db9a9f2fbfe5f0f9609b387af7be5b7fbb7a1767c831c9e995dbe6649ffff001d05e0ed6d0101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d010effffffff0100f2052a0100000043410494b9d3e76c5b1629ecf97fff95d7a4bbdac87cc26099ada28066c6ff1eb9191223cd897194a08d0c2726c5747f1db49e8cf90e75dc3e3550ae9b30086f3cd5aaac00000000");

    // Block 4 - 000000004ebadb55ee9096c9a2f8880e09da59c0d68b1c228da88e48844a1485
    auto const b4 = get_block("010000004944469562ae1c2c74d9a535e00b6f3e40ffbad4f2fda3895501b582000000007a06ea98cd40ba2e3288262b28638cec5337c1456aaf5eedc8e9e5a20f062bdf8cc16649ffff001d2bfee0a90101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d011affffffff0100f2052a01000000434104184f32b212815c6e522e66686324030ff7e5bf08efb21f8b00614fb7690e19131dd31304c54f37baa40db231c918106bb9fd43373e37ae31a0befc6ecaefb867ac00000000");

    // Block 79880 - 00000000002e872c6fbbcf39c93ef0d89e33484ebf457f6829cbf4b561f3af5a - timestamp = 1284561413 - Sep 15, 2010 11:36:53 AM
    auto const b79880 = get_block("01000000a594fda9d85f69e762e498650d6fdb54d838657cea7841915203170000000000a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f505da904ce6ed5b1b017fe8070101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b015cffffffff0100f2052a01000000434104283338ffd784c198147f99aed2cc16709c90b1522e3b3637b312a6f9130e0eda7081e373a96d36be319710cd5c134aaffba81ff08650d7de8af332fe4d8cde20ac00000000");

    // Spender blocks ....

    //Block 80000 - 000000000043a8c0fd1d6f726790caa2a406010d19efd2780db27bdbbd93baf6
    // timestamp = 1284613427
    //             Sep 16, 2010 2:03:47 AM

    std::string spender_enc = "01000000ba8b9cda965dd8e536670f9ddec10e53aab14b20bacad27b9137190000000000190760b278fe7b8565fda3b968b918d5fd997f993b23674c0af3b6fde300b38f"
                              "33a5914c"
                              "e6ed5b1b01e32f57"
                              "02"
                              "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b014effffffff0100f2052a01000000434104b68a50eaa0287eff855189f949c1c6e5f58b37c88231373d8a59809cbae83059cc6469d65c665ccfd1cfeb75c6e8e19413bba7fbff9bc762419a76d87b16086eac00000000"
                              "0100000001a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f5000000004948304502206e21798a42fae0e854281abd38bacd1aeed3ee3738d9e1446618c4571d1090db022100e2ac980643b0b82c0e88ffdfec6b64e3e6ba35e7ba5fdd7d5d6cc8d25c6b241501ffffffff0100f2052a010000001976a914404371705fa9bd789a2fcd52d2c580b65d35549d88ac00000000";
    auto const spender80000 = get_block(spender_enc);

    // Dummy blocks to spend
    auto spender80000b = spender80000;
    spender80000b.header().set_version(2);  //To change the block hash
    spender80000b.header().set_timestamp(spender80000b.header().timestamp() + 600 * 1);
    spender80000b.transactions()[0].set_version(2); //To change the coinbase tx hash
    {
        spender80000b.transactions()[1].inputs().push_back(spender80000b.transactions()[1].inputs()[0]);
        spender80000b.transactions()[1].inputs().push_back(spender80000b.transactions()[1].inputs()[0]);
        spender80000b.transactions()[1].inputs().push_back(spender80000b.transactions()[1].inputs()[0]);

        hash_digest txid;
        REQUIRE(decode_hash(txid, "0e3e2357e806b6cdb1f70b54c3a3a17b6714ee1f0e68bebb44a74b1efd512098"));
        spender80000b.transactions()[1].inputs()[0].previous_output().set_hash(txid);
        REQUIRE(decode_hash(txid, "9b0fc92260312ce44e74ef369f5c66bbb85848f2eddd5a7a1cde251e54ccfdd5"));
        spender80000b.transactions()[1].inputs()[1].previous_output().set_hash(txid);
        REQUIRE(decode_hash(txid, "999e1c837c76a1b7fbb7e57baf87b309960f5ffefbf2a9b95dd890602272f644"));
        spender80000b.transactions()[1].inputs()[2].previous_output().set_hash(txid);
        REQUIRE(decode_hash(txid, "df2b060fa2e5e9c8ed5eaf6a45c13753ec8c63282b2688322eba40cd98ea067a"));
        spender80000b.transactions()[1].inputs()[3].previous_output().set_hash(txid);

    }
    // std::println("src/database/test/internal_database.cpp", encode_hash(spender80000b.transactions()[1].inputs()[0].previous_output().hash()));


    using my_clock = dummy_clock<1284613427>;

    KTH_DB_env* env_;
    KTH_DB_dbi dbi_utxo_;
    KTH_DB_dbi dbi_reorg_pool_;
    KTH_DB_dbi dbi_reorg_index_;
    KTH_DB_dbi dbi_reorg_block_;
    KTH_DB_dbi dbi_block_header_;
    KTH_DB_dbi dbi_block_header_by_hash_;


    KTH_DB_dbi dbi_block_db_;


    KTH_DB_dbi dbi_transaction_db_;
    KTH_DB_dbi dbi_transaction_hash_db_;
    KTH_DB_dbi dbi_transaction_unconfirmed_db_;
    KTH_DB_dbi dbi_history_db_;
    KTH_DB_dbi dbi_spend_db_;

    //KTH_DB_txn* db_txn;

    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 86, db_size, true); // 1 to 86 no entra el primero
        REQUIRE(db.open());

        REQUIRE(db.push_block(genesis, 0, 1) == result_code::success);
        REQUIRE(db.push_block(b1, 1, 1)      == result_code::success);
        REQUIRE(db.push_block(b2, 2, 1)      == result_code::success);
        REQUIRE(db.push_block(b3, 3, 1)      == result_code::success);
        REQUIRE(db.push_block(b4, 4, 1)      == result_code::success);
        REQUIRE(db.push_block(b79880, 5, 1)  == result_code::success);

    }   //close() implicit


    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 0);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 0);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 0);

    REQUIRE(db_count_items(env_, dbi_utxo_) == 6);
    REQUIRE(db_count_items(env_, dbi_block_header_) == 6);
    REQUIRE(db_count_items(env_, dbi_block_header_by_hash_) == 6);



    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);

    // check_reorg_output_doesnt_exists(env_, dbi_reorg_pool_, "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6", 0);
    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
        , dbi_transaction_unconfirmed_db_
    );




    // ------------------------------------------------------------------------------------
    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 86, db_size, true); // 1 to 86 no entra el primero
        REQUIRE(db.open());

        REQUIRE(db.push_block(spender80000, 6, 1) == result_code::success);

    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 1);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 1);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 1);
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 6) == 1);
    REQUIRE(db_exists_height(env_, dbi_reorg_block_, 6));
    REQUIRE( ! db_exists_height(env_, dbi_reorg_block_, 7));
    REQUIRE(db_count_items(env_, dbi_utxo_) == 7);
    REQUIRE(db_count_items(env_, dbi_block_header_) == 7);
    REQUIRE(db_count_items(env_, dbi_block_header_by_hash_) == 7);




    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
        , dbi_transaction_unconfirmed_db_
    );


    // ------------------------------------------------------------------------------------
    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 86, db_size, true); // 1 to 86 no entra el primero
        REQUIRE(db.open());

        REQUIRE(db.push_block(spender80000b, 7, 1) == result_code::success);

    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 2);
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 6) == 1);
    REQUIRE(db_exists_height(env_, dbi_reorg_block_, 6));
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 7) == 4);
    REQUIRE(db_exists_height(env_, dbi_reorg_block_, 7));
    REQUIRE(db_count_items(env_, dbi_utxo_) == 5);
    REQUIRE(db_count_items(env_, dbi_block_header_) == 8);
    REQUIRE(db_count_items(env_, dbi_block_header_by_hash_) == 8);



    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 7);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );



    // ------------------------------------------------------------------------------------
    // Prunes
    // ------------------------------------------------------------------------------------

    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 86, db_size, true);
        REQUIRE(db.open());
        REQUIRE(db.prune() == result_code::no_data_to_prune);
    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 2);
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 6) == 1);
    REQUIRE(db_exists_height(env_, dbi_reorg_block_, 6));
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 7) == 4);
    REQUIRE(db_exists_height(env_, dbi_reorg_block_, 7));



    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 7);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );

    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 11, db_size, true);
        REQUIRE(db.open());
        REQUIRE(db.prune() == result_code::no_data_to_prune);
    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 2);
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 6) == 1);
    REQUIRE(db_exists_height(env_, dbi_reorg_block_, 6));
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 7) == 4);
    REQUIRE(db_exists_height(env_, dbi_reorg_block_, 7));



    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 7);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );

    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 10, db_size, true);
        REQUIRE(db.open());
        REQUIRE(db.prune() == result_code::no_data_to_prune);
    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 2);
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 6) == 1);
    REQUIRE(db_exists_height(env_, dbi_reorg_block_, 6));
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 7) == 4);
    REQUIRE(db_exists_height(env_, dbi_reorg_block_, 7));



    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 7);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );

    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 5, db_size,true);
        REQUIRE(db.open());
        REQUIRE(db.prune() == result_code::no_data_to_prune);
    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 2);
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 6) == 1);
    REQUIRE(db_exists_height(env_, dbi_reorg_block_, 6));
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 7) == 4);
    REQUIRE(db_exists_height(env_, dbi_reorg_block_, 7));


    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 7);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );

    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 4, db_size, true);
        REQUIRE(db.open());
        REQUIRE(db.prune() == result_code::no_data_to_prune);
    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 2);
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 6) == 1);
    REQUIRE(db_exists_height(env_, dbi_reorg_block_, 6));
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 7) == 4);
    REQUIRE(db_exists_height(env_, dbi_reorg_block_, 7));



    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 7);


    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );

    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 1, db_size, true);
        REQUIRE(db.open());
        REQUIRE(db.prune() == result_code::success);
    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 4);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 4);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 1);
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 6) == 0);
    REQUIRE( !  db_exists_height(env_, dbi_reorg_block_, 6));
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 7) == 4);
    REQUIRE(db_exists_height(env_, dbi_reorg_block_, 7));



    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 7);


    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );


    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 0, db_size, true);
        REQUIRE(db.open());
        REQUIRE(db.prune() == result_code::success);
    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 0);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 0);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 0);
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 6) == 0);
    REQUIRE( !  db_exists_height(env_, dbi_reorg_block_, 6));
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 7) == 0);
    REQUIRE( !  db_exists_height(env_, dbi_reorg_block_, 7));




    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 7);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_


        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );

}


TEST_CASE("internal database  prune 3", "[None]") {

    // Block 0
    auto const genesis = get_genesis();

    // Block 1 - 00000000839a8e6886ab5951d76f411475428afc90947ee320161bbf18eb6048
    auto const b1 = get_block("010000006fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000982051fd1e4ba744bbbe680e1fee14677ba1a3c3540bf7b1cdb606e857233e0e61bc6649ffff001d01e362990101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d0104ffffffff0100f2052a0100000043410496b538e853519c726a2c91e61ec11600ae1390813a627c66fb8be7947be63c52da7589379515d4e0a604f8141781e62294721166bf621e73a82cbf2342c858eeac00000000");

    // Block 2 - 000000006a625f06636b8bb6ac7b960a8d03705d1ace08b1a19da3fdcc99ddbd
    auto const b2 = get_block("010000004860eb18bf1b1620e37e9490fc8a427514416fd75159ab86688e9a8300000000d5fdcc541e25de1c7a5addedf24858b8bb665c9f36ef744ee42c316022c90f9bb0bc6649ffff001d08d2bd610101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d010bffffffff0100f2052a010000004341047211a824f55b505228e4c3d5194c1fcfaa15a456abdf37f9b9d97a4040afc073dee6c89064984f03385237d92167c13e236446b417ab79a0fcae412ae3316b77ac00000000");

    // Block 3 - 0000000082b5015589a3fdf2d4baff403e6f0be035a5d9742c1cae6295464449
    auto const b3 = get_block("01000000bddd99ccfda39da1b108ce1a5d70038d0a967bacb68b6b63065f626a0000000044f672226090d85db9a9f2fbfe5f0f9609b387af7be5b7fbb7a1767c831c9e995dbe6649ffff001d05e0ed6d0101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d010effffffff0100f2052a0100000043410494b9d3e76c5b1629ecf97fff95d7a4bbdac87cc26099ada28066c6ff1eb9191223cd897194a08d0c2726c5747f1db49e8cf90e75dc3e3550ae9b30086f3cd5aaac00000000");

    // Block 4 - 000000004ebadb55ee9096c9a2f8880e09da59c0d68b1c228da88e48844a1485
    auto const b4 = get_block("010000004944469562ae1c2c74d9a535e00b6f3e40ffbad4f2fda3895501b582000000007a06ea98cd40ba2e3288262b28638cec5337c1456aaf5eedc8e9e5a20f062bdf8cc16649ffff001d2bfee0a90101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d011affffffff0100f2052a01000000434104184f32b212815c6e522e66686324030ff7e5bf08efb21f8b00614fb7690e19131dd31304c54f37baa40db231c918106bb9fd43373e37ae31a0befc6ecaefb867ac00000000");

    // Block 79880 - 00000000002e872c6fbbcf39c93ef0d89e33484ebf457f6829cbf4b561f3af5a - timestamp = 1284561413 - Sep 15, 2010 11:36:53 AM
    auto const b79880 = get_block("01000000a594fda9d85f69e762e498650d6fdb54d838657cea7841915203170000000000a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f505da904ce6ed5b1b017fe8070101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b015cffffffff0100f2052a01000000434104283338ffd784c198147f99aed2cc16709c90b1522e3b3637b312a6f9130e0eda7081e373a96d36be319710cd5c134aaffba81ff08650d7de8af332fe4d8cde20ac00000000");

    // Spender blocks ....

    //Block 80000 - 000000000043a8c0fd1d6f726790caa2a406010d19efd2780db27bdbbd93baf6
    // timestamp = 1284613427
    //             Sep 16, 2010 2:03:47 AM

    std::string spender_enc = "01000000ba8b9cda965dd8e536670f9ddec10e53aab14b20bacad27b9137190000000000190760b278fe7b8565fda3b968b918d5fd997f993b23674c0af3b6fde300b38f"
                              "33a5914c"
                              "e6ed5b1b01e32f57"
                              "02"
                              "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b014effffffff0100f2052a01000000434104b68a50eaa0287eff855189f949c1c6e5f58b37c88231373d8a59809cbae83059cc6469d65c665ccfd1cfeb75c6e8e19413bba7fbff9bc762419a76d87b16086eac00000000"
                              "0100000001a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f5000000004948304502206e21798a42fae0e854281abd38bacd1aeed3ee3738d9e1446618c4571d1090db022100e2ac980643b0b82c0e88ffdfec6b64e3e6ba35e7ba5fdd7d5d6cc8d25c6b241501ffffffff0100f2052a010000001976a914404371705fa9bd789a2fcd52d2c580b65d35549d88ac00000000";
    auto const spender80000 = get_block(spender_enc);

    // Dummy blocks to spend
    auto spender80000b = spender80000;
    spender80000b.header().set_version(2);  //To change the block hash
    spender80000b.header().set_timestamp(spender80000b.header().timestamp() + 600 * 1);
    spender80000b.transactions()[0].set_version(2); //To change the coinbase tx hash
    {
        spender80000b.transactions()[1].inputs().push_back(spender80000b.transactions()[1].inputs()[0]);
        spender80000b.transactions()[1].inputs().push_back(spender80000b.transactions()[1].inputs()[0]);
        spender80000b.transactions()[1].inputs().push_back(spender80000b.transactions()[1].inputs()[0]);

        hash_digest txid;
        REQUIRE(decode_hash(txid, "0e3e2357e806b6cdb1f70b54c3a3a17b6714ee1f0e68bebb44a74b1efd512098"));
        spender80000b.transactions()[1].inputs()[0].previous_output().set_hash(txid);
        REQUIRE(decode_hash(txid, "9b0fc92260312ce44e74ef369f5c66bbb85848f2eddd5a7a1cde251e54ccfdd5"));
        spender80000b.transactions()[1].inputs()[1].previous_output().set_hash(txid);
        REQUIRE(decode_hash(txid, "999e1c837c76a1b7fbb7e57baf87b309960f5ffefbf2a9b95dd890602272f644"));
        spender80000b.transactions()[1].inputs()[2].previous_output().set_hash(txid);
        REQUIRE(decode_hash(txid, "df2b060fa2e5e9c8ed5eaf6a45c13753ec8c63282b2688322eba40cd98ea067a"));
        spender80000b.transactions()[1].inputs()[3].previous_output().set_hash(txid);

    }
    // std::println("src/database/test/internal_database.cpp", encode_hash(spender80000b.transactions()[1].inputs()[0].previous_output().hash()));


    using my_clock = dummy_clock<1284613427>;

    KTH_DB_env* env_;
    KTH_DB_dbi dbi_utxo_;
    KTH_DB_dbi dbi_reorg_pool_;
    KTH_DB_dbi dbi_reorg_index_;
    KTH_DB_dbi dbi_reorg_block_;
    KTH_DB_dbi dbi_block_header_;
    KTH_DB_dbi dbi_block_header_by_hash_;


    KTH_DB_dbi dbi_block_db_;


    KTH_DB_dbi dbi_transaction_db_;
    KTH_DB_dbi dbi_transaction_hash_db_;
    KTH_DB_dbi dbi_transaction_unconfirmed_db_;
    KTH_DB_dbi dbi_history_db_;
    KTH_DB_dbi dbi_spend_db_;

    //KTH_DB_txn* db_txn;

    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 86, db_size, true); // 1 to 86 no entra el primero
        REQUIRE(db.open());

        REQUIRE(db.push_block(genesis, 0, 1) == result_code::success);
        REQUIRE(db.push_block(b1, 1, 1)      == result_code::success);
        REQUIRE(db.push_block(b2, 2, 1)      == result_code::success);
        REQUIRE(db.push_block(b3, 3, 1)      == result_code::success);
        REQUIRE(db.push_block(b4, 4, 1)      == result_code::success);
        REQUIRE(db.push_block(b79880, 5, 1)  == result_code::success);

    }   //close() implicit


    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 0);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 0);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 0);

    REQUIRE(db_count_items(env_, dbi_utxo_) == 6);
    REQUIRE(db_count_items(env_, dbi_block_header_) == 6);
    REQUIRE(db_count_items(env_, dbi_block_header_by_hash_) == 6);


    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);


    // check_reorg_output_doesnt_exists(env_, dbi_reorg_pool_, "f5d8ee39a430901c91a5917b9f2dc19d6d1a0e9cea205b009ca73dd04470b9a6", 0);
    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );


    // ------------------------------------------------------------------------------------
    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 86, db_size, true); // 1 to 86 no entra el primero
        REQUIRE(db.open());

        REQUIRE(db.push_block(spender80000b, 6, 1) == result_code::success);

    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 4);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 4);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 1);
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 6) == 4);
    REQUIRE(db_exists_height(env_, dbi_reorg_block_, 6));
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 7) == 0);
    REQUIRE( !  db_exists_height(env_, dbi_reorg_block_, 7));
    REQUIRE(db_count_items(env_, dbi_utxo_) == 4);
    REQUIRE(db_count_items(env_, dbi_block_header_) == 7);
    REQUIRE(db_count_items(env_, dbi_block_header_by_hash_) == 7);



    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );


    // ------------------------------------------------------------------------------------
    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 86, db_size, true); // 1 to 86 no entra el primero
        REQUIRE(db.open());

        REQUIRE(db.push_block(spender80000, 7, 1) == result_code::success);

    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 5);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 2);
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 6) == 4);
    REQUIRE(db_exists_height(env_, dbi_reorg_block_, 6));
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 7) == 1);
    REQUIRE(db_exists_height(env_, dbi_reorg_block_, 7));
    REQUIRE(db_count_items(env_, dbi_utxo_) == 5);
    REQUIRE(db_count_items(env_, dbi_block_header_) == 8);
    REQUIRE(db_count_items(env_, dbi_block_header_by_hash_) == 8);


    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 7);


    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_


        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );

    // ------------------------------------------------------------------------------------
    // Prunes
    // ------------------------------------------------------------------------------------

    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 1, db_size, true);
        REQUIRE(db.open());
        REQUIRE(db.prune() == result_code::success);
    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 1);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 1);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 1);
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 6) == 0);
    REQUIRE( !  db_exists_height(env_, dbi_reorg_block_, 6));
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 7) == 1);
    REQUIRE(db_exists_height(env_, dbi_reorg_block_, 7));



    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 7);

    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );


    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 0, db_size, true);
        REQUIRE(db.open());
        REQUIRE(db.prune() == result_code::success);
    }   //close() implicit

    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 0);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 0);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 0);
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 6) == 0);
    REQUIRE( !  db_exists_height(env_, dbi_reorg_block_, 6));
    REQUIRE(db_count_index_by_height(env_, dbi_reorg_index_, 7) == 0);
    REQUIRE( !  db_exists_height(env_, dbi_reorg_block_, 7));


    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 3);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 4);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 5);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 6);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 7);


    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_


        , dbi_block_db_


        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );

}

TEST_CASE("internal database  prune empty blockchain", "[None]") {
    using my_clock = dummy_clock<1284613427>;
    internal_database_basis<my_clock> db(db_path, db_mode_type::full, 4, db_size, true);
    REQUIRE(db.open());
    REQUIRE(db.prune() == result_code::no_data_to_prune);
}

TEST_CASE("internal database  prune empty reorg pool", "[None]") {
    using my_clock = dummy_clock<1284613427>;
    internal_database_basis<my_clock> db(db_path, db_mode_type::full, 1000, db_size, true);
    REQUIRE(db.open());
    REQUIRE(db.push_block(get_genesis(), 0, 1) == result_code::success);
    REQUIRE(db.prune() == result_code::no_data_to_prune);
}


TEST_CASE("internal database  prune empty reorg pool 2", "[None]") {
    using my_clock = dummy_clock<1284613427>;
    internal_database_basis<my_clock> db(db_path, db_mode_type::full, 0, db_size, true);
    REQUIRE(db.open());
    REQUIRE(db.push_block(get_genesis(), 0, 1) == result_code::success);
    REQUIRE(db.prune() == result_code::no_data_to_prune);
}

TEST_CASE("internal database  prune empty reorg pool 3", "[None]") {
    using my_clock = dummy_clock<1284613427>;
    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 10000000, db_size, true);
        REQUIRE(db.open());
        REQUIRE(db.push_block(get_genesis(), 0, 1) == result_code::success);

        // Block 1 - 00000000839a8e6886ab5951d76f411475428afc90947ee320161bbf18eb6048
        auto const b1 = get_block("010000006fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000982051fd1e4ba744bbbe680e1fee14677ba1a3c3540bf7b1cdb606e857233e0e61bc6649ffff001d01e362990101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d0104ffffffff0100f2052a0100000043410496b538e853519c726a2c91e61ec11600ae1390813a627c66fb8be7947be63c52da7589379515d4e0a604f8141781e62294721166bf621e73a82cbf2342c858eeac00000000");
        REQUIRE(db.push_block(b1, 1, 1) == result_code::success);


        std::string spender_enc = "01000000ba8b9cda965dd8e536670f9ddec10e53aab14b20bacad27b9137190000000000190760b278fe7b8565fda3b968b918d5fd997f993b23674c0af3b6fde300b38f"
                                "33a5914c"
                                "e6ed5b1b01e32f57"
                                "02"
                                "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704e6ed5b1b014effffffff0100f2052a01000000434104b68a50eaa0287eff855189f949c1c6e5f58b37c88231373d8a59809cbae83059cc6469d65c665ccfd1cfeb75c6e8e19413bba7fbff9bc762419a76d87b16086eac00000000"
                                "0100000001a6b97044d03da79c005b20ea9c0e1a6d9dc12d9f7b91a5911c9030a439eed8f5000000004948304502206e21798a42fae0e854281abd38bacd1aeed3ee3738d9e1446618c4571d1090db022100e2ac980643b0b82c0e88ffdfec6b64e3e6ba35e7ba5fdd7d5d6cc8d25c6b241501ffffffff0100f2052a010000001976a914404371705fa9bd789a2fcd52d2c580b65d35549d88ac00000000";
        auto const spender80000 = get_block(spender_enc);

        // Dummy blocks to spend
        auto spender80000b = spender80000;
        spender80000b.header().set_version(2);  //To change the block hash
        spender80000b.header().set_timestamp(spender80000b.header().timestamp() + 600 * 1);
        spender80000b.transactions()[0].set_version(2); //To change the coinbase tx hash
        spender80000b.transactions()[1].inputs().push_back(spender80000b.transactions()[1].inputs()[0]);
        hash_digest txid;
        REQUIRE(decode_hash(txid, "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"));
        spender80000b.transactions()[1].inputs()[0].previous_output().set_hash(txid);
        REQUIRE(decode_hash(txid, "0e3e2357e806b6cdb1f70b54c3a3a17b6714ee1f0e68bebb44a74b1efd512098"));
        spender80000b.transactions()[1].inputs()[1].previous_output().set_hash(txid);

        REQUIRE(db.push_block(spender80000b, 2, 1) == result_code::success);
    }


    KTH_DB_env* env_;
    KTH_DB_dbi dbi_utxo_;
    KTH_DB_dbi dbi_reorg_pool_;
    KTH_DB_dbi dbi_reorg_index_;
    KTH_DB_dbi dbi_reorg_block_;
    KTH_DB_dbi dbi_block_header_;
    KTH_DB_dbi dbi_block_header_by_hash_;


    KTH_DB_dbi dbi_block_db_;


    KTH_DB_dbi dbi_transaction_db_;
    KTH_DB_dbi dbi_transaction_hash_db_;
    KTH_DB_dbi dbi_transaction_unconfirmed_db_;
    KTH_DB_dbi dbi_history_db_;
    KTH_DB_dbi dbi_spend_db_;

    //KTH_DB_txn* db_txn;
    std::tie(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

    , dbi_block_db_

    , dbi_transaction_db_
    , dbi_history_db_
    , dbi_spend_db_
    , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    ) = open_dbs();

    REQUIRE(db_count_items(env_, dbi_reorg_pool_) == 2);
    REQUIRE(db_count_items(env_, dbi_reorg_index_) == 2);
    REQUIRE(db_count_items(env_, dbi_reorg_block_) == 3);


    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 0);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 1);
    check_blocks_db(env_, dbi_block_db_, dbi_block_header_, dbi_transaction_db_, 2);


    close_everything(env_, dbi_utxo_, dbi_reorg_pool_, dbi_reorg_index_, dbi_block_header_, dbi_block_header_by_hash_, dbi_reorg_block_

        , dbi_block_db_

        , dbi_transaction_db_
        , dbi_history_db_
        , dbi_spend_db_
        , dbi_transaction_hash_db_
    , dbi_transaction_unconfirmed_db_
    );

    {
        internal_database_basis<my_clock> db(db_path, db_mode_type::full, 3, db_size,true);
        REQUIRE(db.open());
        REQUIRE(db.prune() == result_code::no_data_to_prune);
    }
}





/*
------------------------------------------------------------------------------------------
Block #170
BlockHash 00000000d1145790a8694403d4063f323d499e655c83426834d4ce2f8dd4a2ee
0100000055bd840a78798ad0da853f68974f3d183e2bd1db6a842c1feecf222a00000000ff104ccb05421ab93e63f8c3ce5c2c2e9dbb37de2764b3a3175c8166562cac7d51b96a49ffff001d283e9e700201000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d0102ffffffff0100f2052a01000000434104d46c4968bde02899d2aa0963367c7a6ce34eec332b32e42e5f3407e052d64ac625da6f0718e7b302140434bd725706957c092db53805b821a85b23a7ac61725bac000000000100000001c997a5e56e104102fa209c6a852dd90660a20b2d9c352423edce25857fcd3704000000004847304402204e45e16932b8af514961a1d3a1a25fdf3f4f7732e9d624c6c61548ab5fb8cd410220181522ec8eca07de4860a4acdd12909d831cc56cbbac4622082221a8768d1d0901ffffffff0200ca9a3b00000000434104ae1a62fe09c5f51b13905f07f06b99a2f7159b2225f374cd378d71302fa28414e7aab37397f554a7df5f142c21c1b7303b8a0626f1baded5c72a704f7e6cd84cac00286bee0000000043410411db93e1dcdb8a016b49840f8c53bc1eb68a382e97b1482ecad7b148a6909a5cb2e0eaddfb84ccf9744464f82e160bfa9b8b64f9d4c03f999b8643f656b412a3ac00000000


Tx0: Coinbase

Tx1
    hash:           f4184fc596403b9d638783cf57adfe4c75c605f6356fbc91338530e9831e9e16
    bytes hexa:     0100000001c997a5e56e104102fa209c6a852dd90660a20b2d9c352423edce25857fcd3704000000004847304402204e45e16932b8af514961a1d3a1a25fdf3f4f7732e9d624c6c61548ab5fb8cd410220181522ec8eca07de4860a4acdd12909d831cc56cbbac4622082221a8768d1d0901ffffffff0200ca9a3b00000000434104ae1a62fe09c5f51b13905f07f06b99a2f7159b2225f374cd378d71302fa28414e7aab37397f554a7df5f142c21c1b7303b8a0626f1baded5c72a704f7e6cd84cac00286bee0000000043410411db93e1dcdb8a016b49840f8c53bc1eb68a382e97b1482ecad7b148a6909a5cb2e0eaddfb84ccf9744464f82e160bfa9b8b64f9d4c03f999b8643f656b412a3ac00000000
    prev out:       0437cd7f8525ceed2324359c2d0ba26006d92d856a9c20fa0241106ee5a597c9        Block #9


------------------------------------------------------------------------------------------
Block #9
BlockHash 000000008d9dc510f23c2657fc4f67bea30078cc05a90eb89e84cc475c080805
01000000c60ddef1b7618ca2348a46e868afc26e3efc68226c78aa47f8488c4000000000c997a5e56e104102fa209c6a852dd90660a20b2d9c352423edce25857fcd37047fca6649ffff001d28404f530101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d0134ffffffff0100f2052a0100000043410411db93e1dcdb8a016b49840f8c53bc1eb68a382e97b1482ecad7b148a6909a5cb2e0eaddfb84ccf9744464f82e160bfa9b8b64f9d4c03f999b8643f656b412a3ac00000000

------------------------------------------------------------------------------------------
Block #5217
BlockHash 0000000025075f093c42a0393c844bc59f90024b18a9f588f6fa3fc37487c3c2
01000000944bb801c604dda3d51758f292afdca8d973288434c8fe4bf0b5982d000000008a7d204ffe05282b05f280459401b59be41b089cefc911f4fb5641f90309d942b929a149ffff001d1b8d847f0301000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0804ffff001d02c500ffffffff0100f2052a01000000434104f4af426e464d972012256f4cbe5df528aa99b1ceb489968a56cf6b295e6fad1473be89f66fbd3d16adf3dfba7c5253517d11d1d188fe858720497c4fc0a1ef9dac00000000010000000465dabdbdb83e9820e4f666f3634d88308909789f7ae29e730812784a96485e3c000000004948304502204c52c2301dcc3f6af7a3ef2ad118185ca2d52a7ae90013332ad53732a085b8d4022100f074ab99e77d5d4c54eb6bfc82c42a094d7d7eaf632d52897ef058c617a2bb2301ffffffffb7404d6a9c451a9f527a7fbeb54839c2bca2eac7b138cdd700be19d733efa0fc000000004847304402206c55518aa596824d1e760afcfeb7b0103a1a82ea8dcd4c3474becc8246ba823702205009cbc40affa3414f9a139f38a86f81a401193f759fb514b9b1d4e2e49f82a401ffffffffcc07817ab589551d698ba7eb2a6efd6670d6951792ad52e2bd5832bf2f4930ec0000000049483045022100b485b4daa4af75b7b34b4f2338e7b96809c75fab5577905ade0789c7f821a69e022010d73d2a3c7fcfc6db911dead795b0aa7d5448447ad5efc7e516699955a18ac801fffffffff61fefef8ee758b273ee64e1bf5c07485dd74cd065a5ce0d59827e0700cad0d9000000004a493046022100bc6e89ee580d1c721b15c36d0a1218c9e78f6f7537616553341bbd1199fe615a02210093062f2c1a1c87f55b710011976a03dff57428e38dd640f6fbdef0fa52ad462d01ffffffff0100c817a80400000043410408998c08bbe6bba756e9b864722fe76ca403929382db2b120f9f621966b00af48f4b014b458bccd4f2acf63b1487ecb9547bc87bdecb08e9c4d08c138c76439aac00000000010000000115327dc99375fc1fdc02e15394369daa6e23ad4dc27e7c1c4af21606add5b068000000004a49304602210086b55b7f2fa5395d1e90a85115ada930afa01b86116d6bbeeecd8e2b97eefbac022100d653846d378845df2ced4b4923dcae4b6ddd5e8434b25e1602928235054c8d5301ffffffff0100f2052a01000000434104b68b035858a00051ca70dd4ba297168d9a3720b642c2e0cd08846bfbb144233b11b24c4b8565353b579bd7109800e42a1fc1e20dbdfbba6a12d0089aab313181ac00000000


Tx0: Coinbase

Tx1
    hash:           9fa5efd12e4bdba914bf1acd03981c6e31eabaa8a8bd85fc2be36afe5a787c06
    bytes hexa:     010000000465dabdbdb83e9820e4f666f3634d88308909789f7ae29e730812784a96485e3c000000004948304502204c52c2301dcc3f6af7a3ef2ad118185ca2d52a7ae90013332ad53732a085b8d4022100f074ab99e77d5d4c54eb6bfc82c42a094d7d7eaf632d52897ef058c617a2bb2301ffffffffb7404d6a9c451a9f527a7fbeb54839c2bca2eac7b138cdd700be19d733efa0fc000000004847304402206c55518aa596824d1e760afcfeb7b0103a1a82ea8dcd4c3474becc8246ba823702205009cbc40affa3414f9a139f38a86f81a401193f759fb514b9b1d4e2e49f82a401ffffffffcc07817ab589551d698ba7eb2a6efd6670d6951792ad52e2bd5832bf2f4930ec0000000049483045022100b485b4daa4af75b7b34b4f2338e7b96809c75fab5577905ade0789c7f821a69e022010d73d2a3c7fcfc6db911dead795b0aa7d5448447ad5efc7e516699955a18ac801fffffffff61fefef8ee758b273ee64e1bf5c07485dd74cd065a5ce0d59827e0700cad0d9000000004a493046022100bc6e89ee580d1c721b15c36d0a1218c9e78f6f7537616553341bbd1199fe615a02210093062f2c1a1c87f55b710011976a03dff57428e38dd640f6fbdef0fa52ad462d01ffffffff0100c817a80400000043410408998c08bbe6bba756e9b864722fe76ca403929382db2b120f9f621966b00af48f4b014b458bccd4f2acf63b1487ecb9547bc87bdecb08e9c4d08c138c76439aac00000000
    prev out:       3c5e48964a781208739ee27a9f78098930884d63f366f6e420983eb8bdbdda65        Block #4334
                    fca0ef33d719be00d7cd38b1c7eaa2bcc23948b5be7f7a529f1a459c6a4d40b7        Block #4966
                    ec30492fbf3258bde252ad921795d67066fd6e2aeba78b691d5589b57a8107cc        Block #4561
                    d9d0ca00077e82590dcea565d04cd75d48075cbfe164ee73b258e78eefef1ff6        Block #4991

Tx2
    hash:           bea2f18440827950003bfcc130d0a82ebff013cec3848cdc8cb1c06e15672333
    bytes hexa:     010000000115327dc99375fc1fdc02e15394369daa6e23ad4dc27e7c1c4af21606add5b068000000004a49304602210086b55b7f2fa5395d1e90a85115ada930afa01b86116d6bbeeecd8e2b97eefbac022100d653846d378845df2ced4b4923dcae4b6ddd5e8434b25e1602928235054c8d5301ffffffff0100f2052a01000000434104b68b035858a00051ca70dd4ba297168d9a3720b642c2e0cd08846bfbb144233b11b24c4b8565353b579bd7109800e42a1fc1e20dbdfbba6a12d0089aab313181ac00000000
    prev out:       68b0d5ad0616f24a1c7c7ec24dad236eaa9d369453e102dc1ffc7593c97d3215        Block #4556



------------------------------------------------------------------------------------------
Block #4334
BlockHash 000000009cdad3c55df9c9bc88265329254a6c8ca810fa7f0e953c947df86dc7
010000005a0aeca67f7e43582c2b0138b2daa0c8dc1bedbb2477cfba2d3f96bf0000000065dabdbdb83e9820e4f666f3634d88308909789f7ae29e730812784a96485e3cd5899749ffff001dc2544c010101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0804ffff001d028e00ffffffff0100f2052a01000000434104b48f20398caaf3ff5d40710e0af87a4b86fd19d125ddacf15a2a023831d1731350e5fd40d0e28bb6481ad1843847213764feb98a2dd041069a8c39c842e1da93ac00000000

Block #4966
BlockHash 000000004f6a440a95a5d2d6c89f3e6b46587cd43f76efbaa96ef5d37ea90961
010000002cba11cca7170e1da742fcfb83d15c091bac17a79c089e944dda904a00000000b7404d6a9c451a9f527a7fbeb54839c2bca2eac7b138cdd700be19d733efa0fc82609e49ffff001df66339030101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d0121ffffffff0100f2052a01000000434104c20502b45fe276d418a32b55435cb4361dea4e173c36a8e0ad52518b17f2d48cde4336c8ac8d2270e6040469f11c56036db1feef803fb529e36d0f599261cb19ac00000000

Block #4561
BlockHash 0000000089abf237d732a1515f7066e7ba29e1833664da8b704c8575c0465223
0100000052df1ff74876f2de37db341e12f932768f2a18dcc09dd024a1e676aa00000000cc07817ab589551d698ba7eb2a6efd6670d6951792ad52e2bd5832bf2f4930ecb5f19949ffff001d4045c6010101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0804ffff001d029f00ffffffff0100f2052a010000004341044bdc62d08cc074664cef09b24b01125a5e92fc2d61f821d6fddac367eb80b06a0a6148feabb0d25717f9eb84950ef0d3e7fe49ce7fb5a6e14da881a5b2bc64c0ac00000000

Block #4991
BlockHash 00000000fa413253e1d30ff687f239b528330c810e3de86e42a538175682599d
0100000040eb019191a99f1f3ef5e04606314d80a635e214ca3347388259ad4000000000f61fefef8ee758b273ee64e1bf5c07485dd74cd065a5ce0d59827e0700cad0d98c9f9e49ffff001d20a92f010101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0704ffff001d013affffffff0100f2052a01000000434104debec4c8f781d5223a90aa90416ee51abf15d37086cc2d9be67873534fb06ae68c8a4e0ed0a7eedff9c97fe23f03e652d7f44501286dc1f75148bfaa9e386300ac00000000

Block #4556
BlockHash 0000000054d4f171b0eab3cd4e31da4ce5a1a06f27b39bf36c5902c9bb8ef5c4
0100000021a06106f13b4f0112a63e77fae3a48ffe10716ff3cdfb35371032990000000015327dc99375fc1fdc02e15394369daa6e23ad4dc27e7c1c4af21606add5b068dadf9949ffff001dda8444000101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff0804ffff001d029600ffffffff0100f2052a0100000043410475804bd8be2560ab2ebd170228429814d60e58d7415a93dc51f23e4cb2f8b5188dd56fa8d519e9ef9c16d6ab22c1c8304e10e74a28444eb26821948b2d1482a4ac00000000

*/

// End Test Suite
