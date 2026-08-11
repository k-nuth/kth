// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_INTERNAL_DATABASE_HPP_
#define KTH_DATABASE_INTERNAL_DATABASE_HPP_

#include <expected>
#include <filesystem>
#include <optional>
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
#include <kth/database/utxo_transition_record.hpp>
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

/// What the persisted transition record says about the last transition.
///
/// Four answers, not two, and the validation order that produces them is fixed
/// by the record's envelope: length, then checksum, then version, then the
/// fields. "Could not read" is never reported as "clean" — that conflation is
/// the failure the record exists to prevent, and its own reader would be a poor
/// place to reintroduce it.
enum class transition_status {
    clean,              ///< The key is absent. Nothing was interrupted.
    unreadable,         ///< A storage error other than not-found. Nothing is known.
    corrupt,            ///< Bytes are there and they are not a record this build can read.
    recovery_required,  ///< A valid record in `in_progress`: a transition did not finish.
};

/// The answer, with whatever is worth reporting alongside it.
struct transition_check {
    transition_status status{transition_status::clean};

    /// Set only when `status` is `recovery_required`. The type, id and height
    /// range the refusal message names — the id correlating the log line
    /// written at the failure with the record found at this start.
    std::optional<utxo_transition_record> record;

    /// Set only when `status` is `corrupt`. Which refusal the decoder reached,
    /// so a diagnosis does not have to guess between a truncated record and a
    /// version this build does not know.
    std::optional<transition_decode_error> decode_error;
};

/// The heights a completed transition publishes, written in the transaction
/// that clears the marker.
///
/// Optional per height because the two paths publish different sets: a connect
/// batch moves the built height only (the stored-block marker moved when the
/// bodies landed), while a reorganization rolls both back to the fork. An
/// absent member is not written — it is not "write zero".
struct transition_heights {
    std::optional<uint32_t> last_block_height;
    std::optional<uint32_t> utxo_built_height;
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

    // =========================================================================
    // The transition record (#600)
    // =========================================================================

    /// Record, durably, that a transition is about to mutate the stores.
    ///
    /// One put in one transaction, so the record is written whole or not at
    /// all. It does NOT reach the disk here: the environment is opened
    /// MDB_NOSYNC, so the caller must follow this with `env_sync()` before the
    /// first mutation, or the marker describes a window it does not cover.
    [[nodiscard]]
    result_code begin_transition_record(utxo_transition_record const& record);

    /// Move the heights, in one transaction, WITHOUT touching the record.
    ///
    /// For a step inside a transition rather than at the end of one: a
    /// reorganization rolls both heights back one block at a time, and each of
    /// those steps has to be all-or-nothing on its own — two separate writes
    /// leave the stored-block height and the built height naming different
    /// blocks — while the record must stay in place until the whole
    /// reorganization is published.
    [[nodiscard]]
    result_code set_heights(transition_heights const& heights);

    /// Publish the heights a completed transition reached AND clear the record,
    /// in ONE transaction.
    ///
    /// Together, because separately there is an instant where the height says
    /// "arrived" and the marker says "clean" independently — which is exactly
    /// the window the marker exists to close. Also not durable on return: the
    /// caller follows it with `env_sync()`.
    [[nodiscard]]
    result_code publish_transition(transition_heights const& heights);

    /// Put every committed transaction on stable storage.
    ///
    /// The environment is opened MDB_NOSYNC, so a commit returns before
    /// anything reaches the disk: without this call a durable marker is
    /// decorative, and a height marker can outlive — or be outlived by — the
    /// mutations it describes. Forced (`mdb_env_sync(env, 1)`): the unforced
    /// form does nothing under NOSYNC, which is the whole reason this exists.
    ///
    /// @return success, or `other` when the barrier failed. Never discarded.
    [[nodiscard]]
    result_code env_sync();

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

    /// What the persisted transition record says (#600). Outside the read-only
    /// guard on purpose: a build that only reads still has to refuse a database
    /// whose last transition did not finish.
    [[nodiscard]]
    transition_check read_transition_record() const;

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

    /// A property that is not a height. Distinguishes "the key is not there"
    /// (`key_not_found`) from "the read failed" (`other`), because the one
    /// caller there is exists to keep those apart.
    std::expected<data_chunk, result_code> get_property_blob(property_code prop, KTH_DB_txn* db_txn) const;

#if ! defined(KTH_DB_READONLY)
    result_code set_property_height(property_code prop, uint32_t height);
    result_code set_property_height(property_code prop, uint32_t height, KTH_DB_txn* db_txn);

    result_code set_property_blob(property_code prop, byte_span value, KTH_DB_txn* db_txn);

    /// The heights half of set_heights/publish_transition, so the two cannot
    /// drift apart in what they write.
    result_code write_heights(transition_heights const& heights, KTH_DB_txn* db_txn);

    /// Remove a property. Absence is the answer this stores, so deleting a key
    /// that is not there is success, not a failure to report.
    result_code clear_property(property_code prop, KTH_DB_txn* db_txn);
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
