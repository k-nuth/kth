// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_CHAIN_STATE_HPP
#define KTH_DOMAIN_CHAIN_CHAIN_STATE_HPP

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>

#include <kth/domain/chain/abla.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/machine/opcode.hpp>
#include <kth/domain/machine/rule_fork.hpp>
#include <kth/infrastructure/config/checkpoint.hpp>
#include <kth/infrastructure/math/hash.hpp>


namespace kth::domain::chain {

class block;
class header;

struct KD_API chain_state {
    using bitss = std::deque<uint32_t>;                 //TODO(fernando): why deque?
    using versions = std::deque<uint32_t>;              //TODO(fernando): why deque?
    using timestamps = std::deque<uint32_t>;            //TODO(fernando): why deque?
    using range = struct {size_t count; size_t high;};
    using ptr = std::shared_ptr<chain_state>;
    using checkpoints = infrastructure::config::checkpoint::list;

    /// Heights used to identify construction requirements.
    /// All values are lower-bounded by the genesis block height.
    /// Obtaining all values even in the case where the set of queries could be
    /// short-circuited allows us to cache results for the next block,
    /// minimizing overall querying.
    struct map {
        // This sentinel indicates that the value was not requested.
        static
        size_t const unrequested = max_size_t;

        /// [block - 1, floor(block - 2016, 0)] mainnet: 1, testnet: 2016|0
        range bits;

        /// (block - 0), used only for populating bits.ordered on increment.
        size_t bits_self;

        /// [block - 1, floor(block - 1000, 0)] mainnet: 1000, testnet: 100
        range version;

        /// (block - 0)
        size_t version_self;

        /// [block - 1, floor(block - 11, 0)]
        range timestamp;

        /// (block - 0)
        size_t timestamp_self;

        /// (block - (block % 2016 == 0 ? 2016 : block % 2016))
        size_t timestamp_retarget;

        /// mainnet: 227931, testnet: 21111 (or map::unrequested)
        size_t allow_collisions_height;

#if ! defined(KTH_CURRENCY_BCH)
        /// mainnet: 419328, testnet: 770112 (or map::unrequested)
        size_t bip9_bit0_height;

        /// mainnet: 481824, testnet: 834624 (or map::unrequested)
        size_t bip9_bit1_height;
#endif
    };

    /// Values used to populate chain state at the target height.
    struct data {
        /// Header values are based on this height.
        size_t height;

        /// Hash of the candidate block or null_hash for memory pool.
        hash_digest hash;

        /// Hash of the allow_collisions block or null_hash if unrequested.
        hash_digest allow_collisions_hash;

#if ! defined(KTH_CURRENCY_BCH)
        /// Hash of the bip9_bit0 block or null_hash if unrequested.
        hash_digest bip9_bit0_hash;

        /// Hash of the bip9_bit1 block or null_hash if unrequested.
        hash_digest bip9_bit1_hash;
#endif

        /// Values must be ordered by height with high (block - 1) last.
        struct {
            uint32_t self;
            bitss ordered;
        } bits;

        /// Values must be ordered by height with high (block - 1) last.
        struct {
            uint32_t self;
            versions ordered;
        } version;

        /// Values must be ordered by height with high (block - 1) last.
        struct {
            uint32_t self;
            uint32_t retarget;
            timestamps ordered;
        } timestamp;

#if defined(KTH_CURRENCY_BCH)
        abla::state abla_state;
#endif
    };

#if defined(KTH_CURRENCY_BCH)
    struct assert_anchor_block_info_t {
        size_t height;
        uint64_t ancestor_timestamp;
        uint32_t bits;
    };
#endif

    /// Checkpoints must be ordered by height with greatest at back.
    /// Forks and checkpoints must match those provided for map creation.
    chain_state(data&& values, uint32_t forks, checkpoints const& checkpoints
                , domain::config::network network
#if defined(KTH_CURRENCY_BCH)
                , assert_anchor_block_info_t const& assert_anchor_block_info
                , uint32_t asert_half_life
                , abla::config const& abla_config
                // , euclid_t euclid_activation_time
                // , pisano_t pisano_activation_time
                // , mersenne_t mersenne_activation_time
                // , fermat_t fermat_activation_time
                // , euler_t euler_activation_time
                // , gauss_t gauss_activation_time
                // , descartes_t descartes_activation_time
                // , lobachevski_t lobachevski_activation_time
                // , galois_t galois_activation_time
                , leibniz_t leibniz_activation_time
                , cantor_t cantor_activation_time
#endif  //KTH_CURRENCY_BCH
    );


    // Named constructors
    static
    std::shared_ptr<chain_state> from_pool_ptr(chain_state const& pool, block const& block);

    /// Checkpoints must be ordered by height with greatest at back.
    static
    map get_map(size_t height, checkpoints const& checkpoints, uint32_t forks, domain::config::network network);

    static
    uint32_t signal_version(uint32_t forks);

    /// Properties.
    [[nodiscard]]
    domain::config::network network() const;

    [[nodiscard]]
    size_t height() const;

    [[nodiscard]]
    abla::state const& abla_state() const;

