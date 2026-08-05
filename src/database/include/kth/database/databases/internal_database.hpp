// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_INTERNAL_DATABASE_HPP_
#define KTH_DATABASE_INTERNAL_DATABASE_HPP_

#include <expected>
#include <filesystem>
#include <tuple>

#include <boost/interprocess/mapped_region.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <kth/database/databases/generic_db.hpp>
#include <kth/database/databases/header_abla_entry.hpp>
#include <kth/database/databases/property_code.hpp>
#include <kth/database/databases/result_code.hpp>
#include <kth/database/databases/tools.hpp>
#include <kth/database/define.hpp>
#include <kth/domain.hpp>
#include <kth/domain/chain/input_point.hpp>
#include <kth/infrastructure/path.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>

#ifdef KTH_INTERNAL_DB_4BYTES_INDEX
#define KTH_INTERNAL_DB_WIRE true
#else
#define KTH_INTERNAL_DB_WIRE false
#endif

#if defined(KTH_DB_READONLY)
#define KTH_DB_CONDITIONAL_CREATE 0
#else
#define KTH_DB_CONDITIONAL_CREATE KTH_DB_CREATE
#endif

#if defined(KTH_DB_READONLY)
#define KTH_DB_CONDITIONAL_READONLY KTH_DB_RDONLY
#else
#define KTH_DB_CONDITIONAL_READONLY 0
#endif

namespace kth::database {

/// Heights stored in the database (headers and validated blocks).
struct heights_t {
    uint32_t header;  ///< Height of the last header
    uint32_t block;   ///< Height of the last validated block
};

constexpr size_t max_dbs_full_ = 3;        // KTH_DB_NEW_FULL
constexpr size_t max_dbs_blocks_ = 3;      // KTH_DB_NEW_BLOCKS
constexpr size_t max_dbs_pruned_ = 3;       // KTH_DB_NEW_PRUNED

constexpr size_t env_open_mode_ = 0664;
constexpr int directory_exists = 0;

template <typename Clock = std::chrono::system_clock>
struct KD_API internal_database_basis {
    using path = kth::path;

    constexpr static char block_header_db_name[] = "block_header";
    constexpr static char block_header_by_hash_db_name[] = "block_header_by_hash";
    constexpr static char db_properties_name[] = "properties";

    internal_database_basis(path const& db_dir, db_mode_type mode, uint32_t reorg_pool_limit, uint64_t db_max_size, bool safe_mode);
    ~internal_database_basis();

    // Non-copyable, non-movable
    internal_database_basis(internal_database_basis const&) = delete;
    internal_database_basis& operator=(internal_database_basis const&) = delete;

#if ! defined(KTH_DB_READONLY)
    bool create();
#endif

    bool open();
    bool close();

#if ! defined(KTH_DB_READONLY)
    // ==========================================================================
    // DEPRECATED: Block storage moved to flat files (blk*.dat)
    // ==========================================================================
    // result_code push_genesis(domain::chain::block const& block);
    // result_code push_block(domain::chain::block const& block, uint32_t height, uint32_t median_time_past);
    // result_code push_block_fast(domain::chain::block const& block, uint32_t height);

    // ==========================================================================
    // DEPRECATED: UTXO storage moved to UTXOZ
    // ==========================================================================
    // result_code apply_utxo_delta(
    //     boost::unordered_flat_map<domain::chain::point, utxo_entry> const& inserts,
    //     boost::unordered_flat_map<domain::chain::point, uint32_t> const& deletes
    // );
    // result_code clear_utxo_set();

    // Get/set the last block height for which UTXO set was built
    std::expected<uint32_t, result_code> get_utxo_built_height() const;
    result_code set_utxo_built_height(uint32_t height);

    /// The in-flight batch marker (issue #600). Persisted before the first
    /// mutation of a batch and cleared only once its delta, its deferred
    /// deletions and its built height have all landed. Stored as first + 1 so
    /// that zero — the value a missing key reads as — means clean, and height
    /// zero stays expressible.
    std::expected<std::optional<uint32_t>, result_code> get_utxo_batch_dirty() const;
    result_code set_utxo_batch_dirty(uint32_t first_height);
    result_code clear_utxo_batch_dirty();

    // Headers-first sync: store header without full block data (ABLA state = zeros)
    result_code push_header(domain::chain::header const& header, uint32_t height);

    // Headers-first sync: store header with explicit ABLA state
    result_code push_header(domain::chain::header const& header, uint32_t height, uint64_t block_size, uint64_t control_block_size, uint64_t elastic_buffer_size);

    // Headers-first sync: store multiple headers in a single transaction (batch)
    // start_height is the height of the first header in the list
    result_code push_headers_batch(domain::chain::header::list const& headers, uint32_t start_height);

    /// Make the by-height table describe `headers` from `start_height` up, and
    /// nothing above them: writes each header, drops every height past the last
    /// one (with its hash -> height entry), and sets the last-header height.
    ///
    /// One transaction, because a reorganization needs all of it or none. Written
    /// halfway, the table would name the new branch over part of the replaced
    /// range and the abandoned one over the rest — an inconsistency no later pass
    /// would notice, since each height holds a header that parses.
    ///
    /// Both halves are needed: a branch with more work can have fewer blocks (it
    /// was mined at higher difficulty), so the chain can end lower than it did and
    /// leave heights behind that are on no chain at all.
    ///
    /// `start_height` must be at least 1 — genesis anchors the chain — and
    /// `headers` must not be empty: "replace with nothing from here up" would
    /// mean deleting the chain from that height, which is not what any caller
    /// means by it. Both are refused before the transaction opens.
    result_code replace_headers_from(domain::chain::header::list const& headers, uint32_t start_height);
#endif

