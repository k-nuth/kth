// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/populate/populate_chain_state.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

#include <kth/domain.hpp>


#include <kth/blockchain/define.hpp>
#include <kth/blockchain/interface/fast_chain.hpp>
#include <kth/blockchain/pools/branch.hpp>
#include <kth/blockchain/settings.hpp>

#include <kth/infrastructure/utility/limits.hpp>
#include <kth/infrastructure/utility/timer.hpp>

namespace kth::blockchain {

using namespace kd::chain;

// This value should never be read, but may be useful in debugging.
static constexpr uint32_t unspecified = max_uint32;

// Database access is limited to:
// get_last_height
// block: { hash, bits, version, timestamp }

populate_chain_state::populate_chain_state(fast_chain const& chain, settings const& settings, domain::config::network network)
    :
#if defined(KTH_CURRENCY_BCH)
      settings_(settings),
#endif //KTH_CURRENCY_BCH
      configured_forks_(settings.enabled_forks())
    , checkpoints_(infrastructure::config::checkpoint::sort(settings.checkpoints))
    , network_(network)
    , fast_chain_(chain)
{}

inline
bool is_transaction_pool(branch::const_ptr branch) {
    return branch->empty();
}

bool populate_chain_state::get_bits(uint32_t& out_bits, size_t height, branch::const_ptr branch) const {
    // branch returns false only if the height is out of range.
    return branch->get_bits(out_bits, height) || fast_chain_.get_bits(out_bits, height);
}

bool populate_chain_state::get_version(uint32_t& out_version, size_t height, branch::const_ptr branch) const {
    // branch returns false only if the height is out of range.
    return branch->get_version(out_version, height) || fast_chain_.get_version(out_version, height);
}

bool populate_chain_state::get_timestamp(uint32_t& out_timestamp, size_t height, branch::const_ptr branch) const {
    // branch returns false only if the height is out of range.
    return branch->get_timestamp(out_timestamp, height) || fast_chain_.get_timestamp(out_timestamp, height);
}

bool populate_chain_state::get_block_hash(hash_digest& out_hash, size_t height, branch::const_ptr branch) const {
    return branch->get_block_hash(out_hash, height) || fast_chain_.get_block_hash(out_hash, height);
}

bool populate_chain_state::populate_bits(chain_state::data& data, chain_state::map const& map, branch::const_ptr branch) const {
    auto& bits = data.bits.ordered;
    bits.resize(map.bits.count);
    auto height = map.bits.high - map.bits.count;

    for (auto& bit: bits) {
        if ( ! get_bits(bit, ++height, branch)) {
            return false;
        }
    }

    if (is_transaction_pool(branch)) {
        // This is an unused value.
        data.bits.self = work_limit(true);
        return true;
    }

    return get_bits(data.bits.self, map.bits_self, branch);
}

bool populate_chain_state::populate_versions(chain_state::data& data, chain_state::map const& map, branch::const_ptr branch) const {
    auto& versions = data.version.ordered;
    versions.resize(map.version.count);
    auto height = map.version.high - map.version.count;

    for (auto& version: versions) {
        if ( ! get_version(version, ++height, branch)) {
            return false;
        }
    }

    if (is_transaction_pool(branch)) {
        data.version.self = chain_state::signal_version(configured_forks_);
        return true;
    }

    return get_version(data.version.self, map.version_self, branch);
}

bool populate_chain_state::populate_timestamps(chain_state::data& data, chain_state::map const& map, branch::const_ptr branch) const {
    data.timestamp.retarget = unspecified;
    auto& timestamps = data.timestamp.ordered;
    timestamps.resize(map.timestamp.count);
    auto height = map.timestamp.high - map.timestamp.count;

    for (auto& timestamp: timestamps) {
        if ( ! get_timestamp(timestamp, ++height, branch)) {
            return false;
        }
    }

    // Retarget is required if timestamp_retarget is not unrequested.
    if (map.timestamp_retarget != chain_state::map::unrequested &&
// #ifdef LITECOIN
#ifdef KTH_CURRENCY_LTC
        ! get_timestamp(data.timestamp.retarget, map.timestamp_retarget != 0 ? map.timestamp_retarget - 1 : 0, branch))
#else
        ! get_timestamp(data.timestamp.retarget, map.timestamp_retarget, branch))
#endif //KTH_CURRENCY_LTC
    {
        return false;
    }

