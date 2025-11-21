// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/chain_state.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include <boost/range/adaptor/reversed.hpp>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/chain_state.hpp>
#include <kth/domain/chain/compact.hpp>

#if defined(KTH_CURRENCY_BCH)
#include <kth/domain/chain/daa/aserti3_2d.hpp>
#endif

#include <kth/domain/chain/script.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/machine/opcode.hpp>
#include <kth/domain/machine/rule_fork.hpp>
#include <kth/domain/multi_crypto_support.hpp>

#include <kth/infrastructure/config/checkpoint.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/unicode/unicode.hpp>
#include <kth/infrastructure/utility/limits.hpp>
#include <kth/infrastructure/utility/timer.hpp>

namespace kth::domain::chain {

using namespace infrastructure::config;
using namespace kth::domain::machine;
using namespace boost::adaptors;

// Constructors.
//-----------------------------------------------------------------------------

// The allow_collisions hard fork is always activated (not configurable).
chain_state::chain_state(data&& values, uint32_t forks, checkpoints const& checkpoints
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
)
    : data_(std::move(values))
#if defined(KTH_CURRENCY_BCH)
    , assert_anchor_block_info_(assert_anchor_block_info)
#endif
    , forks_(forks | rule_fork::allow_collisions)
    , checkpoints_(checkpoints)
    , network_(network)
    , active_(activation(data_, forks_, network_
#if defined(KTH_CURRENCY_BCH)
        // , euclid_activation_time
        // , pisano_activation_time
        // , mersenne_activation_time
        // , fermat_activation_time
        // , euler_activation_time
        // , gauss_activation_time
        // , descartes_activation_time
        // , lobachevski_activation_time
        // , galois_activation_time
        , leibniz_activation_time
        , cantor_activation_time
#endif  //KTH_CURRENCY_BCH
        ))
    , median_time_past_(median_time_past(data_))
    , work_required_(work_required(data_, network_, forks_
#if defined(KTH_CURRENCY_BCH)
            // , euler_activation_time
            // , gauss_activation_time
            // , descartes_activation_time
            // , lobachevski_activation_time
            // , galois_activation_time
            , leibniz_activation_time
            , cantor_activation_time
            , assert_anchor_block_info_
            , asert_half_life
#endif
    ))
#if defined(KTH_CURRENCY_BCH)
    , asert_half_life_(asert_half_life)
    , abla_config_(abla_config)
    // , euclid_activation_time_(euclid_activation_time)
    // , pisano_activation_time_(pisano_activation_time)
    // , mersenne_activation_time_(mersenne_activation_time)
    // , fermat_activation_time_(fermat_activation_time)
    // , euler_activation_time_(euler_activation_time)
    // , gauss_activation_time_(gauss_activation_time)
    // , descartes_activation_time_(descartes_activation_time)
    // , lobachevski_activation_time_(lobachevski_activation_time)
    // , galois_activation_time_(galois_activation_time)
    , leibniz_activation_time_(leibniz_activation_time)
    , cantor_activation_time_(cantor_activation_time)
#endif  //KTH_CURRENCY_BCH
{}

domain::config::network chain_state::network() const {
    // Retargeting and testnet are only activated via configuration.
    // auto const testnet = script::is_enabled(forks, rule_fork::easy_blocks);
    // auto const retarget = script::is_enabled(forks, rule_fork::retarget);
    // auto const mainnet = retarget && !testnet;
    return network_;
}


// Named constructors.
//-----------------------------------------------------------------------------

// static
std::shared_ptr<chain_state> chain_state::from_pool_ptr(chain_state const& pool, block const& block) {
    return std::make_shared<chain_state>(to_block(pool, block), pool.forks_, pool.checkpoints_
        , pool.network_
#if defined(KTH_CURRENCY_BCH)
        , pool.assert_anchor_block_info_
        , pool.asert_half_life_
        , pool.abla_config_
        // , pool.fermat_activation_time_
        // , pool.euler_activation_time_
        // , pool.gauss_activation_time_
        // , pool.descartes_activation_time_
        // , pool.lobachevski_activation_time_
        // , pool.galois_activation_time_
        , pool.leibniz_activation_time_
        , pool.cantor_activation_time_
#endif  //KTH_CURRENCY_BCH
    );
}

// Inlines.
//-----------------------------------------------------------------------------

inline
size_t version_sample_size(domain::config::network network) {
    return network == domain::config::network::mainnet ? mainnet_sample : testnet_sample;
}

inline
bool is_active(size_t count, domain::config::network network) {
    return network_relation(network, std::greater_equal<>{}, count
                            , mainnet_active
                            , testnet_active
                            , size_t(0)
#if defined(KTH_CURRENCY_BCH)
                            , size_t(0)
                            , size_t(0)
                            , size_t(0)
#endif
                            );
}

inline
bool is_enforced(size_t count, domain::config::network network) {
    return count >= (network == domain::config::network::mainnet ? mainnet_enforce : testnet_enforce);
}

inline
bool is_bip16_exception(infrastructure::config::checkpoint const& check, domain::config::network network) {
    return network == domain::config::network::mainnet && check == mainnet_bip16_exception_checkpoint;
}

inline
bool is_bip30_exception(infrastructure::config::checkpoint const& check, domain::config::network network) {
    return network == domain::config::network::mainnet &&
           ((check == mainnet_bip30_exception_checkpoint1) ||
            (check == mainnet_bip30_exception_checkpoint2));
}

inline
bool allow_collisions(hash_digest const& hash, domain::config::network network) {
    return network_relation(network, std::equal_to<>{}, hash
                                , mainnet_bip34_active_checkpoint.hash()
                                , testnet_bip34_active_checkpoint.hash()
                                , regtest_bip34_active_checkpoint.hash()
#if defined(KTH_CURRENCY_BCH)
                                , testnet4_bip34_active_checkpoint.hash()
                                , scalenet_bip34_active_checkpoint.hash()
                                , chipnet_bip34_active_checkpoint.hash()
#endif
                                );
}

inline
bool allow_collisions(size_t height, domain::config::network network) {
    return network_relation(network, std::equal_to<>{}, height
                            , mainnet_bip34_active_checkpoint.height()
                            , testnet_bip34_active_checkpoint.height()
                            , regtest_bip34_active_checkpoint.height()
#if defined(KTH_CURRENCY_BCH)
                            , testnet4_bip34_active_checkpoint.height()
                            , scalenet_bip34_active_checkpoint.height()
                            , chipnet_bip34_active_checkpoint.height()
#endif
                            );
}

#if ! defined(KTH_CURRENCY_BCH)
inline
bool bip9_bit0_active(hash_digest const& hash, domain::config::network network) {
    return network_relation(network, std::equal_to<>{}, hash,
                            mainnet_bip9_bit0_active_checkpoint.hash(),
                            testnet_bip9_bit0_active_checkpoint.hash(),
                            regtest_bip9_bit0_active_checkpoint.hash());
}

inline
bool bip9_bit0_active(size_t height, domain::config::network network) {
    return network_relation(network, std::equal_to<>{}, height,
                            mainnet_bip9_bit0_active_checkpoint.height(),
                            testnet_bip9_bit0_active_checkpoint.height(),
                            regtest_bip9_bit0_active_checkpoint.height());
}

inline
bool bip9_bit1_active(hash_digest const& hash, domain::config::network network) {
    return network_relation(network, std::equal_to<>{}, hash,
                            mainnet_bip9_bit1_active_checkpoint.hash(),
                            testnet_bip9_bit1_active_checkpoint.hash(),
                            regtest_bip9_bit1_active_checkpoint.hash());
}

inline
bool bip9_bit1_active(size_t height, domain::config::network network) {
    return network_relation(network, std::equal_to<>{}, height,
                            mainnet_bip9_bit1_active_checkpoint.height(),
                            testnet_bip9_bit1_active_checkpoint.height(),
                            regtest_bip9_bit1_active_checkpoint.height());
}
#endif

inline
bool bip34(size_t height, bool frozen, domain::config::network network) {
    return frozen &&
           network_relation(network, std::greater_equal<>{}, height
                            , mainnet_bip34_freeze
                            , testnet_bip34_freeze
                            , regtest_bip34_freeze
#if defined(KTH_CURRENCY_BCH)
                            , testnet4_bip34_freeze
                            , scalenet_bip34_freeze
                            , chipnet_bip34_freeze
#endif
                            );
}

inline
bool bip66(size_t height, bool frozen, domain::config::network network) {
    return frozen &&
           network_relation(network, std::greater_equal<>{}, height
                            , mainnet_bip66_freeze
                            , testnet_bip66_freeze
                            , regtest_bip66_freeze
#if defined(KTH_CURRENCY_BCH)
                            , testnet4_bip66_freeze
                            , scalenet_bip66_freeze
                            , chipnet_bip66_freeze
#endif
                            );
}

inline
bool bip65(size_t height, bool frozen, domain::config::network network) {
    return frozen &&
           network_relation(network, std::greater_equal<>{}, height
                            , mainnet_bip65_freeze
                            , testnet_bip65_freeze
                            , regtest_bip65_freeze
#if defined(KTH_CURRENCY_BCH)
                            , testnet4_bip65_freeze
                            , scalenet_bip65_freeze
                            , chipnet_bip65_freeze
#endif
                            );
}

inline
uint32_t timestamp_high(chain_state::data const& values) {
    return values.timestamp.ordered.back();
}

inline
uint32_t bits_high(chain_state::data const& values) {
    return values.bits.ordered.back();
}

#if defined(KTH_CURRENCY_BCH)
uint256_t chain_state::difficulty_adjustment_cash(uint256_t const& target) {
    return target + (target >> 2);
}

bool chain_state::is_mtp_activated(uint32_t median_time_past, uint32_t activation_time) {
    return (median_time_past >= activation_time);
}

// 2018-May
bool chain_state::is_pythagoras_enabled() const {
    return is_pythagoras_enabled(height(), network());
}

// 2018-Nov
bool chain_state::is_euclid_enabled() const {
    return is_euclid_enabled(height(), network());
}

// 2019-May
bool chain_state::is_pisano_enabled() const {
    return is_pisano_enabled(height(), network());
}

// 2019-Nov
bool chain_state::is_mersenne_enabled() const {
    return is_mersenne_enabled(height(), network());
}

// 2020-May
bool chain_state::is_fermat_enabled() const {
    return is_fermat_enabled(height(), network());
}

// 2020-Nov
bool chain_state::is_euler_enabled() const {
    return is_euler_enabled(height(), network());
}

// 2021-May: There were no hard forks in 2021

// 2022-May
bool chain_state::is_gauss_enabled() const {
    return is_gauss_enabled(height(), network());
}

// 2023-May
bool chain_state::is_descartes_enabled() const {
    return is_descartes_enabled(height(), network());
}

// 2024-May
bool chain_state::is_lobachevski_enabled() const {
    return is_lobachevski_enabled(height(), network());
}

// 2025-May
bool chain_state::is_galois_enabled() const {
    return is_galois_enabled(height(), network());
}

// 2026-May
bool chain_state::is_leibniz_enabled() const {
   //TODO(fernando): this was activated, change to the other method
    return is_mtp_activated(median_time_past(), std::to_underlying(leibniz_activation_time()));
    // return is_leibniz_enabled(height(), network());
}

// 2027-May
bool chain_state::is_cantor_enabled() const {
   //TODO(fernando): this was activated, change to the other method
    return is_mtp_activated(median_time_past(), std::to_underlying(cantor_activation_time()));
    // return is_cantor_enabled(height(), network());
}

// 20xx-May
// //static
// bool chain_state::is_unnamed_enabled() const {
//    //TODO(fernando): this was activated, change to the other method
//     return is_mtp_activated(median_time_past(), unnamed_activation_time());
// }

#endif  //KTH_CURRENCY_BCH

// Statics.
// activation
//-----------------------------------------------------------------------------

// static
chain_state::activations chain_state::activation(data const& values, uint32_t forks
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
) {
    auto const height = values.height;
    auto const version = values.version.self;
    auto const& history = values.version.ordered;
    auto const frozen = script::is_enabled(forks, rule_fork::bip90_rule);

    //*************************************************************************
    // CONSENSUS: Though unspecified in bip34, the satoshi implementation
    // performed this comparison using the signed integer version value.
    //*************************************************************************
    auto const ge = [](uint32_t value, size_t version) {
        return static_cast<int32_t>(value) >= version;
    };

    // Declare bip34-based version predicates.
    auto const ge_2 = [=](uint32_t value) { return ge(value, bip34_version); };
    auto const ge_3 = [=](uint32_t value) { return ge(value, bip66_version); };
    auto const ge_4 = [=](uint32_t value) { return ge(value, bip65_version); };

    // Compute bip34-based activation version summaries.
    auto const count_2 = std::count_if(history.begin(), history.end(), ge_2);
    auto const count_3 = std::count_if(history.begin(), history.end(), ge_3);
    auto const count_4 = std::count_if(history.begin(), history.end(), ge_4);

    // Frozen activations (require version and enforce above freeze height).
    auto const bip34_ice = bip34(height, frozen, network);
    auto const bip66_ice = bip66(height, frozen, network);
    auto const bip65_ice = bip65(height, frozen, network);

    // Initialize activation results with genesis values.
    activations result{rule_fork::no_rules, first_version};

    // retarget is only activated via configuration (hard fork).
    result.forks |= (rule_fork::retarget & forks);

    // testnet is activated based on configuration alone (hard fork).
    result.forks |= (rule_fork::easy_blocks & forks);

    // bip90 is activated based on configuration alone (hard fork).
    result.forks |= (rule_fork::bip90_rule & forks);

    // bip16 is activated with a one-time test on mainnet/testnet (~55% rule).
    // There was one invalid p2sh tx mined after that time (code shipped late).
    if (values.timestamp.self >= bip16_activation_time &&
        ! is_bip16_exception({values.hash, height}, network)) {
        result.forks |= (rule_fork::bip16_rule & forks);
    }

    // bip30 is active for all but two mainnet blocks that violate the rule.
    // These two blocks each have a coinbase transaction that exctly duplicates
    // another that is not spent by the arrival of the corresponding duplicate.
    if ( ! is_bip30_exception({values.hash, height}, network)) {
        result.forks |= (rule_fork::bip30_rule & forks);
    }

#if defined(KTH_CURRENCY_LTC)
    if (bip34_ice) {
        result.forks |= (rule_fork::bip34_rule & forks);
    }
#else
    // bip34 is activated based on 75% of preceding 1000 mainnet blocks.
    if (bip34_ice || (is_active(count_2, network) && version >= bip34_version)) {
        result.forks |= (rule_fork::bip34_rule & forks);
    }
#endif

    // bip66 is activated based on 75% of preceding 1000 mainnet blocks.
    if (bip66_ice || (is_active(count_3, network) && version >= bip66_version)) {
        result.forks |= (rule_fork::bip66_rule & forks);
    }

    // bip65 is activated based on 75% of preceding 1000 mainnet blocks.
    if (bip65_ice || (is_active(count_4, network) && version >= bip65_version)) {
        result.forks |= (rule_fork::bip65_rule & forks);
    }

    // allow_collisions is active above the bip34 checkpoint only.
    if (allow_collisions(values.allow_collisions_hash, network)) {
        result.forks |= (rule_fork::allow_collisions & forks);
    }

#if ! defined(KTH_CURRENCY_BCH)
    // bip9_bit0 forks are enforced above the bip9_bit0 checkpoint.
    if (bip9_bit0_active(values.bip9_bit0_hash, network)) {
        result.forks |= (rule_fork::bip9_bit0_group & forks);
    }

    // Activate the segwit rules only if not bch
    // bip9_bit1 forks are enforced above the bip9_bit1 checkpoint.
    if (bip9_bit1_active(values.bip9_bit1_hash, network)) {
        result.forks |= (rule_fork::bip9_bit1_group & forks);
    }
#endif

    // version 4/3/2 enforced based on 95% of preceding 1000 mainnet blocks.
    if (bip65_ice || is_enforced(count_4, network)) {
        result.minimum_version = bip65_version;
    } else if (bip66_ice || is_enforced(count_3, network)) {
        result.minimum_version = bip66_version;
    } else if (bip34_ice || is_enforced(count_2, network)) {
        result.minimum_version = bip34_version;
    } else {
        result.minimum_version = first_version;
    }

#if defined(KTH_CURRENCY_BCH)
    if (is_csv_enabled(values.height, network)) {
        // flags |= SCRIPT_VERIFY_CHECKSEQUENCEVERIFY;
        result.forks |= (rule_fork::bip112_rule & forks);
    }

    if (is_uahf_enabled(values.height, network)) {
        // result.forks |= (rule_fork::cash_verify_flags_script_enable_sighash_forkid & forks);
        // result.forks |= (rule_fork::SCRIPT_VERIFY_STRICTENC & forks);
        // result.forks |= (rule_fork::SCRIPT_ENABLE_SIGHASH_FORKID & forks);
        result.forks |= (rule_fork::bch_uahf & forks);
    }

    if (is_daa_cw144_enabled(values.height, network)) {
        // result.forks |= (rule_fork::cash_low_s_rule & forks);
        // result.forks |= (rule_fork::SCRIPT_VERIFY_LOW_S & forks);
        // result.forks |= (rule_fork::SCRIPT_VERIFY_NULLFAIL & forks);
        result.forks |= (rule_fork::bch_daa_cw144 & forks);
    }

    if (is_pythagoras_enabled(values.height, network)) {
        result.forks |= (rule_fork::bch_pythagoras & forks);
    }

    if (is_euclid_enabled(values.height, network)) {
        // result.forks |= (rule_fork::cash_checkdatasig & forks);
        // result.forks |= (rule_fork::SCRIPT_VERIFY_CHECKDATASIG_SIGOPS & forks);
        // result.forks |= (rule_fork::SCRIPT_VERIFY_SIGPUSHONLY & forks);
        // result.forks |= (rule_fork::SCRIPT_VERIFY_CLEANSTACK & forks);
        result.forks |= (rule_fork::bch_euclid & forks);
    }

    if (is_pisano_enabled(values.height, network)) {
        // result.forks |= (rule_fork::cash_checkdatasig & forks);
        // result.forks |= (rule_fork::SCRIPT_VERIFY_CHECKDATASIG_SIGOPS & forks);
        // result.forks |= (rule_fork::SCRIPT_VERIFY_SIGPUSHONLY & forks);
        // result.forks |= (rule_fork::SCRIPT_VERIFY_CLEANSTACK & forks);
        result.forks |= (rule_fork::bch_pisano & forks);
    }

    if (is_mersenne_enabled(values.height, network)) {
        // result.forks |= (rule_fork::SCRIPT_ENABLE_SCHNORR_MULTISIG & forks);
        // result.forks |= (rule_fork::SCRIPT_VERIFY_MINIMALDATA & forks);
        result.forks |= (rule_fork::bch_mersenne & forks);
    }

    if (is_fermat_enabled(values.height, network)) {
    //     flags |= SCRIPT_ENABLE_OP_REVERSEBYTES;
    //     flags |= SCRIPT_REPORT_SIGCHECKS;
    //     flags |= SCRIPT_ZERO_SIGOPS;
        result.forks |= (rule_fork::bch_fermat & forks);
    }

    if (is_euler_enabled(values.height, network)) {
        result.forks |= (rule_fork::bch_euler & forks);
    }

    if (is_gauss_enabled(values.height, network)) {
        result.forks |= (rule_fork::bch_gauss & forks);
    }

    if (is_descartes_enabled(values.height, network)) {
        result.forks |= (rule_fork::bch_descartes & forks);
    }

    if (is_lobachevski_enabled(values.height, network)) {
        result.forks |= (rule_fork::bch_lobachevski & forks);
    }

    if (is_galois_enabled(values.height, network)) {
        result.forks |= (rule_fork::bch_galois & forks);
    }

    auto const mtp = median_time_past(values);
    if (is_mtp_activated(mtp, std::to_underlying(leibniz_activation_time))) {
        //Note(Fernando): Move this to the next fork rules
        result.forks |= (rule_fork::bch_leibniz & forks);
    }

    if (is_mtp_activated(mtp, std::to_underlying(cantor_activation_time))) {
        //Note(Fernando): Move this to the next fork rules
        result.forks |= (rule_fork::bch_cantor & forks);
    }

    // Old rules with Replay Protection
    // auto const mtp = median_time_past(values);
    // if (is_mtp_activated(mtp, std::to_underlying(gauss_activation_time))) {
    //     //Note(Fernando): Move this to the next fork rules
    // //     flags |= SCRIPT_ENABLE_REPLAY_PROTECTION;
    //     result.forks |= (rule_fork::bch_gauss & forks);
    //     // result.forks |= (rule_fork::bch_replay_protection & forks);
    // }
#endif  //KTH_CURRENCY_BCH

    return result;
}

size_t chain_state::bits_count(size_t height, uint32_t forks) {
    auto const testnet = script::is_enabled(forks, rule_fork::easy_blocks);
    auto const retarget = script::is_enabled(forks, rule_fork::retarget);
    auto const easy_work = testnet && retarget && !is_retarget_height(height);

#if defined(KTH_CURRENCY_BCH)
    return easy_work ? std::min(height, retargeting_interval) : std::min(height, chain_state_timestamp_count);
#else
    return easy_work ? std::min(height, retargeting_interval) : 1;
#endif  //KTH_CURRENCY_BCH
}

// static
size_t chain_state::version_count(size_t height, uint32_t forks, domain::config::network network) {
    if (  script::is_enabled(forks, rule_fork::bip90_rule) ||
        ! script::is_enabled(forks, rule_fork::bip34_activations)) {
        return 0;
    }

    return std::min(height, version_sample_size(network));
}

size_t chain_state::timestamp_count(size_t height, uint32_t /*unused*/) {
#if defined(KTH_CURRENCY_BCH)
    return std::min(height, chain_state_timestamp_count);  //TODO(kth): check what happens with Bitcoin Legacy...?
#else
    return std::min(height, median_time_past_interval);
#endif  //KTH_CURRENCY_BCH
}

size_t chain_state::retarget_height(size_t height, uint32_t forks) {
    if ( ! script::is_enabled(forks, rule_fork::retarget)) {
        return map::unrequested;
    }

    // Height must be a positive multiple of interval, so underflow safe.
    // If not retarget height get most recent so that it may be promoted.
    return height - (is_retarget_height(height) ? retargeting_interval : retarget_distance(height));
}

size_t chain_state::collision_height(size_t height, config::network network) {
    auto const bip34_height = network_map(network
                                        , mainnet_bip34_active_checkpoint.height()
                                        , testnet_bip34_active_checkpoint.height()
                                        , regtest_bip34_active_checkpoint.height()
#if defined(KTH_CURRENCY_BCH)
                                        , testnet4_bip34_active_checkpoint.height()
                                        , scalenet_bip34_active_checkpoint.height()
                                        , chipnet_bip34_active_checkpoint.height()
#endif
                                        );

    // Require collision hash at heights above historical bip34 activation.
    return height > bip34_height ? bip34_height : map::unrequested;
}

#if ! defined(KTH_CURRENCY_BCH)
size_t chain_state::bip9_bit0_height(size_t height, uint32_t forks) {
    auto const testnet = script::is_enabled(forks, rule_fork::easy_blocks);
    auto const regtest = !script::is_enabled(forks, rule_fork::retarget);

    auto const activation_height =
        testnet ? testnet_bip9_bit0_active_checkpoint.height() :
        regtest ? regtest_bip9_bit0_active_checkpoint.height() :
                  mainnet_bip9_bit0_active_checkpoint.height();

    // Require bip9_bit0 hash at heights above historical bip9_bit0 activation.
    return height > activation_height ? activation_height : map::unrequested;
}

size_t chain_state::bip9_bit1_height(size_t height, uint32_t forks) {
    auto const testnet = script::is_enabled(forks, rule_fork::easy_blocks);
    auto const activation_height = testnet ? testnet_bip9_bit1_active_checkpoint.height() :
                                             mainnet_bip9_bit1_active_checkpoint.height();

    // Require bip9_bit1 hash at heights above historical bip9_bit1 activation.
    return height > activation_height ? activation_height : map::unrequested;
}
#endif

// median_time_past
//-----------------------------------------------------------------------------

bool chain_state::is_rule_enabled(size_t height, config::network network, size_t mainnet_height, size_t testnet_height
#if defined(KTH_CURRENCY_BCH)
                                    , size_t testnet4_height
                                    , size_t scalenet_height
                                    , size_t chipnet_height
#endif
) {

    if (network == config::network::regtest) return true;

    return network_relation(network, std::greater_equal<>{}, height
                            , mainnet_height
                            , testnet_height
                            , size_t(0)
#if defined(KTH_CURRENCY_BCH)
                            , testnet4_height
                            , scalenet_height
                            , chipnet_height
#endif
                            );
}

#if defined(KTH_CURRENCY_BCH)
// Block height at which CSV (BIP68, BIP112 and BIP113) becomes active
bool chain_state::is_csv_enabled(size_t height, config::network network) {
    auto res = is_rule_enabled(height, network
        , mainnet_csv_activation_height
        , testnet_csv_activation_height
        , testnet4_csv_activation_height
        , scalenet_csv_activation_height
        , chipnet_csv_activation_height
        );

    return res;
}

//2017-August-01 hard fork
bool chain_state::is_uahf_enabled(size_t height, config::network network) {
    auto res = is_rule_enabled(height, network
        , mainnet_uahf_activation_height
        , testnet_uahf_activation_height
        , testnet4_uahf_activation_height
        , scalenet_uahf_activation_height
        , chipnet_uahf_activation_height
        );

    return res;
}

//2017-November-13 hard fork
bool chain_state::is_daa_cw144_enabled(size_t height, config::network network) {
    return is_rule_enabled(height, network
        , mainnet_daa_cw144_activation_height
        , testnet_daa_cw144_activation_height
        , testnet4_daa_cw144_activation_height
        , scalenet_daa_cw144_activation_height
        , chipnet_daa_cw144_activation_height
        );
}

//2018-May hard fork
bool chain_state::is_pythagoras_enabled(size_t height, config::network network) {
    return is_rule_enabled(height, network
        , mainnet_pythagoras_activation_height
        , testnet_pythagoras_activation_height
        , testnet4_pythagoras_activation_height
        , scalenet_pythagoras_activation_height
        , chipnet_pythagoras_activation_height
        );
}

//2018-Nov hard fork
bool chain_state::is_euclid_enabled(size_t height, config::network network) {
    return is_rule_enabled(height, network
        , mainnet_euclid_activation_height
        , testnet_euclid_activation_height
        , testnet4_euclid_activation_height
        , scalenet_euclid_activation_height
        , chipnet_euclid_activation_height
        );
}

//2019-May hard fork
bool chain_state::is_pisano_enabled(size_t height, config::network network) {
    return is_rule_enabled(height, network
        , mainnet_pisano_activation_height
        , testnet_pisano_activation_height
        , testnet4_pisano_activation_height
        , scalenet_pisano_activation_height
        , chipnet_pisano_activation_height
        );
}

//2019-Nov hard fork
bool chain_state::is_mersenne_enabled(size_t height, config::network network) {
    return is_rule_enabled(height, network
        , mainnet_mersenne_activation_height
        , testnet_mersenne_activation_height
        , testnet4_mersenne_activation_height
        , scalenet_mersenne_activation_height
        , chipnet_mersenne_activation_height
        );
}

//2020-May hard fork
bool chain_state::is_fermat_enabled(size_t height, config::network network) {
    return is_rule_enabled(height, network
        , mainnet_fermat_activation_height
        , testnet_fermat_activation_height
        , testnet4_fermat_activation_height
        , scalenet_fermat_activation_height
        , chipnet_fermat_activation_height
        );
}

//2020-Nov hard fork
bool chain_state::is_euler_enabled(size_t height, config::network network) {
    return is_rule_enabled(height, network
        , mainnet_euler_activation_height
        , testnet_euler_activation_height
        , testnet4_euler_activation_height
        , scalenet_euler_activation_height
        , chipnet_euler_activation_height
     );
}

//2022-May hard fork
bool chain_state::is_gauss_enabled(size_t height, config::network network) {
    return is_rule_enabled(height, network
        , mainnet_gauss_activation_height
        , testnet_gauss_activation_height
        , testnet4_gauss_activation_height
        , scalenet_gauss_activation_height
        , chipnet_gauss_activation_height
     );
}

//2023-May hard fork
bool chain_state::is_descartes_enabled(size_t height, config::network network) {
    return is_rule_enabled(height, network
        , mainnet_descartes_activation_height
        , testnet_descartes_activation_height
        , testnet4_descartes_activation_height
        , scalenet_descartes_activation_height
        , chipnet_descartes_activation_height
     );
}

// 2024-May hard fork
bool chain_state::is_lobachevski_enabled(size_t height, config::network network) {
    return is_rule_enabled(height, network
        , mainnet_lobachevski_activation_height
        , testnet_lobachevski_activation_height
        , testnet4_lobachevski_activation_height
        , scalenet_lobachevski_activation_height
        , chipnet_lobachevski_activation_height
     );
}

// 2025-May hard fork
bool chain_state::is_galois_enabled(size_t height, config::network network) {
    return is_rule_enabled(height, network
        , mainnet_galois_activation_height
        , testnet_galois_activation_height
        , testnet4_galois_activation_height
        , scalenet_galois_activation_height
        , chipnet_galois_activation_height
     );
}

//2026-May hard fork
// Complete after the hard fork
// bool chain_state::is_leibniz_enabled(size_t height, config::network network) {
//     return is_rule_enabled(height, network
//         , mainnet_leibniz_activation_height
//         , testnet_leibniz_activation_height
//         , testnet4_leibniz_activation_height
//         , scalenet_leibniz_activation_height
//         , chipnet_leibniz_activation_height
//      );
// }

//2027-May hard fork
// Complete after the hard fork
// bool chain_state::is_cantor_enabled(size_t height, config::network network) {
//     return is_rule_enabled(height, network
//         , mainnet_cantor_activation_height
//         , testnet_cantor_activation_height
//         , testnet4_cantor_activation_height
//         , scalenet_cantor_activation_height
//         , chipnet_cantor_activation_height
//      );
// }

//20xx-May hard fork
// Complete after the hard fork
// bool chain_state::is_unnamed_enabled(size_t height, config::network network) {
//     return is_rule_enabled(height, network
//         , mainnet_unnamed_activation_height
//         , testnet_unnamed_activation_height
//         , testnet4_unnamed_activation_height
//         , scalenet_unnamed_activation_height
//         , chipnet_unnamed_activation_height
//      );
// }

#endif // KTH_CURRENCY_BCH

auto timestamps_position(chain_state::timestamps const& times, size_t last_n = median_time_past_interval) {
    if (times.size() > last_n) {
        return times.begin() + (times.size() - last_n);
    }
    return times.begin();
}

std::vector<typename chain_state::timestamps::value_type> timestamps_subset(chain_state::timestamps const& times, size_t last_n = median_time_past_interval) {
    auto at = timestamps_position(times, last_n);
    auto n = (std::min)(static_cast<size_t>(std::distance(at, times.end())), median_time_past_interval);
    std::vector<typename chain_state::timestamps::value_type> subset(n);
    std::copy_n(at, n, subset.begin());
    return subset;
}

uint32_t median(std::vector<typename chain_state::timestamps::value_type>& subset) {
    // Sort the times by value to obtain the median.
    // Note(fernando): no need to stable sorting, because we are dealing with just integers.
    std::sort(subset.begin(), subset.end());

    // Consensus defines median time using modulo 2 element selection.
    // This differs from arithmetic median which averages two middle values.
    return subset.empty() ? 0 : subset[subset.size() / 2];
}

uint32_t chain_state::median_time_past(data const& values, size_t last_n /* = median_time_past_interval*/) {
    auto subset = timestamps_subset(values.timestamp.ordered, last_n);
    return median(subset);
}

// ------------------------------------------------------------------------------------------------------------
// Note(fernando): This is a Non-Stable Non-Standard Median of Three Algorithm
// See Footnote 2 of https://github.com/Bitcoin-UAHF/spec/blob/master/nov-13-hardfork-spec.md
// Modified version of select_1_3 taken from https://github.com/tao-cpp/algorithm
// ------------------------------------------------------------------------------------------------------------

template <typename T, typename U, typename R>
// requires(SameType<T, U> && Domain<R, T>)         //C++20 Concepts
inline constexpr
auto select_0_2(T&& a, U&& b, R r) {
    return r(b, a) ? std::forward<U>(b) : std::forward<T>(a);
}

template <typename T, typename U, typename V, typename R>
// requires(SameType<T, U> && SameType<U, V> && Domain<R, T>)
inline constexpr
auto select_1_3_unstable_ac(T&& a, U&& b, V&& c, R r) {
    // precondition: ! r(c, a)
    return r(b, a)
        ? std::forward<T>(a)                                        // b, a, c
        : select_0_2(std::forward<U>(b), std::forward<V>(c), r);    // a is not the median
}

template <typename T, typename U, typename V, typename R>
// requires(SameType<T, U> && SameType<U, V> && Domain<R, T>)
inline constexpr
auto select_1_3_unstable(T&& a, U&& b, V&& c, R r) {
    return r(c, a)
        ? select_1_3_unstable_ac(std::forward<V>(c), std::forward<U>(b), std::forward<T>(a), r)
        : select_1_3_unstable_ac(std::forward<T>(a), std::forward<U>(b), std::forward<V>(c), r);
}

// ------------------------------------------------------------------------------------------------------------

#if defined(KTH_CURRENCY_BCH)

// DAA/aserti3-2d: 2020-Nov-15 Hard fork
uint32_t chain_state::daa_aserti3_2d(data const& values, assert_anchor_block_info_t const& assert_anchor_block_info, uint32_t half_life) {
    //TODO(fernando): pre and postconditions!

    static uint256_t const pow_limit(compact{retarget_proof_of_work_limit});

    auto const time_diff = timestamp_high(values) - assert_anchor_block_info.ancestor_timestamp;
    auto const height_diff = values.height - assert_anchor_block_info.height;

    uint256_t const anchor_target(compact{assert_anchor_block_info.bits});

    auto next_target = daa::aserti3_2d(anchor_target, target_spacing_seconds, time_diff, height_diff, pow_limit, half_life);
    return compact(next_target).normal();
}

// DAA/cw-144: 2017-Nov-13 Hard fork
uint32_t chain_state::daa_cw144(data const& values) {
    // precondition: values.timestamp.size() >= 147

    using std::make_pair;
    using pair_t = std::pair<size_t, uint32_t>;

    auto const bits_size = values.bits.ordered.size();

    auto pair_cmp = [](pair_t const& a, pair_t const& b) { return a.second < b.second; };

    auto first_block = select_1_3_unstable(
                        make_pair(bits_size - 3, values.timestamp.ordered[144]),
                        make_pair(bits_size - 2, values.timestamp.ordered[145]),
                        make_pair(bits_size - 1, values.timestamp.ordered[146]),
                        pair_cmp);

    auto last_block = select_1_3_unstable(
                        make_pair(bits_size - 147, values.timestamp.ordered[0]),
                        make_pair(bits_size - 146, values.timestamp.ordered[1]),
                        make_pair(bits_size - 145, values.timestamp.ordered[2]),
                        pair_cmp);

    uint256_t work = 0;
    for (size_t i = last_block.first + 1; i <= first_block.first; ++i) {
        work += header::proof(values.bits.ordered[i]);
    }

    work *= target_spacing_seconds;  //10 * 60

    int64_t actual_timespan = first_block.second - last_block.second;
    if (actual_timespan > 288 * target_spacing_seconds) {
        actual_timespan = 288 * target_spacing_seconds;
    } else if (actual_timespan < 72 * target_spacing_seconds) {
        actual_timespan = 72 * target_spacing_seconds;
    }

    work /= actual_timespan;
    auto next_target = (-1 * work) / work;  //Compute target result
    uint256_t pow_limit(compact{retarget_proof_of_work_limit});

    if (next_target.compare(pow_limit) == 1) {
        return retarget_proof_of_work_limit;
    }

    return compact(next_target).normal();
}
#endif  //KTH_CURRENCY_BCH

// work_required
//-----------------------------------------------------------------------------

//static
uint32_t chain_state::work_required(data const& values, config::network network, uint32_t forks
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
) {
    // Invalid parameter via public interface, test is_valid for results.
    if (values.height == 0) {
        return {};
    }

    // regtest bypasses all retargeting.
    if ( ! script::is_enabled(forks, rule_fork::retarget)) {
        return bits_high(values);
    }

#if defined(KTH_CURRENCY_BCH)
    //TODO(fernando): could it be improved?
    bool const daa_cw144_active = is_daa_cw144_enabled(values.height, network);
    bool const daa_asert_active = is_euler_enabled(values.height, network);

    // MTP activation way: The comment is left in case in the future it is necessary to do it again.
    // auto const last_time_span = median_time_past(values);
    // bool const daa_asert_active = is_mtp_activated(last_time_span, std::to_underlying(euler_activation_time));
#else
    bool const daa_cw144_active = false;
#endif  //KTH_CURRENCY_BCH

    if (is_retarget_height(values.height) && ! daa_cw144_active) {
        return work_required_retarget(values);
    }

    if (script::is_enabled(forks, rule_fork::easy_blocks)) {
#if defined(KTH_CURRENCY_BCH)
        return easy_work_required(values, daa_cw144_active, daa_asert_active, assert_anchor_block_info, asert_half_life);
#else
        return easy_work_required(values);
#endif
    }

#if defined(KTH_CURRENCY_BCH)
    if (daa_asert_active) {
        return daa_aserti3_2d(values, assert_anchor_block_info, asert_half_life);
    }

    if (daa_cw144_active) {
        return daa_cw144(values);
    }

    //EDA
    if (is_uahf_enabled(values.height, network)) {
        auto const last_time_span = median_time_past(values);
        auto const six_time_span = median_time_past(values, bch_daa_eda_blocks);
        // precondition: last_time_span >= six_time_span
        if ((last_time_span - six_time_span) > (12 * 3600)) {
            return work_required_adjust_cash(values);
        }
    }
#endif  //KTH_CURRENCY_BCH

    return bits_high(values);
}

#if defined(KTH_CURRENCY_BCH)
uint32_t chain_state::work_required_adjust_cash(data const& values) {
    compact const bits(bits_high(values));
    uint256_t target(bits);
    target = difficulty_adjustment_cash(target);  //target += (target >> 2);
    static uint256_t const pow_limit(compact{retarget_proof_of_work_limit});
    return target > pow_limit ? retarget_proof_of_work_limit : compact(target).normal();
}
#endif  //KTH_CURRENCY_BCH

// [CalculateNextWorkRequired]
uint32_t chain_state::work_required_retarget(data const& values) {
    compact const bits(bits_high(values));

#if defined(KTH_CURRENCY_LTC)
    uint256_t target(bits);
    static uint256_t const pow_limit("0x00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    // hash_number retarget_new;
    // retarget_new.set_compact(bits_high(values));

    bool shift = target > ((uint256_t(1) << 236) - 1);

    if (shift) {
        target >>= 1;
    }

    int64_t const high = timestamp_high(values);
    int64_t const retarget = values.timestamp.retarget;
    int64_t const actual_timespan = range_constrain(high - retarget, (int64_t)min_timespan, (int64_t)max_timespan);
    // std::println("high:            {}", high);

    target *= actual_timespan;
    target /= target_timespan_seconds;

    if (shift) {
        target <<= 1;
    }

    // return target > pow_limit ? pow_limit.compact() : target.compact();

    return target > pow_limit ? retarget_proof_of_work_limit : compact(target).normal();

#else   //KTH_CURRENCY_LTC
    static uint256_t const pow_limit(compact{retarget_proof_of_work_limit});
    KTH_ASSERT_MSG( ! bits.is_overflowed(), "previous block has bad bits");

    uint256_t target(bits);
    target *= retarget_timespan(values);
    target /= target_timespan_seconds;

    // The proof_of_work_limit constant is pre-normalized.
    return target > pow_limit ? retarget_proof_of_work_limit : compact(target).normal();
#endif  //KTH_CURRENCY_LTC
}

// Get the bounded total time spanning the highest 2016 blocks.
uint32_t chain_state::retarget_timespan(data const& values) {
    auto const high = timestamp_high(values);
    auto const retarget = values.timestamp.retarget;

    //*************************************************************************
    // CONSENSUS: subtract unsigned 32 bit numbers in signed 64 bit space in
    // order to prevent underflow before applying the range constraint.
    //*************************************************************************
    auto const timespan = cast_subtract<int64_t>(high, retarget);
    return range_constrain(timespan, min_timespan, max_timespan);
}

//static
uint32_t chain_state::easy_work_required(data const& values
#if defined(KTH_CURRENCY_BCH)
                            , bool daa_cw144_active
                            , bool daa_asert_active
                            , assert_anchor_block_info_t const& assert_anchor_block_info
                            , uint32_t asert_half_life
#endif
) {
    KTH_ASSERT(values.height != 0);

    // If the time limit has passed allow a minimum difficulty block.
    if (values.timestamp.self > easy_time_limit(values)) {
        return retarget_proof_of_work_limit;
    }

#if defined(KTH_CURRENCY_BCH)
    if (daa_asert_active) {
        return daa_aserti3_2d(values, assert_anchor_block_info, asert_half_life);
    }
    if (daa_cw144_active) {
        return daa_cw144(values);
    }
#endif

    auto height = values.height;
    auto const& bits = values.bits.ordered;

    // Reverse iterate the ordered-by-height list of header bits.
    for (auto bit = bits.rbegin(); bit != bits.rend(); ++bit) {
        if (is_retarget_or_non_limit(--height, *bit)) {
            return *bit;
        }
    }

    // Since the set of heights is either a full retarget range or ends at
    // zero this is not reachable unless the data set is invalid.
    KTH_ASSERT(false);
    return retarget_proof_of_work_limit;
}

//static
uint32_t chain_state::easy_time_limit(chain_state::data const& values) {
    int64_t const high = timestamp_high(values);
    int64_t const spacing = easy_spacing_seconds;

    //*************************************************************************
    // CONSENSUS: add unsigned 32 bit numbers in signed 64 bit space in
    // order to prevent overflow before applying the domain constraint.
    //*************************************************************************
    return domain_constrain<uint32_t>(cast_add<int64_t>(high, spacing));
}

// A retarget height, or a block that does not have proof_of_work_limit bits.
//static
bool chain_state::is_retarget_or_non_limit(size_t height, uint32_t bits) {
    // Zero is a retarget height, ensuring termination before height underflow.
    // This is guaranteed, just asserting here to document the safeguard.
    KTH_ASSERT_MSG(is_retarget_height(0), "loop overflow potential");

    return bits != retarget_proof_of_work_limit || is_retarget_height(height);
}

// Determine if height is a multiple of retargeting_interval.
bool chain_state::is_retarget_height(size_t height) {
    return retarget_distance(height) == 0;
}

// Determine the number of blocks back to the closest retarget height.
size_t chain_state::retarget_distance(size_t height) {
    return height % retargeting_interval;
}

// Publics.
//-----------------------------------------------------------------------------

// static
chain_state::map chain_state::get_map(size_t height, checkpoints const& /*checkpoints*/, uint32_t forks, domain::config::network network) {
    if (height == 0) {
        return {};
    }

    map map;

    // The height bound of the reverse (high to low) retarget search.
    map.bits_self = height;
    map.bits.high = height - 1;
    map.bits.count = bits_count(height, forks);

    // The height bound of the median time past function.
    map.timestamp_self = height;
    map.timestamp.high = height - 1;
    map.timestamp.count = timestamp_count(height, forks);

    // The height bound of the version sample for activations.
    map.version_self = height;
    map.version.high = height - 1;
    map.version.count = version_count(height, forks, network);

    // The most recent past retarget height.
    map.timestamp_retarget = retarget_height(height, forks);

    // The checkpoint above which tx hash collisions are allowed to occur.
    map.allow_collisions_height = collision_height(height, network);

#if ! defined(KTH_CURRENCY_BCH)
    // The checkpoint above which bip9_bit0 rules are enforced.
    map.bip9_bit0_height = bip9_bit0_height(height, forks);

    // The checkpoint above which bip9_bit1 rules are enforced.
    map.bip9_bit1_height = bip9_bit1_height(height, forks);
#endif

    return map;
}

// static
uint32_t chain_state::signal_version(uint32_t forks) {
    if (script::is_enabled(forks, rule_fork::bip65_rule)) {
        return bip65_version;
    }

    if (script::is_enabled(forks, rule_fork::bip66_rule)) {
        return bip66_version;
    }

    if (script::is_enabled(forks, rule_fork::bip34_rule)) {
        return bip34_version;
    }

#if ! defined(KTH_CURRENCY_BCH)
    // TODO(legacy): these can be retired.
    // Signal bip9 bit0 if any of the group is configured.
    if (script::is_enabled(forks, rule_fork::bip9_bit0_group)) {
        return bip9_version_base | bip9_version_bit0;
    }

    // TODO(legacy): these can be retired.
    // Signal bip9 bit1 if any of the group is configured.
    if (script::is_enabled(forks, rule_fork::bip9_bit1_group)) {
        return bip9_version_base | bip9_version_bit1;
    }
#endif

    return first_version;
}

// static
chain_state::data chain_state::to_block(chain_state const& pool, block const& block) {
    // // Alias configured forks.
    // auto const forks = pool.forks_;

    // Copy data from presumed same-height pool state.
    auto data = pool.data_;

    // Replace pool chain state with block state at same (next) height.
    // Preserve data.timestamp.retarget promotion.
    auto const& header = block.header();
    data.hash = header.hash();
    data.bits.self = header.bits();
    data.version.self = header.version();
    data.timestamp.self = header.timestamp();

    // Cache hash of bip34 height block, otherwise use preceding state.
    if (allow_collisions(data.height, pool.network())) {
        data.allow_collisions_hash = data.hash;
    }


#if ! defined(KTH_CURRENCY_BCH)
    // Cache hash of bip9 bit0 height block, otherwise use preceding state.
    if (bip9_bit0_active(data.height, pool.network())) {
        data.bip9_bit0_hash = data.hash;
    }

    // Cache hash of bip9 bit1 height block, otherwise use preceding state.
    if (bip9_bit1_active(data.height, pool.network())) {
        data.bip9_bit1_hash = data.hash;
    }
#endif

    return data;
}

// Semantic invalidity can also arise from too many/few values in the arrays.
// The same computations used to specify the ranges could detect such errors.
// These are the conditions that would cause exception during execution.
bool chain_state::is_valid() const {
    return data_.height != 0;
}

// Properties.
//-----------------------------------------------------------------------------

size_t chain_state::height() const {
    return data_.height;
}

abla::state const& chain_state::abla_state() const {
    return data_.abla_state;
}

uint32_t chain_state::enabled_forks() const {
    return active_.forks;
}

uint32_t chain_state::minimum_version() const {
    return active_.minimum_version;
}

uint32_t chain_state::median_time_past() const {
    return median_time_past_;
}

uint32_t chain_state::work_required() const {
    return work_required_;
}

#if defined(KTH_CURRENCY_BCH)
chain_state::assert_anchor_block_info_t chain_state::assert_anchor_block_info() const {
    return assert_anchor_block_info_;
}

uint32_t chain_state::asert_half_life() const {
    return asert_half_life_;
}

uint64_t chain_state::dynamic_max_block_size() const {
    uint64_t const static_size = static_max_block_size(network());
    if ( ! is_lobachevski_enabled()) {
        return static_size;
    }
    uint64_t const dynamic_size = block_size_limit(data_.abla_state);
    return std::max(static_size, dynamic_size);
}

uint64_t chain_state::dynamic_max_block_sigops() const {
    return dynamic_max_block_size() / max_sigops_factor;
}

uint64_t chain_state::dynamic_max_block_sigchecks() const {
    return dynamic_max_block_size() / block_maxbytes_maxsigchecks_ratio;
}

// uint64_t chain_state::pythagoras_activation_time() const {
//     return pythagoras_activation_time_;
// }

// euclid_t chain_state::euclid_activation_time() const {
//     return euclid_activation_time_;
// }

// pisano_t chain_state::pisano_activation_time() const {
//     return pisano_activation_time_;
// }

// mersenne_t chain_state::mersenne_activation_time() const {
//     return mersenne_activation_time_;
// }

// fermat_t chain_state::fermat_activation_time() const {
//     return fermat_activation_time_;
// }

// euler_t chain_state::euler_activation_time() const {
//     return euler_activation_time_;
// }

// gauss_t chain_state::gauss_activation_time() const {
//     return gauss_activation_time_;
// }

// descartes_t chain_state::descartes_activation_time() const {
//     return descartes_activation_time_;
// }

// lobachevski_t chain_state::lobachevski_activation_time() const {
//     return lobachevski_activation_time_;
// }

// galois_t chain_state::galois_activation_time() const {
//     return galois_activation_time_;
// }

leibniz_t chain_state::leibniz_activation_time() const {
    return leibniz_activation_time_;
}

cantor_t chain_state::cantor_activation_time() const {
    return cantor_activation_time_;
}

#endif  //KTH_CURRENCY_BCH

// Forks.
//-----------------------------------------------------------------------------

bool chain_state::is_enabled(rule_fork fork) const {
    return script::is_enabled(active_.forks, fork);
}

bool chain_state::is_checkpoint_conflict(hash_digest const& hash) const {
    return ! infrastructure::config::checkpoint::validate(hash, data_.height, checkpoints_);
}

bool chain_state::is_under_checkpoint() const {
    // This assumes that the checkpoints are sorted.
    return infrastructure::config::checkpoint::covered(data_.height, checkpoints_);
}

// Mining.
//-----------------------------------------------------------------------------

uint32_t chain_state::get_next_work_required(uint32_t time_now) {
    auto values = data_;
    values.timestamp.self = time_now;
    return work_required(values, network(), enabled_forks()
#if defined(KTH_CURRENCY_BCH)
                            // , euler_activation_time()
                            // , gauss_activation_time()
                            // , descartes_activation_time()
                            // , lobachevski_activation_time()
                            // , galois_activation_time()
                            , leibniz_activation_time()
                            , cantor_activation_time()
                            , assert_anchor_block_info_
                            , asert_half_life()
#endif
    );
}

} // namespace kth::domain::chain