    // DEPRECATED: UTXO storage moved to UTXOZ
    // std::expected<utxo_entry, result_code> get_utxo(domain::chain::output_point const& point) const;

    // Height tracking via properties table
    std::expected<heights_t, result_code> get_last_heights() const;

#if ! defined(KTH_DB_READONLY)
    result_code set_last_header_height(uint32_t height);
    result_code set_last_block_height(uint32_t height);
#endif

    std::expected<std::pair<domain::chain::header, uint32_t>, result_code> get_header(hash_digest const& hash) const;
    std::expected<domain::chain::header, result_code> get_header(uint32_t height) const;
    std::expected<domain::chain::header::list, result_code> get_headers(uint32_t from, uint32_t to) const;
    std::expected<header_with_abla_state_t, result_code> get_header_and_abla_state(uint32_t height) const;

    // ==========================================================================
    // DEPRECATED: Block storage moved to flat files (blk*.dat)
    // ==========================================================================
    // std::expected<std::pair<domain::chain::block, uint32_t>, result_code> get_block(hash_digest const& hash) const;
    // std::expected<domain::chain::block, result_code> get_block(uint32_t height) const;
    // std::expected<domain::chain::block::list, result_code> get_blocks(uint32_t from, uint32_t to) const;
    // std::expected<std::vector<data_chunk>, result_code> get_blocks_raw(uint32_t from, uint32_t to) const;

private:

#if ! defined(KTH_DB_READONLY)
    bool create_db_mode_property();
    bool create_height_properties();
#endif

    bool verify_db_mode_property() const;

    // Property helpers (with transaction)
    std::expected<uint32_t, result_code> get_property_height(property_code prop, KTH_DB_txn* db_txn) const;
#if ! defined(KTH_DB_READONLY)
    result_code set_property_height(property_code prop, uint32_t height);
    result_code set_property_height(property_code prop, uint32_t height, KTH_DB_txn* db_txn);
#endif

    bool open_internal();

    size_t get_db_page_size() const;

    size_t adjust_db_size(size_t size) const;

    bool create_and_open_environment();

    bool open_databases();

#if ! defined(KTH_DB_READONLY)
    // ==========================================================================
    // DEPRECATED: UTXO storage moved to UTXOZ, Block storage to flat files
    // ==========================================================================
    // result_code remove_utxo(uint32_t height, domain::chain::output_point const& point, bool insert_reorg, KTH_DB_txn* db_txn);
    // result_code insert_utxo(domain::chain::output_point const& point, domain::chain::output const& output, data_chunk const& fixed_data, KTH_DB_txn* db_txn);
    // result_code remove_inputs(hash_digest const& tx_id, uint32_t height, domain::chain::input::list const& inputs, bool insert_reorg, KTH_DB_txn* db_txn);
    // result_code insert_outputs(hash_digest const& tx_id, uint32_t height, domain::chain::output::list const& outputs, data_chunk const& fixed_data, KTH_DB_txn* db_txn);
    // result_code insert_outputs_error_treatment(uint32_t height, data_chunk const& fixed_data, hash_digest const& txid, domain::chain::output::list const& outputs, KTH_DB_txn* db_txn);
    // template <typename I>
    // result_code push_transactions_outputs_non_coinbase(uint32_t height, data_chunk const& fixed_data, I f, I l, KTH_DB_txn* db_txn);
    // template <typename I>
    // result_code remove_transactions_inputs_non_coinbase(uint32_t height, I f, I l, bool insert_reorg, KTH_DB_txn* db_txn);
    // template <typename I>
    // result_code push_transactions_non_coinbase(uint32_t height, data_chunk const& fixed_data, I f, I l, bool insert_reorg, KTH_DB_txn* db_txn);

    result_code push_block_header(domain::chain::block const& block, domain::chain::abla::state const& abla_state, uint32_t height, KTH_DB_txn* db_txn);

    // Headers-first sync: store header only (without block data)
    result_code push_header_only(domain::chain::header const& header, uint32_t height, KTH_DB_txn* db_txn);

    // Headers-first sync: store header with explicit ABLA state
    result_code push_header_with_abla(domain::chain::header const& header, uint32_t height, uint64_t block_size, uint64_t control_block_size, uint64_t elastic_buffer_size, KTH_DB_txn* db_txn);

    result_code remove_block_header(hash_digest const& hash, uint32_t height, KTH_DB_txn* db_txn);
#endif

    std::expected<domain::chain::header, result_code> get_header(uint32_t height, KTH_DB_txn* db_txn) const;
    std::expected<header_with_abla_state_t, result_code> get_header_and_abla_state(uint32_t height, KTH_DB_txn* db_txn) const;

// Data members ----------------------------
    path const db_dir_;
    bool env_created_ = false;
    bool db_opened_ = false;
    db_mode_type db_mode_;
    uint64_t db_max_size_;
    bool safe_mode_;
    //bool fast_mode = false;

    KTH_DB_env* env_;
    KTH_DB_dbi dbi_block_header_;
    KTH_DB_dbi dbi_block_header_by_hash_;
    KTH_DB_dbi dbi_properties_;
};

template <typename Clock>
constexpr char internal_database_basis<Clock>::block_header_db_name[];           //key: block height, value: block header

template <typename Clock>
constexpr char internal_database_basis<Clock>::block_header_by_hash_db_name[];   //key: block hash, value: block height

template <typename Clock>
constexpr char internal_database_basis<Clock>::db_properties_name[];             //key: propery, value: data

using internal_database = internal_database_basis<std::chrono::system_clock>;

} // namespace kth::database


#include <kth/database/databases/header_database.ipp>
#include <kth/database/databases/internal_database.ipp>

#endif // KTH_DATABASE_INTERNAL_DATABASE_HPP_