    if (is_transaction_pool(branch)) {
        data.timestamp.self = static_cast<uint32_t>(zulu_time());
        return true;
    }

    return get_timestamp(data.timestamp.self, map.timestamp_self, branch);
}

bool populate_chain_state::populate_collision(chain_state::data& data, chain_state::map const& map, branch::const_ptr branch) const {
    if (map.allow_collisions_height == chain_state::map::unrequested) {
        data.allow_collisions_hash = null_hash;
        return true;
    }

    if (is_transaction_pool(branch)) {
        data.allow_collisions_hash = null_hash;
        return true;
    }

    return get_block_hash(data.allow_collisions_hash, map.allow_collisions_height, branch);
}

#if ! defined(KTH_CURRENCY_BCH)
bool populate_chain_state::populate_bip9_bit0(chain_state::data& data, chain_state::map const& map, branch::const_ptr branch) const {
    if (map.bip9_bit0_height == chain_state::map::unrequested) {
        data.bip9_bit0_hash = null_hash;
        return true;
    }

    return get_block_hash(data.bip9_bit0_hash, map.bip9_bit0_height, branch);
}

bool populate_chain_state::populate_bip9_bit1(chain_state::data& data,
    chain_state::map const& map, branch::const_ptr branch) const {
    if (map.bip9_bit1_height == chain_state::map::unrequested) {
        data.bip9_bit1_hash = null_hash;
        return true;
    }

    return get_block_hash(data.bip9_bit1_hash,
        map.bip9_bit1_height, branch);
}
#endif


bool populate_chain_state::populate_all(chain_state::data& data, branch::const_ptr branch) const {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    unique_lock lock(mutex_);

    // Construct a map to inform chain state data population.
    auto const map = chain_state::get_map(data.height, checkpoints_, configured_forks_, network_);

    return (
        populate_bits(data, map, branch)
        && populate_versions(data, map, branch)
        && populate_timestamps(data, map, branch)
        && populate_collision(data, map, branch)
#if ! defined(KTH_CURRENCY_BCH)
        && populate_bip9_bit0(data, map, branch)
        && populate_bip9_bit1(data, map, branch)
#endif
    );
    ///////////////////////////////////////////////////////////////////////////
}

#if defined(KTH_CURRENCY_BCH)

chain_state::assert_anchor_block_info_t populate_chain_state::get_assert_anchor_block(domain::config::network network) const {

    auto const height = network_map(network
                                , mainnet_asert_anchor_block_height
                                , testnet_asert_anchor_block_height
                                , size_t(0)
                                , testnet4_asert_anchor_block_height
                                , scalenet_asert_anchor_block_height
                                , chipnet_asert_anchor_block_height
                                );

    auto const ancestor_time = network_map(network
                                , mainnet_asert_anchor_block_ancestor_time
                                , testnet_asert_anchor_block_ancestor_time
                                , size_t(0)
                                , testnet4_asert_anchor_block_ancestor_time
                                , scalenet_asert_anchor_block_ancestor_time
                                , chipnet_asert_anchor_block_ancestor_time
                                );

    //TODO(fernando): make the function network_map generic
    uint32_t const bits = network_map(network
                                , mainnet_asert_anchor_block_bits
                                , testnet_asert_anchor_block_bits
                                , size_t(0)
                                , testnet4_asert_anchor_block_bits
                                , scalenet_asert_anchor_block_bits
                                , chipnet_asert_anchor_block_bits
                                );

    return {height, ancestor_time, bits};
}

#endif // defined(KTH_CURRENCY_BCH)

