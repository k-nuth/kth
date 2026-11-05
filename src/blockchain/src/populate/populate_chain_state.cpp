// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/populate/populate_chain_state.hpp>

#include <kth/blockchain/abla_pipeline.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include <kth/domain.hpp>
#include <kth/domain/chain/faster_blocks.hpp>


#include <kth/blockchain/define.hpp>
#include <kth/blockchain/interface/block_chain.hpp>
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

populate_chain_state::populate_chain_state(block_chain const& chain, settings const& settings, domain::config::network network)
    :
#if defined(KTH_CURRENCY_BCH)
      settings_(settings),
#endif //KTH_CURRENCY_BCH
      configured_flags_(settings.enabled_flags())
    , checkpoints_(settings.checkpoints_sorted)
    , network_(network)
    , chain_(chain)
{}

inline
bool is_transaction_pool(branch::const_ptr branch) {
    return branch->empty();
}

namespace detail {

// No production path writes this. Armed only by a control that needs the ABLA
// lookup to fail with something other than an absent row, which is the one
// distinction that cannot be arranged from outside: an absent row is the
// ordinary case, a read that FAILS needs a broken store.
std::atomic<bool> abla_lookup_fault{false};

void fail_abla_lookup(bool enabled) {
    abla_lookup_fault.store(enabled, std::memory_order_release);
}

bool abla_lookup_faulted() {
    return abla_lookup_fault.load(std::memory_order_acquire);
}

} // namespace detail

// The header index entry for an active-chain height, or null_index.
//
// Every reader below goes through here first. They are asked for a WINDOW of
// heights -- the retarget span, the median-time-past span -- not just the tip, so
// a run whose durable table lags by even one block cannot build a chain state at
// all if they read the table. That is what ended the mainnet run in #697: the
// header lookup was only the first thing to fail.
inline
header_index::index_t active_index_at(block_chain const& chain, size_t height) {
    return chain.headers().active_at(static_cast<int32_t>(height));
}

bool populate_chain_state::get_bits(uint32_t& out_bits, size_t height, branch::const_ptr branch) const {
    // branch returns false only if the height is out of range.
    if (branch->get_bits(out_bits, height)) {
        return true;
    }
    // From the index: it is the runtime source of truth for headers, and the
    // durable table is a checkpoint that lags.
    if (auto const idx = active_index_at(chain_, height);
        idx != header_index::null_index) {
        out_bits = chain_.headers().get_bits(idx);
        return true;
    }

    // Hydration only. The index is not materialised until sync_tip() runs, and a
    // start publishes a chain view before that.
    if ( ! chain_.hydrating()) {
        return false;
    }
    auto result = chain_.get_bits(height);
    if ( ! result) {
        return false;
    }
    out_bits = *result;
    return true;
}

bool populate_chain_state::get_version(uint32_t& out_version, size_t height, branch::const_ptr branch) const {
    // branch returns false only if the height is out of range.
    if (branch->get_version(out_version, height)) {
        return true;
    }
    // From the index: it is the runtime source of truth for headers, and the
    // durable table is a checkpoint that lags.
    if (auto const idx = active_index_at(chain_, height);
        idx != header_index::null_index) {
        out_version = chain_.headers().get_version(idx);
        return true;
    }

    // Hydration only. The index is not materialised until sync_tip() runs, and a
    // start publishes a chain view before that.
    if ( ! chain_.hydrating()) {
        return false;
    }
    auto result = chain_.get_version(height);
    if ( ! result) {
        return false;
    }
    out_version = *result;
    return true;
}

bool populate_chain_state::get_timestamp(uint32_t& out_timestamp, size_t height, branch::const_ptr branch) const {
    // branch returns false only if the height is out of range.
    if (branch->get_timestamp(out_timestamp, height)) {
        return true;
    }
    // From the index: it is the runtime source of truth for headers, and the
    // durable table is a checkpoint that lags.
    if (auto const idx = active_index_at(chain_, height);
        idx != header_index::null_index) {
        out_timestamp = chain_.headers().get_timestamp(idx);
        return true;
    }

    // Hydration only. The index is not materialised until sync_tip() runs, and a
    // start publishes a chain view before that.
    if ( ! chain_.hydrating()) {
        return false;
    }
    auto result = chain_.get_timestamp(height);
    if ( ! result) {
        return false;
    }
    out_timestamp = *result;
    return true;
}