    [[nodiscard]]
    uint32_t enabled_forks() const;

    [[nodiscard]]
    uint32_t minimum_version() const;

    [[nodiscard]]
    uint32_t median_time_past() const;

    [[nodiscard]]
    uint32_t work_required() const;

#if defined(KTH_CURRENCY_BCH)
    [[nodiscard]]
    assert_anchor_block_info_t assert_anchor_block_info() const;

    [[nodiscard]]
    uint32_t asert_half_life() const;

    [[nodiscard]]
    uint64_t dynamic_max_block_size() const;

    [[nodiscard]]
    uint64_t dynamic_max_block_sigops() const;

    [[nodiscard]]
    uint64_t dynamic_max_block_sigchecks() const;



    // [[nodiscard]]
    // euclid_t euclid_activation_time() const;

    // [[nodiscard]]
    // pisano_t pisano_activation_time() const;

    // [[nodiscard]]
    // mersenne_t mersenne_activation_time() const;

    // [[nodiscard]]
    // fermat_t fermat_activation_time() const;

    // [[nodiscard]]
    // euler_t euler_activation_time() const;

    // [[nodiscard]]
    // gauss_t gauss_activation_time() const;

    // [[nodiscard]]
    // descartes_t descartes_activation_time() const;

    // [[nodiscard]]
    // lobachevski_t lobachevski_activation_time() const;

    // [[nodiscard]]
    // galois_t galois_activation_time() const;

    [[nodiscard]]
    leibniz_t leibniz_activation_time() const;

    [[nodiscard]]
    cantor_t cantor_activation_time() const;

#endif  //KTH_CURRENCY_BCH

    /// Construction with zero height or any empty array causes invalid state.
    [[nodiscard]]
    bool is_valid() const;

    /// Determine if the fork is set for this block.
    [[nodiscard]]
    bool is_enabled(machine::rule_fork fork) const;

    /// Determine if this block hash fails a checkpoint at this height.
    [[nodiscard]]
    bool is_checkpoint_conflict(hash_digest const& hash) const;

    /// This block height is less than or equal to that of the top checkpoint.
    [[nodiscard]]
    bool is_under_checkpoint() const;

    static
    bool is_retarget_height(size_t height);  //Need to be public, for Litecoin

#if defined(KTH_CURRENCY_BCH)
    static
    uint256_t difficulty_adjustment_cash(uint256_t const& target);
#endif  //KTH_CURRENCY_BCH

    uint32_t get_next_work_required(uint32_t time_now);

#if defined(KTH_CURRENCY_BCH)
    static
    bool is_mtp_activated(uint32_t median_time_past, uint32_t activation_time);

    [[nodiscard]]
    bool is_pythagoras_enabled() const;

    [[nodiscard]]
    bool is_euclid_enabled() const;

    [[nodiscard]]
    bool is_pisano_enabled() const;

    [[nodiscard]]
    bool is_mersenne_enabled() const;

    [[nodiscard]]
    bool is_fermat_enabled() const;

    [[nodiscard]]
    bool is_euler_enabled() const;

    [[nodiscard]]
    bool is_gauss_enabled() const;

    [[nodiscard]]
    bool is_descartes_enabled() const;

    [[nodiscard]]
    bool is_lobachevski_enabled() const;

    [[nodiscard]]
    bool is_galois_enabled() const;

    [[nodiscard]]
    bool is_leibniz_enabled() const;

    [[nodiscard]]
    bool is_cantor_enabled() const;
#endif  //KTH_CURRENCY_BCH

    static
    uint32_t median_time_past(data const& values, size_t last_n = median_time_past_interval);

protected:
    struct activations {
        // The forks that are active at this height.
        uint32_t forks;

        // The minimum block version required at this height.
        uint32_t minimum_version;
    };

    static
    activations activation(data const& values, uint32_t forks
            , domain::config::network network
#if defined(KTH_CURRENCY_BCH)
            // , euclid_t euclid_activation_time
            // , pisano_t pisano_activation_time
            // , mersenne_t mersenne_activation_time
            // , fermat_t fermat_activation_time
            // , euler_t euler_activation_time
            // , gauss_t gauss_activation_time
            // , descartes_t descartes_activation_time
            // , lobachevski_t lobachevski_activation_time
            // , galois_t galois_activation_time
            , leibniz_t leibniz_activation_time
            , cantor_t cantor_activation_time
#endif  //KTH_CURRENCY_BCH
    );

    static
    uint32_t work_required(data const& values, config::network network, uint32_t forks
#if defined(KTH_CURRENCY_BCH)
                            // , euler_t euler_activation_time
                            // , gauss_t gauss_activation_time
                            // , descartes_t descartes_activation_time
                            // , lobachevski_t lobachevski_activation_time
                            // , galois_t galois_activation_time
                            , leibniz_t leibniz_activation_time
                            , cantor_t cantor_activation_time
                            , assert_anchor_block_info_t const& assert_anchor_block_info
                            , uint32_t asert_half_life
#endif
    );

private:

    static
    size_t bits_count(size_t height, uint32_t forks);