chain_state::ptr populate_chain_state::populate() const {
    size_t top;
    if ( ! fast_chain_.get_last_height(top)) {
        spdlog::error("[blockchain] Failed to populate chain state, last height.");
        return {};
    }
    auto opt = fast_chain_.get_header_and_abla_state(top);
    if ( ! opt) {
        spdlog::error("[blockchain] Failed to populate chain state, last header.");
        return {};
    }
    auto [last_header, block_size, control_block_size, elastic_buffer_size] = *opt;
    if ( ! last_header.is_valid()) {
        spdlog::error("[blockchain] Failed to populate chain state, last header.");
        return {};
    }

    chain_state::data data;
    data.hash = null_hash;
    data.height = *safe_add(top, size_t(1));

    if (block_size == 0) {
        data.abla_state = abla::state(settings_.abla_config, static_max_block_size(network_));
    } else {
        data.abla_state = abla::state(settings_.abla_config, block_size);
        data.abla_state.control_block_size = control_block_size;
        data.abla_state.elastic_buffer_size = elastic_buffer_size;
    }

    auto branch_ptr = std::make_shared<branch>(top);

    // Use an empty branch to represent the transaction pool.
    if ( ! populate_all(data, branch_ptr)) {
        spdlog::error("[blockchain] Failed to populate chain state, all.");
        return {};
    }

#if defined(KTH_CURRENCY_BCH)
    auto const anchor = get_assert_anchor_block(network_);
#endif

    auto ret = std::make_shared<chain_state>(
        std::move(data)
        , configured_forks_
        , checkpoints_
        , network_
#if defined(KTH_CURRENCY_BCH)
        , anchor
        , settings_.asert_half_life
        , settings_.abla_config
        // , settings_.pythagoras_activation_time
        // , settings_.euclid_activation_time
        // , settings_.pisano_activation_time
        // , settings_.mersenne_activation_time
        // , fermat_t(settings_.fermat_activation_time)
        // , euler_t(settings_.euler_activation_time)
        // , gauss_t(settings_.gauss_activation_time)
        // , descartes_t(settings_.descartes_activation_time)
        // , lobachevski_t(settings_.lobachevski_activation_time)
        // , galois_t(settings_.galois_activation_time)
        , leibniz_t(settings_.leibniz_activation_time)
        , cantor_t(settings_.cantor_activation_time)
#endif //KTH_CURRENCY_BCH
    );

    return ret;
}

chain_state::ptr populate_chain_state::populate(chain_state::ptr pool, branch::const_ptr branch) const {
    auto const block = branch->top();
    KTH_ASSERT(block);

    // If this is not a reorganization we can just promote the pool state.
    if (branch->size() == 1 && branch->top_height() == pool->height()) {
        return chain_state::from_pool_ptr(*pool, *block);
    }

    auto const height = branch->top_height();
    chain_state::data data;
    data.hash = block->hash();
    data.height = height;

    // Caller must test result.
    if ( ! populate_all(data, branch)) {
        return {};
    }

    // Before activating lobachevski, we need to check if it is enabled using the median time past.
    // auto const is_lobachevski_enabled = chain_state::is_mtp_activated(chain_state::median_time_past(data), settings_.lobachevski_activation_time);
    // After activating lobachevski, we need to check if it is enabled using the height.
    auto const is_lobachevski_enabled = chain_state::is_lobachevski_enabled(height, network_);
    if (is_lobachevski_enabled) {
        if ( ! pool->is_lobachevski_enabled()) {
            data.abla_state = abla::state(settings_.abla_config, block->serialized_size(1));
        } else {
            auto const abla_config_opt = abla::next(pool->abla_state(), settings_.abla_config, block->serialized_size(1));
            if ( ! abla_config_opt) {
                return {};
            }
            data.abla_state = *abla_config_opt;
        }
    } else {
        data.abla_state = abla::state(settings_.abla_config, block->serialized_size(1));
    }

    return std::make_shared<chain_state>(
        std::move(data)
        , configured_forks_
        , checkpoints_
        , network_
#if defined(KTH_CURRENCY_BCH)
        , pool->assert_anchor_block_info()
        , settings_.asert_half_life
        , settings_.abla_config
        // , settings_.pythagoras_activation_time
        // , settings_.euclid_activation_time
        // , settings_.pisano_activation_time
        // , settings_.mersenne_activation_time
        // , fermat_t(settings_.fermat_activation_time)
        // , euler_t(settings_.euler_activation_time)
        // , gauss_t(settings_.gauss_activation_time)
        // , descartes_t(settings_.descartes_activation_time)
        // , lobachevski_t(settings_.lobachevski_activation_time)
        // , galois_t(settings_.galois_activation_time)
        , leibniz_t(settings_.leibniz_activation_time)
        , cantor_t(settings_.cantor_activation_time)
#endif //KTH_CURRENCY_BCH
    );
}

chain_state::ptr populate_chain_state::populate(chain_state::ptr top) const {
    // Create pool state from top block chain state.
    auto const state = std::make_shared<chain_state>(*top);

    // Invalidity is not possible unless next height is zero.
    // This can only happen when the chain size overflows size_t.
    KTH_ASSERT(state->is_valid());

    return state;
}

} // namespace kth::blockchain
