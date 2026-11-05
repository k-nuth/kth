// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/abla_pipeline.hpp>

#if defined(KTH_CURRENCY_BCH)
#include <spdlog/spdlog.h>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/blockchain/tick_schedule.hpp>
#include <kth/blockchain/validate/validate_header.hpp>
#include <kth/database/databases/header_abla_entry.hpp>
#include <kth/domain/constants.hpp>

namespace kth::blockchain {

using domain::chain::abla::state;

std::expected<std::optional<state>, code> abla_state_from_record(
    settings const& settings, uint64_t block_size, uint64_t control_block_size,
    uint64_t elastic_buffer_size, uint64_t interval) {
    if (control_block_size == 0 && elastic_buffer_size == 0) {
        return std::optional<state>{};
    }
    state ret(settings.abla_config, block_size, interval);
    ret.control_block_size = control_block_size;
    ret.elastic_buffer_size = elastic_buffer_size;
    if (block_size == 0 || domain::chain::abla::validate(ret, settings.abla_config) !=
            domain::chain::abla::state_validity::valid) {
        spdlog::error("[abla] Invalid persisted ABLA state; rebuild the chain state before resuming");
        return std::unexpected(error::operation_failed);
    }
    return std::optional<state>{ret};
}

std::optional<state> advance_abla_state(
    settings const& settings, domain::config::network network,
    std::optional<state> const& parent, size_t height, uint64_t block_size, uint64_t interval) {
    // Pre-activation bookkeeping starts from the initial values. The first
    // governed block already consumes the preceding block's actual size, so
    // it must advance from that parent just like every later governed block.
    auto const enabled = domain::chain::chain_state::is_lobachevski_enabled(height, network);
    if (interval == 0 || interval > domain::chain::abla::base_interval) {
        return std::nullopt;
    }
    if (parent && domain::chain::abla::validate(*parent, settings.abla_config) !=
            domain::chain::abla::state_validity::valid) {
        return std::nullopt;
    }
    if (!enabled || height == 0) {
        return state(settings.abla_config, block_size, interval);
    }
    if (!parent) {
        // The genesis record has no ABLA state. Its first successor starts at
        // the known initial state. Every later post-ABLA block requires the
        // real parent, even at 600 ticks: fabricating a legacy floor would let
        // the next connect persist it as apparently trusted state and carry
        // that invention across the shorter-interval activation.
        if (height > 1) {
            spdlog::error("[abla] Missing parent state for post-ABLA block {}; "
                "rebuild the chain state before resuming", height);
            return std::nullopt;
        }
        return state(settings.abla_config, block_size, interval);
    }
    return domain::chain::abla::next(*parent, settings.abla_config, block_size, interval);
}

std::expected<std::vector<state>, code> abla_states_for_batch(
    block_chain const& chain, settings const& settings, domain::config::network network,
    uint32_t start_height, std::span<uint64_t const> sizes,
    header_index::index_t last_idx) {
    std::vector<state> out;
    if (sizes.empty()) {
        return out;
    }

    validate_header const validator(settings, network);
    auto const schedule = tick_schedule_for_tip(validator, chain.headers(), last_idx);
    if ( ! schedule) {
        return std::unexpected(schedule.error());
    }

    std::optional<state> parent;
    if (start_height > 0) {
        auto const stored = chain.get_header_and_abla_state(start_height - 1);
        if (stored) {
            auto const ancestor = chain.headers().get_ancestor(last_idx, int32_t(start_height - 1));
            if (ancestor == header_index::null_index ||
                    domain::chain::hash(std::get<0>(*stored)) != chain.headers().get_hash(ancestor)) {
                spdlog::error("[abla] Persisted parent header does not belong to the batch branch");
                return std::unexpected(error::operation_failed);
            }
            auto const parsed = abla_state_from_record(settings, std::get<1>(*stored), std::get<2>(*stored),
                std::get<3>(*stored), schedule->block_interval(start_height - 1));
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            parent = *parsed;
        } else if (stored.error() != database::result_code::key_not_found) {
            // A read that FAILED is not an absent state, and inventing one from
            // an error would put a made-up consensus input under every block of
            // the batch. An absent row is the durable table lagging the index,
            // which populate(connected_top) tolerates the same way.
            spdlog::error("[abla] cannot read the state of the connected tip at height {}: {}",
                start_height - 1, database::result_code_name(stored.error()));
            return std::unexpected(error::operation_failed);
        }
        if ( ! parent) {
            spdlog::warn("[abla] No ABLA state recorded at height {}; the post-ABLA path "
                "requires reconstruction", start_height - 1);
        }
    }

    out.reserve(sizes.size());
    for (size_t i = 0; i < sizes.size(); ++i) {
        auto const height = size_t(start_height) + i;
        auto const next = advance_abla_state(settings, network, parent, height, sizes[i],
            schedule->block_interval(height));
        if ( ! next) {
            spdlog::error("[abla] the state at height {} cannot be represented", height);
            return std::unexpected(error::operation_failed);
        }
        out.push_back(*next);
        parent = *next;
    }
    return out;
}

std::vector<database::header_abla_record> abla_records_for(
    uint32_t start_height, std::span<state const> states) {
    std::vector<database::header_abla_record> out;
    out.reserve(states.size());
    for (size_t i = 0; i < states.size(); ++i) {
        out.push_back({uint32_t(start_height + i), states[i].block_size,
            states[i].control_block_size, states[i].elastic_buffer_size});
    }
    return out;
}

} // namespace kth::blockchain
#endif // KTH_CURRENCY_BCH