    static
    size_t version_count(size_t height, uint32_t forks, domain::config::network network);

    static
    size_t timestamp_count(size_t height, uint32_t forks);

    // TODO(kth): make function private again. Moved to public in the litecoin merge
    static
    size_t retarget_height(size_t height, uint32_t forks);

    static
    size_t collision_height(size_t height, config::network network);

#if ! defined(KTH_CURRENCY_BCH)
    static
    size_t bip9_bit0_height(size_t height, uint32_t forks);

    static
    size_t bip9_bit1_height(size_t height, uint32_t forks);
#endif

public:
    // static
    // bool is_rule_enabled(size_t height, uint32_t forks, size_t mainnet_height, size_t testnet_height);
    static
    bool is_rule_enabled(size_t height, config::network network, size_t mainnet_height, size_t testnet_height
#if defined(KTH_CURRENCY_BCH)
        , size_t testnet4_height
        , size_t scalenet_height
        , size_t chipnet_height
#endif
        );


    // ------------------------------------------------------------------------
#if defined(KTH_CURRENCY_BCH)
    // Block height at which CSV (BIP68, BIP112 and BIP113) becomes active
    static
    bool is_csv_enabled(size_t height, config::network network);

    static
    bool is_uahf_enabled(size_t height, config::network network);

    static
    bool is_daa_cw144_enabled(size_t height, config::network network);

    static
    bool is_pythagoras_enabled(size_t height, config::network network);

    static
    bool is_euclid_enabled(size_t height, config::network network);

    static
    bool is_pisano_enabled(size_t height, config::network network);

    static
    bool is_mersenne_enabled(size_t height, config::network network);

    static
    bool is_fermat_enabled(size_t height, config::network network);

    static
    bool is_euler_enabled(size_t height, config::network network);

    static
    bool is_gauss_enabled(size_t height, config::network network);

    static
    bool is_descartes_enabled(size_t height, config::network network);

    static
    bool is_lobachevski_enabled(size_t height, config::network network);

    static
    bool is_galois_enabled(size_t height, config::network network);

    // static
    // bool is_leibniz_enabled(size_t height, config::network network);

    // static
    // bool is_cantor_enabled(size_t height, config::network network);

#endif // KTH_CURRENCY_BCH
    // ------------------------------------------------------------------------

private:
    static
    data to_block(chain_state const& pool, block const& block);

    static
    uint32_t work_required_retarget(data const& values);

    static
    uint32_t retarget_timespan(chain_state::data const& values);

    // TODO(kth): make function private again. Moved to public in the litecoin merge
    // static
    // bool is_retarget_height(size_t height);

#if defined(KTH_CURRENCY_BCH)
    static
    uint32_t daa_aserti3_2d(data const& values, assert_anchor_block_info_t const& assert_anchor_block_info, uint32_t half_life);

    static
    uint32_t daa_cw144(data const& values);

    static
    uint32_t work_required_adjust_cash(data const& values);
#endif  //KTH_CURRENCY_BCH

    // easy blocks
    static
    uint32_t work_required_easy(data const& values);

    static
    uint32_t elapsed_time_limit(chain_state::data const& values);

    static
    bool is_retarget_or_non_limit(size_t height, uint32_t bits);

    static
    uint32_t easy_work_required(data const& values
#if defined(KTH_CURRENCY_BCH)
                            , bool daa_cw144_active
                            , bool daa_asert_active
                            , assert_anchor_block_info_t const& assert_anchor_block_info
                            , uint32_t asert_half_life
#endif
    );

    static
    uint32_t easy_time_limit(chain_state::data const& values);

    static
    size_t retarget_distance(size_t height);

    // This is retained as an optimization for other constructions.
    // A similar height clone can be partially computed, reducing query cost.
    data const data_;
    //TODO(fernando): make it immutable
    assert_anchor_block_info_t assert_anchor_block_info_;

    // Configured forks are saved for state transitions.
    uint32_t const forks_;

    // Checkpoints do not affect the data that is collected or promoted.
    infrastructure::config::checkpoint::list const& checkpoints_;
    domain::config::network network_;

    // These are computed on construct from sample and checkpoints.
    activations const active_;
    uint32_t const median_time_past_;
    uint32_t const work_required_;

//TODO(fernando): inherit BCH data and functions for a specific BCH class
#if defined(KTH_CURRENCY_BCH)
    uint32_t const asert_half_life_;
    abla::config abla_config_;

    // euclid_t const euclid_activation_time_;
    // pisano_t const pisano_activation_time_;
    // mersenne_t const mersenne_activation_time_;
    // fermat_t const fermat_activation_time_;
    // euler_t const euler_activation_time_;
    // gauss_t const gauss_activation_time_;
    // descartes_t const descartes_activation_time_;
    // lobachevski_t const lobachevski_activation_time_;
    // galois_t const galois_activation_time_;
    leibniz_t const leibniz_activation_time_;
    cantor_t const cantor_activation_time_;
#endif  //KTH_CURRENCY_BCH
};

} // namespace kth::domain::chain

#endif