bool populate_chain_state::get_block_hash(hash_digest& out_hash, size_t height, branch::const_ptr branch) const {
    if (branch->get_block_hash(out_hash, height)) {
        return true;
    }
    if (auto const idx = active_index_at(chain_, height);
        idx != header_index::null_index) {
        out_hash = chain_.headers().get_hash(idx);
        return true;
    }

    if ( ! chain_.hydrating()) {
        return false;
    }
    auto result = chain_.get_block_hash(height);
    if ( ! result) {
        return false;
    }
    out_hash = *result;
    return true;
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
        data.version.self = chain_state::signal_version(configured_flags_);
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
        data.timestamp.self = uint32_t(zulu_time());
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


#if defined(KTH_CURRENCY_BCH)
bool populate_chain_state::populate_faster_blocks(chain_state::data& data, branch::const_ptr branch) const {
    data.faster_blocks_activation_height = max_size_t;

    cantor_t const activation_time{settings_.cantor_activation_time};
    if ( ! chain_state::is_faster_blocks_activated(chain_state::median_time_past(data), activation_time)) {
        return true;
    }

    // data.height is the block being described; its parent has the MTP above.
    auto const found = domain::chain::find_faster_blocks_activation_height(
        size_t(data.height) - 1, settings_.cantor_activation_time,
        [&](size_t height) -> std::optional<uint32_t> {
            uint32_t timestamp = 0;
            if ( ! get_timestamp(timestamp, height, branch)) {
                return std::nullopt;
            }
            return timestamp;
        });

    if ( ! found) {
        spdlog::error("[blockchain] Failed to populate chain state: could not locate the "
                      "faster-blocks activation height below {}", data.height);
        return false;
    }

    data.faster_blocks_activation_height = *found;
    return true;
}
#endif

bool populate_chain_state::populate_all(chain_state::data& data, branch::const_ptr branch) const {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    unique_lock lock(mutex_);

    // Construct a map to inform chain state data population.
    auto const map = chain_state::get_map(data.height, checkpoints_, configured_flags_, network_);

    return (
        populate_bits(data, map, branch)
        && populate_versions(data, map, branch)
        && populate_timestamps(data, map, branch)
        && populate_collision(data, map, branch)
#if defined(KTH_CURRENCY_BCH)
        && populate_faster_blocks(data, branch)
#else
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

chain_state::ptr populate_chain_state::populate(size_t connected_top) const {
    // From the header index, which is the runtime source of truth for headers.
    // `internal_db` is a durable checkpoint: it is written incrementally and read
    // back when the node hydrates or recovers, and asking it during a run makes
    // the chain state depend on how far a checkpoint happens to have got. It did
    // -- a run whose chain grew past the point where header sync declared itself
    // complete reached a height whose header had never been written, could not
    // describe the batch it had just connected, and could not be reopened
    // afterwards because a start publishes through this same call (#697).
    auto const& index = chain_.headers();
    auto const idx = index.active_at(static_cast<int32_t>(connected_top));

    uint64_t block_size = 0;
    uint64_t control_block_size = 0;
    uint64_t elastic_buffer_size = 0;

    domain::chain::header last_header;
    if (idx != header_index::null_index) {
        last_header = index.get_header(idx);
    } else if ( ! chain_.hydrating()) {
        // Running, and the index cannot produce a header it is the source of
        // truth for. That is a broken invariant, not a cache miss, and answering
        // it from the durable checkpoint would restore exactly the dependency
        // this change removes -- the chain state would again be decided by how
        // far a checkpoint happened to get (#697).
        spdlog::error("[blockchain] Failed to populate chain state: the header index has no "
                      "header at height {} while the node is running", connected_top);
        return {};
    } else {
        // Hydration, and the only place the durable table is the right one to
        // ask. block_chain::start publishes a chain view before the organizer has
        // materialised the active chain, so active_at() answers null for every
        // height until sync_tip() runs. Reading the checkpoint here is what a
        // start is for -- and the phase is explicit, so it cannot be entered
        // later by an absent entry alone.
        auto const stored = chain_.get_header_and_abla_state(connected_top);
        if ( ! stored) {
            spdlog::error("[blockchain] Failed to populate chain state: no header at height {}",
                          connected_top);
            return {};
        }
        last_header = std::get<0>(*stored);
    }

    // Header identity comes from the active index at runtime; the ABLA state
    // comes from its durable record. Check the identities agree before using
    // that record. A missing row is an unknown state, while other read errors
    // are storage failures. Neither may be replaced with an invented post-ABLA
    // floor: advance_abla_state below permits initialization only at the real
    // activation boundary (or the first successor of genesis).
    // The connect writer persists header gaps and ABLA records before publishing
    // the new connected tip, so a normal publication has its required state.
    auto const stored = detail::abla_lookup_faulted()
        ? std::expected<database::header_with_abla_state_t, database::result_code>(
              std::unexpected(database::result_code::other))
        : chain_.get_header_and_abla_state(connected_top);

    if (stored) {
        if (domain::chain::hash(std::get<0>(*stored)) != domain::chain::hash(last_header)) {
            spdlog::error("[blockchain] Persisted ABLA header does not match the connected branch at {}",
                connected_top);
            return {};
        }
        block_size = std::get<1>(*stored);
        control_block_size = std::get<2>(*stored);
        elastic_buffer_size = std::get<3>(*stored);
    } else if (stored.error() != database::result_code::key_not_found) {
        spdlog::error("[blockchain] Failed to populate chain state: reading the ABLA state at "
                      "height {} failed with {}", connected_top,
                      database::result_code_name(stored.error()));
        return {};
    }

    chain_state::data data;
    data.hash = null_hash;
    data.height = *safe_add(connected_top, size_t(1));

    auto branch_ptr = std::make_shared<branch>(connected_top, chain_.block_validations());

    // Use an empty branch to represent the transaction pool.
    if ( ! populate_all(data, branch_ptr)) {
        spdlog::error("[blockchain] Failed to populate chain state, all.");
        return {};
    }

    // The ABLA state of the CANDIDATE (abla_pipeline.hpp): the record read above
    // is the state of the connected tip, and the block this state describes is
    // the one after it, so the state advances once here -- from the tip's size
    // and the tip's interval -- and nobody downstream advances it again. The
    // candidate's own size is not known: 0. The intervals come from the schedule
    // populate_all just established for this branch.
#if defined(KTH_CURRENCY_BCH)
    auto const schedule = chain_state::branch_ticks(data, cantor_t(settings_.cantor_activation_time));
    auto const tip_state = abla_state_from_record(settings_, block_size, control_block_size,
        elastic_buffer_size, schedule.block_interval(connected_top));
    if (!tip_state) {
        return {};
    }
    auto const candidate_state = advance_abla_state(settings_, network_, *tip_state, data.height,
        0, schedule.block_interval(data.height));
    if ( ! candidate_state) {
        spdlog::error("[blockchain] Failed to populate chain state: the ABLA state at height {} "
                      "cannot be represented", data.height);
        return {};
    }
    data.abla_state = *candidate_state;
#endif

#if defined(KTH_CURRENCY_BCH)
    auto const anchor = get_assert_anchor_block(network_);
#endif

    auto ret = std::make_shared<chain_state>(
        std::move(data)
        , configured_flags_
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
        // , leibniz_t(settings_.leibniz_activation_time)
        , cantor_t(settings_.cantor_activation_time)
#endif //KTH_CURRENCY_BCH
    );

    return ret;
}

chain_state::ptr populate_chain_state::populate(chain_state::ptr pool, branch::const_ptr branch) const {
    auto const block = branch->top();
    KTH_ASSERT(block);

    // Always the general path, including a branch of one block over the pool's
    // own height: the pool's state is the CANDIDATE's (size unknown) and cannot
    // be promoted to the state of a block whose size is known without changing
    // its ABLA state, and the state of a side branch must come from that
    // branch's own ancestors, never from the pool of the active chain.
    auto const height = branch->top_height();
    chain_state::data data;
    data.hash = block->hash();
    data.height = height;

    // Caller must test result.
    if ( ! populate_all(data, branch)) {
        return {};
    }

    // S_height: the state of the branch ancestor at the fork point (a connected
    // block, so its record is the state of that block on this branch too) advanced
    // once through every block of the branch, each at its interval on the
    // branch's own schedule.
#if defined(KTH_CURRENCY_BCH)
    auto const schedule = chain_state::branch_ticks(data, cantor_t(settings_.cantor_activation_time));
    std::optional<abla::state> ancestor;
    if (auto const stored = chain_.get_header_and_abla_state(branch->height()); stored) {
        if (domain::chain::hash(std::get<0>(*stored)) != branch->hash()) {
            spdlog::error("[blockchain] Persisted ABLA header does not match the branch fork at {}",
                branch->height());
            return {};
        }
        auto const parsed = abla_state_from_record(settings_, std::get<1>(*stored), std::get<2>(*stored),
            std::get<3>(*stored), schedule.block_interval(branch->height()));
        if (!parsed) {
            return {};
        }
        ancestor = *parsed;
    } else if (stored.error() != database::result_code::key_not_found) {
        spdlog::error("[blockchain] Failed to populate branch chain state: reading the ABLA state at "
                      "the fork height {} failed with {}", branch->height(),
                      database::result_code_name(stored.error()));
        return {};
    }

    auto const& blocks = *branch->blocks();
    size_t block_height = size_t(branch->height());
    std::optional<abla::state> current = ancestor;
    for (auto const& member : blocks) {
        ++block_height;
        current = advance_abla_state(settings_, network_, current, block_height,
            member->serialized_size(1), schedule.block_interval(block_height));
        if ( ! current) {
            return {};
        }
    }
    if ( ! current) {
        return {};
    }
    data.abla_state = *current;
#endif

    return std::make_shared<chain_state>(
        std::move(data)
        , configured_flags_
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
        // , leibniz_t(settings_.leibniz_activation_time)
        , cantor_t(settings_.cantor_activation_time)
#endif //KTH_CURRENCY_BCH
    );
}

chain_state::ptr populate_chain_state::populate(chain_state::ptr top) const {
    // Create pool state from top block chain state.
    auto const state = std::make_shared<chain_state>(*top);

    // A null (zero-height) state can only happen when the chain size
    // overflows size_t.
    KTH_ASSERT( ! state->is_null());

    return state;
}

} // namespace kth::blockchain
