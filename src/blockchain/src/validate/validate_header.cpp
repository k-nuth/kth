// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/validate/validate_header.hpp>

#include <algorithm>
#include <expected>

#include <spdlog/spdlog.h>

#include <kth/blockchain/header_index.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/domain.hpp>

namespace kth::blockchain {

using namespace kth::domain::chain;
using namespace kth::infrastructure::config;

validate_header::validate_header(settings const& settings, domain::config::network network)
    : settings_(settings)
    , checkpoints_(settings.checkpoints_sorted)
    , configured_flags_(settings.enabled_flags())
    , network_(network)
    , retarget_(network != domain::config::network::regtest)
{}

// Context-free validation
// -----------------------------------------------------------------------------

code validate_header::check(domain::chain::header const& header) const {
    // Delegate to header's check() which validates:
    // 1. Proof of work (hash <= target derived from bits)
    // 2. Timestamp not too far in future (2 hours)
    return header.check(retarget_);
}

// Chain-context validation
// -----------------------------------------------------------------------------

code validate_header::accept(domain::chain::header const& header, size_t height,
                             hash_digest const& previous) const {
    // Validate chain continuity
    if (header.previous_block_hash() != previous) {
        spdlog::warn("[validate_header] accept() Missing parent at height {}: header.prev_hash={}, expected={}",
            height, encode_hash(header.previous_block_hash()), encode_hash(previous));
        return error::store_block_missing_parent;
    }

    auto const hash = header.hash();

    // Validate against checkpoints
    if (is_checkpoint_conflict(hash, height)) {
        return error::checkpoints_failed;
    }

    return error::success;
}

// Combined validation
// -----------------------------------------------------------------------------

code validate_header::validate(domain::chain::header const& header, size_t height,
                               hash_digest const& previous) const {
    // If under checkpoint, we can skip PoW validation (trusted headers)
    // This is a significant optimization during IBD
    if (!is_under_checkpoint(height)) {
        auto const ec = check(header);
        if (ec) {
            return ec;
        }
    }

    return accept(header, height, previous);
}

// Checkpoint helpers
// -----------------------------------------------------------------------------

bool validate_header::is_checkpoint_conflict(hash_digest const& hash, size_t height) const {
    // Check if there's a checkpoint at this height
    auto const it = std::find_if(checkpoints_.begin(), checkpoints_.end(),
        [height](checkpoint const& cp) {
            return cp.height() == height;
        });

    // If checkpoint exists at this height, hash must match
    if (it != checkpoints_.end()) {
        if (hash != it->hash()) {
            spdlog::warn("[validate_header] Checkpoint conflict at height {}: "
                "received hash {} but expected {}",
                height, encode_hash(hash), encode_hash(it->hash()));
            return true;
        }
    }

    return false;
}

bool validate_header::is_under_checkpoint(size_t height) const {
    return height <= last_checkpoint_height();
}

bool validate_header::is_checkpoint_height(size_t height) const {
    return std::any_of(checkpoints_.begin(), checkpoints_.end(),
        [height](checkpoint const& cp) {
            return cp.height() == height;
        });
}

std::optional<hash_digest> validate_header::checkpoint_hash(size_t height) const {
    auto const it = std::find_if(checkpoints_.begin(), checkpoints_.end(),
        [height](checkpoint const& cp) {
            return cp.height() == height;
        });

    if (it != checkpoints_.end()) {
        return it->hash();
    }

    return std::nullopt;
}

size_t validate_header::last_checkpoint_height() const {
    if (checkpoints_.empty()) {
        return 0;
    }

    // Checkpoints are sorted, so last one has highest height
    return checkpoints_.back().height();
}

// =============================================================================
// Full validation with header_index
// =============================================================================

// Precondition: map.bits.high (and version.high, timestamp.high) must equal
// the height of parent_idx. This function traverses backwards from parent_idx,
// so the map ranges must start at the parent's height.
// (When C++26 is available, replace with contract: pre(map.bits.high == index.get_height(parent_idx)))
std::expected<chain_state::data, code> validate_header::collect_historical_data(
    chain_state::map const& map,
    header_index::index_t parent_idx,
    header_index const& index) const {

    KTH_ASSERT(parent_idx == header_index::null_index ||
               map.bits.high == static_cast<size_t>(index.get_height(parent_idx)));

    chain_state::data data{};

    // Resize all containers
    data.bits.ordered.resize(map.bits.count);
    data.version.ordered.resize(map.version.count);
    data.timestamp.ordered.resize(map.timestamp.count);

    // Calculate maximum steps needed
    auto const max_count = std::max({map.bits.count, map.version.count, map.timestamp.count});

    if (max_count == 0) {
        return data;
    }

    // Single traversal collecting all data types
    auto idx = parent_idx;
    for (size_t step = 0; step < max_count && idx != header_index::null_index; ++step) {
        // Collect bits if still needed (store in reverse order)
        if (step < map.bits.count) {
            data.bits.ordered[map.bits.count - 1 - step] = index.get_bits(idx);
        }

        // Collect versions if still needed
        if (step < map.version.count) {
            data.version.ordered[map.version.count - 1 - step] = index.get_version(idx);
        }

        // Collect timestamps if still needed
        if (step < map.timestamp.count) {
            data.timestamp.ordered[map.timestamp.count - 1 - step] = index.get_timestamp(idx);
        }

        idx = index.get_parent_index(idx);
    }

    // Verify we collected enough data
    if (idx == header_index::null_index) {
        // Check if we collected all required data
        auto const parent_height = index.get_height(parent_idx);
        if (parent_height + 1 < int32_t(max_count)) {
            spdlog::warn("[validate_header] collect_historical_data: insufficient chain depth, "
                "need {} but only have {}", max_count, parent_height + 1);
            return std::unexpected(error::operation_failed);
        }
    }

    // Collect retarget timestamp if needed (separate lookup via skip pointers)
    if (map.timestamp_retarget != chain_state::map::unrequested) {
        auto const retarget_height = int32_t(map.timestamp_retarget);
        auto retarget_idx = index.get_ancestor(parent_idx, retarget_height);

        if (retarget_idx == header_index::null_index) {
            spdlog::warn("[validate_header] collect_historical_data: could not find retarget ancestor at height {}",
                retarget_height);
            return std::unexpected(error::operation_failed);
        }

#if defined(KTH_CURRENCY_LTC)
        // Litecoin uses (retarget - 1) for some reason
        if (retarget_height > 0) {
            retarget_idx = index.get_parent_index(retarget_idx);
        }
#endif
        data.timestamp.retarget = index.get_timestamp(retarget_idx);
    }

    return data;
}

#if defined(KTH_CURRENCY_BCH)
chain_state::assert_anchor_block_info_t validate_header::get_asert_anchor_block() const {
    using namespace kth::domain;

    auto const height = network_map(network_
                                , mainnet_asert_anchor_block_height
                                , testnet_asert_anchor_block_height
                                , size_t(0)
                                , testnet4_asert_anchor_block_height
                                , scalenet_asert_anchor_block_height
                                , chipnet_asert_anchor_block_height
                                );

    auto const ancestor_time = network_map(network_
                                , mainnet_asert_anchor_block_ancestor_time
                                , testnet_asert_anchor_block_ancestor_time
                                , size_t(0)
                                , testnet4_asert_anchor_block_ancestor_time
                                , scalenet_asert_anchor_block_ancestor_time
                                , chipnet_asert_anchor_block_ancestor_time
                                );

    uint32_t const bits = network_map(network_
                                , mainnet_asert_anchor_block_bits
                                , testnet_asert_anchor_block_bits
                                , size_t(0)
                                , testnet4_asert_anchor_block_bits
                                , scalenet_asert_anchor_block_bits
                                , chipnet_asert_anchor_block_bits
                                );

    return {height, ancestor_time, bits};
}
#endif // KTH_CURRENCY_BCH

std::expected<chain_state::data, code> validate_header::build_chain_state_data(
    size_t height,
    domain::chain::header const& header,
    hash_digest const& hash,
    header_index::index_t parent_idx,
    header_index const& index) const {

    if (height == 0) {
        spdlog::warn("[validate_header] build_chain_state_data: height 0 is not valid");
        return std::unexpected(error::operation_failed);
    }

    // Get the map that defines what data we need to collect
    auto const map = chain_state::get_map(height, checkpoints_, configured_flags_, network_);

    if (height == 32256) {
        spdlog::warn("[validate_header] DEBUG height 32256: map.bits.count={}, map.bits.high={}, "
            "map.timestamp.count={}, map.timestamp_retarget={}, configured_flags_={:#x}, "
            "parent_idx={}, parent_height={}",
            map.bits.count, map.bits.high,
            map.timestamp.count, map.timestamp_retarget, configured_flags_,
            parent_idx, index.get_height(parent_idx));
    }

    // Collect historical data from header_index (single traversal)
    auto result = collect_historical_data(map, parent_idx, index);
    if (!result) {
        return std::unexpected(result.error());
    }

    auto& data = *result;

    // Set metadata
    data.height = height;
    data.hash = hash;

    // Set self values from the header being validated
    data.bits.self = header.bits();
    data.version.self = header.version();
    data.timestamp.self = header.timestamp();

    // Handle collision hash (for duplicate tx hash detection)
    if (map.allow_collisions_height != chain_state::map::unrequested) {
        auto const collision_idx = index.get_ancestor(parent_idx, int32_t(map.allow_collisions_height));
        data.allow_collisions_hash = (collision_idx != header_index::null_index)
            ? index.get_hash(collision_idx)
            : null_hash;
    } else {
        data.allow_collisions_hash = null_hash;
    }

#if ! defined(KTH_CURRENCY_BCH)
    // BIP9 bit0/bit1 hashes
    if (map.bip9_bit0_height != chain_state::map::unrequested) {
        auto const bip9_idx = index.get_ancestor(parent_idx, int32_t(map.bip9_bit0_height));
        data.bip9_bit0_hash = (bip9_idx != header_index::null_index)
            ? index.get_hash(bip9_idx)
            : null_hash;
    } else {
        data.bip9_bit0_hash = null_hash;
    }

    if (map.bip9_bit1_height != chain_state::map::unrequested) {
        auto const bip9_idx = index.get_ancestor(parent_idx, int32_t(map.bip9_bit1_height));
        data.bip9_bit1_hash = (bip9_idx != header_index::null_index)
            ? index.get_hash(bip9_idx)
            : null_hash;
    } else {
        data.bip9_bit1_hash = null_hash;
    }
#endif

#if defined(KTH_CURRENCY_BCH)
    // ABLA state - we don't have block size during header validation
    // Initialize with default values; actual block size validation happens during block sync
    data.abla_state = domain::chain::abla::state(settings_.abla_config, static_max_block_size(network_));
#endif

    return data;
}

code validate_header::accept_full(domain::chain::header const& header,
                                  hash_digest const& hash,
                                  size_t height,
                                  header_index::index_t parent_idx,
                                  header_index const& index) const {
    // Basic chain continuity check
    if (parent_idx != header_index::null_index) {
        auto const parent_hash = index.get_hash(parent_idx);
        if (header.previous_block_hash() != parent_hash) {
            spdlog::warn("[validate_header] Missing parent at height {}: header.prev_hash={}, expected parent_hash={}, parent_idx={}",
                height, encode_hash(header.previous_block_hash()), encode_hash(parent_hash), parent_idx);
            return error::store_block_missing_parent;
        }
    } else if (height != 0) {
        // Non-genesis block without parent
        spdlog::warn("[validate_header] Missing parent at height {}: no parent_idx but height != 0", height);
        return error::store_block_missing_parent;
    }

    // Build chain_state data from header_index
    auto data_result = build_chain_state_data(height, header, hash, parent_idx, index);
    if (!data_result) {
        spdlog::error("[validate_header] accept_full: failed to build chain_state data for height {}", height);
        return data_result.error();
    }

    // Create chain_state from the data
#if defined(KTH_CURRENCY_BCH)
    auto const anchor = get_asert_anchor_block();
    chain_state state(
        std::move(*data_result),
        configured_flags_,
        checkpoints_,
        network_,
        anchor,
        settings_.asert_half_life,
        settings_.abla_config,
        leibniz_t(settings_.leibniz_activation_time),
        cantor_t(settings_.cantor_activation_time)
    );
#else
    chain_state state(
        std::move(*data_result),
        configured_flags_,
        checkpoints_,
        network_
    );
#endif

    // Now perform the full header validation using chain_state
    // This is equivalent to header_basis::accept()

    // 1. Difficulty check
    if (height == 32256) {
        spdlog::warn("[validate_header] DEBUG height 32256: work_required={:#x}, header.bits={:#x}, "
            "data.timestamp.retarget={}, data.bits.ordered.size={}, "
            "data.timestamp.ordered.size={}",
            state.work_required(), header.bits(),
            data_result->timestamp.retarget,
            data_result->bits.ordered.size(),
            data_result->timestamp.ordered.size());
    }
    if (header.bits() != state.work_required()) {
        spdlog::debug("[validate_header] accept_full: incorrect PoW at height {}: "
            "header bits={:#x}, required={:#x}",
            height, header.bits(), state.work_required());
        return error::incorrect_proof_of_work;
    }

    // 2. Checkpoint conflict check
    if (state.is_checkpoint_conflict(hash)) {
        return error::checkpoints_failed;
    }

    // 3. Under checkpoint - skip remaining checks
    if (state.is_under_checkpoint()) {
        return error::success;
    }

    // 4. Version check
    if (header.version() < state.minimum_version()) {
        spdlog::debug("[validate_header] accept_full: old version at height {}: "
            "header version={}, minimum={}",
            height, header.version(), state.minimum_version());
        return error::old_version_block;
    }

    // 5. Median time past check
    if (header.timestamp() <= state.median_time_past()) {
        spdlog::debug("[validate_header] accept_full: timestamp too early at height {}: "
            "header timestamp={}, MTP={}",
            height, header.timestamp(), state.median_time_past());
        return error::timestamp_too_early;
    }

    return error::success;
}

// =============================================================================
// Static validation functions (pure, no side effects)
// =============================================================================

code validate_header::accept_header(
    domain::chain::header const& header,
    hash_digest const& hash,
    domain::chain::chain_state const& state) {

    // 1. Difficulty check
    if (header.bits() != state.work_required()) {
        return error::incorrect_proof_of_work;
    }

    // 2. Checkpoint conflict check
    if (state.is_checkpoint_conflict(hash)) {
        return error::checkpoints_failed;
    }

    // 3. Under checkpoint - skip remaining checks
    if (state.is_under_checkpoint()) {
        return error::success;
    }

    // 4. Version check
    if (header.version() < state.minimum_version()) {
        return error::old_version_block;
    }

    // 5. Median time past check
    if (header.timestamp() <= state.median_time_past()) {
        return error::timestamp_too_early;
    }

    return error::success;
}

} // namespace kth::blockchain
